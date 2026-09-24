/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"

#include "usb/hid_host.h"

// We are allowing realloc ctrl_xfer buffer, so max report desc size is limited by sane value
// based on very large, exotic devices: can go into the low kilobytes
#define HID_MIN_REPORT_DESC_LEN     512u
#define HID_MAX_REPORT_DESC_LEN     2048u

// HID spinlock
static portMUX_TYPE hid_lock = portMUX_INITIALIZER_UNLOCKED;
#define HID_ENTER_CRITICAL()    portENTER_CRITICAL(&hid_lock)
#define HID_EXIT_CRITICAL()     portEXIT_CRITICAL(&hid_lock)

// HID verification macros
#define HID_GOTO_ON_FALSE_CRITICAL(exp, err)    \
    do {                                        \
        if (unlikely(!(exp))) {                 \
            HID_EXIT_CRITICAL();                \
            ret = err;                          \
            goto fail;                          \
        }                                       \
    } while(0)

#define HID_RETURN_ON_FALSE_CRITICAL(exp, err)  \
    do {                                        \
        if (unlikely(!(exp))) {                 \
            HID_EXIT_CRITICAL();                \
            return err;                         \
        }                                       \
    } while(0)

#define HID_GOTO_ON_ERROR(exp, msg) ESP_GOTO_ON_ERROR(exp, fail, TAG, msg)

#define HID_GOTO_ON_FALSE(exp, err, msg) ESP_GOTO_ON_FALSE( (exp), err, fail, TAG, msg )

#define HID_RETURN_ON_ERROR(exp, msg) ESP_RETURN_ON_ERROR((exp), TAG, msg)

#define HID_RETURN_ON_FALSE(exp, err, msg) ESP_RETURN_ON_FALSE( (exp), (err), TAG, msg)

#define HID_RETURN_ON_INVALID_ARG(exp) ESP_RETURN_ON_FALSE((exp) != NULL, ESP_ERR_INVALID_ARG, TAG, "Argument error")

// USB Descriptor parsing helping macros
#define GET_NEXT_INTERFACE_DESC(p, max_len, offs)                                                \
    ((const usb_intf_desc_t *)usb_parse_next_descriptor_of_type((const usb_standard_desc_t *)p,  \
                                                                max_len,                         \
                                                                USB_B_DESCRIPTOR_TYPE_INTERFACE, \
                                                                &(offs)))

#define GET_NEXT_HID_DESC(p, max_len, offs)                                                      \
    ((const hid_descriptor_t *)usb_parse_next_descriptor_of_type((const usb_standard_desc_t *)p, \
                                                                max_len,                         \
                                                                HID_CLASS_DESCRIPTOR_TYPE_HID,   \
                                                                &(offs)))

static const char *TAG = "hid-host";

#define DEFAULT_TIMEOUT_MS  (5000)

// [esp32-nut, review A5a] Timeout of the GET/SET class requests issued while polling.
// The caller (the loopTask) serves NUT and the web UI too: a healthy UPS answers in a
// few ms, so 5 s only lengthened every stall. Descriptor requests at enumeration and
// the lock waits keep DEFAULT_TIMEOUT_MS. To be confirmed by a soak test on APC and Eaton.
#ifndef USBUPS_CTRL_TIMEOUT_MS
#define USBUPS_CTRL_TIMEOUT_MS (1500)
#endif
#define CTRL_REQUEST_TIMEOUT_MS  (USBUPS_CTRL_TIMEOUT_MS)

/**
 * @brief HID Device structure.
 *
 */
typedef struct hid_host_device {
    STAILQ_ENTRY(hid_host_device) tailq_entry;  /**< HID device queue */
    SemaphoreHandle_t device_busy;              /**< HID device main mutex */
    SemaphoreHandle_t ctrl_xfer_done;           /**< Control transfer semaphore */
    usb_transfer_t *ctrl_xfer;                  /**< Pointer to control transfer buffer */
    volatile bool ctrl_inflight;                /**< ctrl_xfer is owned by the USB Host stack (submitted, callback not yet delivered) */
    usb_device_handle_t dev_hdl;                /**< USB device handle */
    uint8_t dev_addr;                           /**< USB device address */
#ifdef HID_HOST_REMOTE_WAKE_SUPPORTED
    bool remote_wakeup_enabled;                 /**< To indicate whether remote wakeup is currently enabled */
#endif // HID_HOST_REMOTE_WAKE_SUPPORTED
} hid_device_t;

/**
 * @brief HID Interface state
*/
typedef enum {
    HID_INTERFACE_STATE_NOT_INITIALIZED = 0x00, /**< HID Interface not initialized */
    HID_INTERFACE_STATE_IDLE,                   /**< HID Interface has been found in connected USB device */
    HID_INTERFACE_STATE_READY,                  /**< HID Interface opened and ready to start transfer */
    HID_INTERFACE_STATE_ACTIVE,                 /**< HID Interface is in use */
    HID_INTERFACE_STATE_WAIT_USER_DELETION,     /**< HID Interface wait user to be removed */
    HID_INTERFACE_STATE_SUSPENDED,              /**< HID Interface (and the whole device) is suspended */
    HID_INTERFACE_STATE_MAX
} hid_iface_state_t;

/**
 * @brief HID Interface structure in device to interact with. After HID device opening keeps the interface configuration
 *
 */
typedef struct hid_interface {
    STAILQ_ENTRY(hid_interface) tailq_entry;
    hid_device_t *parent;                   /**< Parent USB HID device */
    hid_host_dev_params_t dev_params;       /**< USB device parameters */
    uint8_t ep_in;                          /**< Interrupt IN EP number */
    uint16_t ep_in_mps;                     /**< Interrupt IN max size */
    uint8_t country_code;                   /**< Country code */
    uint16_t report_desc_size;              /**< Size of Report */
    uint16_t report_desc_len;               /**< [esp32-nut] Bytes of the Report actually received */
    uint8_t *report_desc;                   /**< Pointer to HID Report */
    usb_transfer_t *in_xfer;                /**< Pointer to IN transfer buffer */
    hid_host_interface_event_cb_t user_cb;  /**< Interface application callback */
    void *user_cb_arg;                      /**< Interface application callback arg */
    hid_iface_state_t state;                /**< Interface state */
    hid_iface_state_t last_state;           /**< Interface last state before entering suspended mode */
} hid_iface_t;

/**
 * @brief HID driver default context
 *
 * This context is created during HID Host install.
 */
typedef struct {
    STAILQ_HEAD(devices, hid_host_device) hid_devices_tailq;    /**< STAILQ of HID interfaces */
    STAILQ_HEAD(interfaces, hid_interface) hid_ifaces_tailq;    /**< STAILQ of HID interfaces */
    usb_host_client_handle_t client_handle;                     /**< Client task handle */
    hid_host_driver_event_cb_t user_cb;                         /**< User application callback */
    void *user_arg;                                             /**< User application callback args */
    bool event_handling_started;                                /**< Events handler started flag */
    SemaphoreHandle_t all_events_handled;                       /**< Events handler semaphore */
    SemaphoreHandle_t open_close_mutex;                         /**< Mutex to prevent race conditions during device open/close */
    volatile bool end_client_event_handling;                    /**< Client event handling flag */
} hid_driver_t;

static hid_driver_t *s_hid_driver;                              /**< Internal pointer to HID driver */
static StaticSemaphore_t s_open_close_mutex_buffer;


// ----------------------- Private Prototypes ----------------------------------

static esp_err_t hid_host_install_device(uint8_t dev_addr,
                                         usb_device_handle_t dev_hdl,
                                         hid_device_t **hid_device);


static esp_err_t hid_host_uninstall_device(hid_device_t *hid_device);
static esp_err_t hid_host_disable_interface_disconnect(hid_iface_t *iface);
static esp_err_t hid_host_device_close_disconnect(hid_host_device_handle_t hid_dev_handle);

// --------------------------- Internal Logic ----------------------------------
/**
 * @brief HID class specific request
*/
typedef struct hid_class_request {
    uint8_t bRequest;               /**< bRequest  */
    uint16_t wValue;                /**< wValue: Report Type and Report ID */
    uint16_t wIndex;                /**< wIndex: Interface */
    uint16_t wLength;               /**< wLength: Report Length */
    uint8_t *data;                  /**< Pointer to data */
} hid_class_request_t;


// ----------------- USB Event Handler - Internal Task -------------------------

/**
 * @brief USB Event handler
 *
 * Handle all USB related events such as USB host (usbh) events or hub events from USB hardware
 *
 * @param[in] arg   Argument, does not used
 */
static void event_handler_task(void *arg)
{
    ESP_LOGD(TAG, "USB HID handling start");
    while (hid_host_handle_events((uint32_t)portMAX_DELAY) == ESP_OK) {
    }
    ESP_LOGD(TAG, "USB HID handling stop");
    vTaskDelete(NULL);
}

/**
 * @brief Return HID device in devices list by USB device handle
 *
 * @param[in] usb_handle   USB device handle
 * @return hid_device_t Pointer to device, NULL if device not present
 */
static hid_device_t *get_hid_device_by_handle(usb_device_handle_t usb_handle)
{
    hid_device_t *device = NULL;

    HID_ENTER_CRITICAL();
    STAILQ_FOREACH(device, &s_hid_driver->hid_devices_tailq, tailq_entry) {
        if (usb_handle == device->dev_hdl) {
            HID_EXIT_CRITICAL();
            return device;
        }
    }
    HID_EXIT_CRITICAL();
    return NULL;
}

/**
 * @brief Return HID Device from the transfer context
 *
 * @param[in] xfer   USB transfer struct
 * @return hid_device_t Pointer to HID Device
 */
static inline hid_device_t *get_hid_device_from_context(usb_transfer_t *xfer)
{
    return (hid_device_t *)xfer->context;
}

/**
 * @brief Verify presence of Interface in the RAM list
 *
 * @param[in] iface         Pointer to an Interface structure
 * @return true             Interface is in the list
 * @return false            Interface is not in the list
 */
static inline bool is_interface_in_list(hid_iface_t *iface)
{
    hid_iface_t *interface = NULL;

    HID_ENTER_CRITICAL();
    STAILQ_FOREACH(interface, &s_hid_driver->hid_ifaces_tailq, tailq_entry) {
        if (iface == interface) {
            HID_EXIT_CRITICAL();
            return true;
        }
    }

    HID_EXIT_CRITICAL();
    return false;
}

/**
 * @brief Get HID Interface pointer by external HID Device handle with verification in RAM list
 *
 * @param[in] hid_dev_handle HID Device handle
 * @return hid_iface_t       Pointer to an Interface structure
 */
static hid_iface_t *get_iface_by_handle(hid_host_device_handle_t hid_dev_handle)
{
    hid_iface_t *hid_iface = (hid_iface_t *) hid_dev_handle;

    if (!is_interface_in_list(hid_iface)) {
        ESP_LOGE(TAG, "HID interface handle not found");
        return NULL;
    }

    return hid_iface;
}

/**
 * @brief Returns pointer to first IN Endpoint descriptor
 *
 * @param[in] iface_desc    Pointer to Interface Descriptor
 * @param[in] total_length  Total length of configuration descriptor
 * @return usb_ep_desc_t Pointer to EP IN Descriptor
 */
static inline const usb_ep_desc_t *get_iface_ep_in(const usb_intf_desc_t *iface_desc,
                                                   const size_t total_length)
{
    assert(iface_desc);
    const usb_ep_desc_t *ep_desc = NULL;
    for (int i = 0; i < iface_desc->bNumEndpoints; i++) {
        int ep_offset = 0;
        ep_desc = usb_parse_endpoint_descriptor_by_index(iface_desc, i, total_length, &ep_offset);
        if (ep_desc) {
            if (USB_EP_DESC_GET_EP_DIR(ep_desc)) {
                return ep_desc;
            }
        }
    }
    return NULL;
}

/**
 * @brief Check HID interface descriptor present
 *
 * @param[in] config_desc  Pointer to Configuration Descriptor
 * @return
 *  - true if HID interface descriptor is present
 *  - false if HID interface descriptor is not present
 */
static bool hid_interface_present(const usb_config_desc_t *config_desc)
{
    assert(config_desc);
    int offset = 0;
    int total_len = config_desc->wTotalLength;
    const usb_intf_desc_t *iface_desc = GET_NEXT_INTERFACE_DESC(config_desc, total_len, offset);
    while (iface_desc != NULL) {
        if (USB_CLASS_HID == iface_desc->bInterfaceClass) {
            return true;
        }
        iface_desc = GET_NEXT_INTERFACE_DESC(iface_desc, total_len, offset);
    }
    return false;
}

/**
 * @brief HID Interface user callback function.
 *
 * @param[in] iface   Pointer to an Interface structure
 * @param[in] event   HID Interface event
 */
static inline void hid_host_user_interface_callback(hid_iface_t *iface,
                                                    const hid_host_interface_event_t event)
{
    assert(iface);

    hid_host_dev_params_t *dev_params = &iface->dev_params;

    assert(dev_params);

    if (iface->user_cb) {
        iface->user_cb(iface, event, iface->user_cb_arg);
    }
}

/**
 * @brief HID Device user callback function.
 *
 * @param[in] iface   Pointer to an Interface structure
 * @param[in] event   HID Device event
 */
static inline void hid_host_user_device_callback(hid_iface_t *iface,
                                                 const hid_host_driver_event_t event)
{
    assert(iface);

    hid_host_dev_params_t *dev_params = &iface->dev_params;

    assert(dev_params);

    if (s_hid_driver && s_hid_driver->user_cb) {
        s_hid_driver->user_cb(iface, event, s_hid_driver->user_arg);
    }
}

/**
 * @brief Add interface in a list
 *
 * @param[in] hid_device    HID device handle
 * @param[in] iface_desc  Pointer to an Interface descriptor
 * @param[in] hid_desc    Pointer to an HID device descriptor
 * @param[in] ep_in_desc  Pointer to an EP descriptor
 * @return esp_err_t
 */
static esp_err_t hid_host_add_interface(hid_device_t *hid_device,
                                        const usb_intf_desc_t *iface_desc,
                                        const hid_descriptor_t *hid_desc,
                                        const usb_ep_desc_t *ep_in_desc)
{
    hid_iface_t *hid_iface = calloc(1, sizeof(hid_iface_t));

    HID_RETURN_ON_FALSE(hid_iface,
                        ESP_ERR_NO_MEM,
                        "Unable to allocate memory");

    HID_ENTER_CRITICAL();
    hid_iface->parent = hid_device;
    hid_iface->state = HID_INTERFACE_STATE_NOT_INITIALIZED;
    hid_iface->dev_params.addr = hid_device->dev_addr;

    if (iface_desc) {
        hid_iface->dev_params.iface_num = iface_desc->bInterfaceNumber;
        hid_iface->dev_params.sub_class = iface_desc->bInterfaceSubClass;
        hid_iface->dev_params.proto = iface_desc->bInterfaceProtocol;
    }

    if (hid_desc) {
        hid_iface->country_code = hid_desc->bCountryCode;
        hid_iface->report_desc_size = hid_desc->wReportDescriptorLength;
    }

    // EP IN && INT Type
    if (ep_in_desc) {
        if ( (ep_in_desc->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) &&
                (ep_in_desc->bmAttributes & USB_B_ENDPOINT_ADDRESS_EP_NUM_MASK) ) {
            hid_iface->ep_in = ep_in_desc->bEndpointAddress;
            hid_iface->ep_in_mps = USB_EP_DESC_GET_MPS(ep_in_desc);
        } else {
            ESP_EARLY_LOGE(TAG, "HID device EP IN %#X configuration error",
                           ep_in_desc->bEndpointAddress);
        }
    }

    if (iface_desc && hid_desc && ep_in_desc) {
        hid_iface->state = HID_INTERFACE_STATE_IDLE;
    }

    STAILQ_INSERT_TAIL(&s_hid_driver->hid_ifaces_tailq, hid_iface, tailq_entry);
    HID_EXIT_CRITICAL();

    return ESP_OK;
}

/**
 * @brief Remove interface from a list
 *
 * Use only inside critical section
 *
 * @param[in] iface    HID interface handle
 * @return esp_err_t
 */
static esp_err_t _hid_host_remove_interface(hid_iface_t *iface)
{
    iface->state = HID_INTERFACE_STATE_NOT_INITIALIZED;
    STAILQ_REMOVE(&s_hid_driver->hid_ifaces_tailq, iface, hid_interface, tailq_entry);
    free(iface);
    return ESP_OK;
}

/**
 * @brief Notify user about the connected Interfaces
 *
 * @param[in] hid_device  Pointer to HID device structure
 */
static void hid_host_notify_interface_connected(hid_device_t *hid_device)
{
    HID_ENTER_CRITICAL();
    hid_iface_t *iface = STAILQ_FIRST(&s_hid_driver->hid_ifaces_tailq);
    hid_iface_t *tmp = NULL;

    while (iface != NULL) {
        tmp = STAILQ_NEXT(iface, tailq_entry);
        HID_EXIT_CRITICAL();

        if (iface->parent && (iface->parent->dev_addr == hid_device->dev_addr)) {
            hid_host_user_device_callback(iface, HID_HOST_DRIVER_EVENT_CONNECTED);
        }
        iface = tmp;

        HID_ENTER_CRITICAL();
    }
    HID_EXIT_CRITICAL();
}

/**
 * @brief Create a list of available interfaces in RAM
 *
 * @param[in] hid_device  Pointer to HID device structure
 * @param[in] config_desc Pointer to USB configuration descriptor
 * @return esp_err_t
 */
static esp_err_t hid_host_interface_list_create(hid_device_t *hid_device,
                                                const usb_config_desc_t *config_desc)
{
    assert(hid_device);
    assert(config_desc);
    size_t total_length = config_desc->wTotalLength;
    const usb_intf_desc_t *iface_desc = NULL;
    const hid_descriptor_t *hid_desc = NULL;
    const usb_ep_desc_t *ep_in_desc = NULL;
    int iface_offset = 0;
    int hid_desc_offset = 0;

    // Get first Interface descriptor
    iface_desc = GET_NEXT_INTERFACE_DESC(config_desc, total_length, iface_offset);
    // For every Interface
    while (iface_desc != NULL) {

        hid_desc = NULL;
        hid_desc_offset = iface_offset;
        ep_in_desc = NULL;

        if (USB_CLASS_HID == iface_desc->bInterfaceClass) {
            ESP_LOGD(TAG, "Found HID, bInterfaceNumber=%d", iface_desc->bInterfaceNumber);
            hid_desc = GET_NEXT_HID_DESC(iface_desc, total_length, hid_desc_offset);
            if (hid_desc) {
                ep_in_desc = get_iface_ep_in(iface_desc, total_length);
                if (ep_in_desc) {
                    HID_RETURN_ON_ERROR( hid_host_add_interface(hid_device,
                                                                iface_desc,
                                                                hid_desc,
                                                                ep_in_desc),
                                         "Unable to add HID Interface to the RAM list");
                }
            }
        } // HID Interface
        iface_desc = GET_NEXT_INTERFACE_DESC(iface_desc, total_length, iface_offset);
    }

    hid_host_notify_interface_connected(hid_device);

    return ESP_OK;
}

/**
 * @brief HID Host initialize device attempt
 *
 * @param[in] dev_addr   USB device physical address
 * @return true USB device contain HID Interface and device was initialized
 * @return false USB does not contain HID Interface
 */
static bool hid_host_device_init_attempt(uint8_t dev_addr)
{
    bool is_hid_device = false;
    usb_device_handle_t dev_hdl = NULL;
    const usb_config_desc_t *config_desc = NULL;
    hid_device_t *hid_device = NULL;

    // [esp32-nut, review C2] The open fails when the UPS resets its USB port while it
    // switches mode: dev_hdl is then not valid and must not be closed. Enumeration
    // errors are logged and cleaned up instead of aborting the HID task (ESP_ERROR_CHECK).
    esp_err_t err = usb_host_device_open(s_hid_driver->client_handle, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open USB device at address %d: %s", dev_addr, esp_err_to_name(err));
        return false;
    }
    if (usb_host_get_active_config_descriptor(dev_hdl, &config_desc) == ESP_OK) {
        is_hid_device = hid_interface_present(config_desc);
    }

    if (!is_hid_device) {
        usb_host_device_close(s_hid_driver->client_handle, dev_hdl);
        ESP_LOGW(TAG, "No HID device at USB port %d", dev_addr);
        return false;
    }

    // Proceed, add HID device to the list, get handle if necessary
    err = hid_host_install_device(dev_addr, dev_hdl, &hid_device);
    if (err != ESP_OK) {
        // hid_host_install_device() released its partial state but not dev_hdl
        ESP_LOGE(TAG, "Unable to install HID device at address %d: %s", dev_addr, esp_err_to_name(err));
        usb_host_device_close(s_hid_driver->client_handle, dev_hdl);
        return false;
    }

    // Create Interfaces list for a possibility to claim Interface
    err = hid_host_interface_list_create(hid_device, config_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to create HID interface list at address %d: %s", dev_addr, esp_err_to_name(err));
        // No interface was notified to the user yet: drop them, then the device (closes dev_hdl)
        HID_ENTER_CRITICAL();
        hid_iface_t *iface = STAILQ_FIRST(&s_hid_driver->hid_ifaces_tailq);
        while (iface != NULL) {
            hid_iface_t *next = STAILQ_NEXT(iface, tailq_entry);
            if (iface->parent == hid_device) {
                _hid_host_remove_interface(iface);
            }
            iface = next;
        }
        HID_EXIT_CRITICAL();
        hid_host_uninstall_device(hid_device);
        return false;
    }

    return true;
}

/**
 * @brief Deinit USB device by handle
 *
 * @param[in] dev_hdl   USB device handle
 * @return esp_err_t
 */
static esp_err_t hid_host_device_disconnected(usb_device_handle_t dev_hdl)
{
    hid_device_t *hid_device = get_hid_device_by_handle(dev_hdl);
    HID_RETURN_ON_INVALID_ARG(hid_device);

    HID_ENTER_CRITICAL();
    hid_iface_t *hid_iface_curr;
    hid_iface_t *hid_iface_next;
    // Go through list
    hid_iface_curr = STAILQ_FIRST(&s_hid_driver->hid_ifaces_tailq);
    while (hid_iface_curr != NULL) {
        hid_iface_next = STAILQ_NEXT(hid_iface_curr, tailq_entry);
        HID_EXIT_CRITICAL();

        if (hid_iface_curr->parent && (hid_iface_curr->parent->dev_addr == hid_device->dev_addr)) {
            // Best-effort cleanup: an individual interface close failure must not
            // prevent the remaining interfaces or the final uninstall_device from
            // being processed. The device is already physically gone from the bus,
            // so we call the disconnect-specific close variant which swallows
            // endpoint errors; the graceful hid_host_device_close() path stays strict.
            esp_err_t close_err = hid_host_device_close_disconnect(hid_iface_curr);
            if (close_err != ESP_OK) {
                ESP_LOGW(TAG, "Close iface %d failed on disconnect (continuing): %s",
                         hid_iface_curr->dev_params.iface_num,
                         esp_err_to_name(close_err));
            }
        }
        HID_ENTER_CRITICAL();
        hid_iface_curr = hid_iface_next;
    }
    HID_EXIT_CRITICAL();

    // Delete HID compliant device — must run even if an individual close above failed,
    // otherwise the device context is leaked and cannot be recovered without a
    // physical re-plug of the USB device. Interfaces with a user callback remain
    // in hid_ifaces_tailq in HID_INTERFACE_STATE_WAIT_USER_DELETION awaiting the
    // user's second hid_host_device_close(); that is a normal state and must not
    // be altered here. The best-effort path inside hid_host_device_close_impl()
    // forces the state forward on disable/release failure, so by the time we reach
    // this point any interface that still references hid_device is already either
    // (a) owned by the user (WAIT_USER_DELETION) or (b) removed from the list.
    esp_err_t uninstall_err = hid_host_uninstall_device(hid_device);
    if (uninstall_err != ESP_OK) {
        ESP_LOGW(TAG, "Uninstall device failed on disconnect (continuing): %s",
                 esp_err_to_name(uninstall_err));
    }

    return ESP_OK;
}

#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED

/**
 * @brief Suspend interface
 *
 * @note endpoints are already halted and flushed when a global suspend is issues by the USB Host lib
 * @param[in] iface    HID interface handle
 * @param[in] stop_ep  Stop (halt and flush) endpoint
 *
 * @return esp_err_t
 */
static esp_err_t hid_host_suspend_interface(hid_iface_t *iface, bool stop_ep)
{
    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface),
                        ESP_ERR_NOT_FOUND,
                        "Interface handle not found");

    HID_RETURN_ON_FALSE((HID_INTERFACE_STATE_SUSPENDED != iface->state),
                        ESP_ERR_INVALID_STATE,
                        "Interface wrong state");

    // EP is usually stopped by usb_host_lib, in case of global suspend, thus no need to Halt->Flush EP again
    if (stop_ep) {
        HID_RETURN_ON_ERROR( usb_host_endpoint_halt(iface->parent->dev_hdl, iface->ep_in),
                             "Unable to HALT EP");
        HID_RETURN_ON_ERROR( usb_host_endpoint_flush(iface->parent->dev_hdl, iface->ep_in),
                             "Unable to FLUSH EP");
        // Don't clear EP, it must remain halted, when the device is in suspended state
    }

    iface->last_state = iface->state;
    iface->state = HID_INTERFACE_STATE_SUSPENDED;

    return ESP_OK;
}

/**
 * @brief Resume interface
 *
 * @note endpoints are already cleared when a global resume is issues by the USB Host lib
 * @param[in] iface      HID interface handle
 * @param[in] resume_ep  Resume (clear) endpoint
 *
 * @return esp_err_t
 */
static esp_err_t hid_host_resume_interface(hid_iface_t *iface, bool resume_ep)
{
    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface),
                        ESP_ERR_NOT_FOUND,
                        "Interface handle not found");

    if (HID_INTERFACE_STATE_ACTIVE == iface->state) {
        // Interface already auto-resumed by hid_host_device_start(), return early and continue to resume event delivery
        return ESP_OK;
    }

    HID_RETURN_ON_FALSE ((HID_INTERFACE_STATE_SUSPENDED == iface->state),
                         ESP_ERR_INVALID_STATE,
                         "Interface wrong state");

    // EP is usually cleared by usb_host_lib, in case of global suspend, thus no need to Clear an EP again
    if (resume_ep) {
        usb_host_endpoint_clear(iface->parent->dev_hdl, iface->ep_in);
    }

    // Use the last device state before the device went to suspended state as the current state
    iface->state = iface->last_state;

    if (iface->in_xfer == NULL) {
        return ESP_OK;
    }

    // If the last state before the device went to suspended state was active state, start the data transfer
    if (iface->last_state == HID_INTERFACE_STATE_ACTIVE) {
        // start data transfer
        HID_RETURN_ON_ERROR( usb_host_transfer_submit(iface->in_xfer), "Unable to start data transfer");
    }

    return ESP_OK;
}

/**
 * @brief Suspend device
 *
 * Go through list, suspend all devices and deliver suspend events
 *
 * @param[in] dev_hdl    USB Device handle
 *
 * @return esp_err_t
 */
static esp_err_t hid_host_device_suspended(usb_device_handle_t dev_hdl)
{
    hid_device_t *hid_device = get_hid_device_by_handle(dev_hdl);
    HID_RETURN_ON_INVALID_ARG(hid_device);

    HID_ENTER_CRITICAL();
    hid_iface_t *hid_iface_curr;
    hid_iface_t *hid_iface_next;
    // Go through list
    hid_iface_curr = STAILQ_FIRST(&s_hid_driver->hid_ifaces_tailq);
    while (hid_iface_curr != NULL) {
        hid_iface_next = STAILQ_NEXT(hid_iface_curr, tailq_entry);
        HID_EXIT_CRITICAL();

        if (hid_iface_curr->parent && (hid_iface_curr->parent->dev_addr == hid_device->dev_addr)) {
            esp_err_t ret = hid_host_suspend_interface(hid_iface_curr, false);

            // Make sure the device is connected and the interface is found otherwise don't deliver suspend event
            if (ret != ESP_ERR_NOT_FOUND) {

                // We will deliver the suspend event, if the hid_host_suspend_interface fails with other errors,
                // as the usb_host_lib has already suspended the root port anyway
                hid_host_user_interface_callback(hid_iface_curr, HID_HOST_INTERFACE_EVENT_SUSPENDED);
            }
        }
        HID_ENTER_CRITICAL();
        hid_iface_curr = hid_iface_next;
    }
    HID_EXIT_CRITICAL();

    return ESP_OK;
}

/**
 * @brief Resume device
 *
 * Go through list, resume all devices and deliver resume events
 *
 * @param[in] dev_hdl    USB Device handle
 *
 * @return esp_err_t
 */
static esp_err_t hid_host_device_resumed(usb_device_handle_t dev_hdl)
{
    hid_device_t *hid_device = get_hid_device_by_handle(dev_hdl);
    HID_RETURN_ON_INVALID_ARG(hid_device);

    HID_ENTER_CRITICAL();
    hid_iface_t *hid_iface_curr;
    hid_iface_t *hid_iface_next;
    // Go through list
    hid_iface_curr = STAILQ_FIRST(&s_hid_driver->hid_ifaces_tailq);
    while (hid_iface_curr != NULL) {
        hid_iface_next = STAILQ_NEXT(hid_iface_curr, tailq_entry);
        HID_EXIT_CRITICAL();

        if (hid_iface_curr->parent && (hid_iface_curr->parent->dev_addr == hid_device->dev_addr)) {
            esp_err_t ret = hid_host_resume_interface(hid_iface_curr, false);

            // Make sure the device is connected and the interface is found otherwise don't deliver resume event
            if (ret != ESP_ERR_NOT_FOUND) {

                // We will deliver the resume event, if the hid_host_resume_interface fails with other errors,
                // as the usb_host_lib has already resumed the root port anyway
                hid_host_user_interface_callback(hid_iface_curr, HID_HOST_INTERFACE_EVENT_RESUMED);
            }
        }
        HID_ENTER_CRITICAL();
        hid_iface_curr = hid_iface_next;
    }
    HID_EXIT_CRITICAL();

    return ESP_OK;
}

#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED

/**
 * @brief USB Host Client's event callback
 *
 * @param[in] event    Client event message
 * @param[in] arg      Argument, does not used
 */
static void client_event_cb(const usb_host_client_event_msg_t *event, void *arg)
{
    switch (event->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGD(TAG, "New device connected");
        hid_host_device_init_attempt(event->new_dev.address);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGD(TAG, "Device suddenly disconnected");
        hid_host_device_disconnected(event->dev_gone.dev_hdl);
        break;
#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
    case USB_HOST_CLIENT_EVENT_DEV_SUSPENDED:
        ESP_LOGD(TAG, "Device suspended");
        hid_host_device_suspended(event->dev_suspend_resume.dev_hdl);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_RESUMED:
        ESP_LOGD(TAG, "Device resumed");
        hid_host_device_resumed(event->dev_suspend_resume.dev_hdl);
        break;
#endif // HID_HOST_SUSPEND_RESUME_API_SUPPORTED
    default:
        ESP_LOGW(TAG, "Unrecognized USB Host client event");
        break;
    }
}

/**
 * @brief HID Host claim Interface and prepare transfer, change state to READY
 *
 * @param[in] iface       Pointer to Interface structure,
 * @return esp_err_t
 */
static esp_err_t hid_host_interface_claim_and_prepare_transfer(hid_iface_t *iface)
{
    HID_RETURN_ON_ERROR( usb_host_interface_claim( s_hid_driver->client_handle,
                                                   iface->parent->dev_hdl,
                                                   iface->dev_params.iface_num, 0),
                         "Unable to claim Interface");

    // [esp32-nut, review M3] Do not leave the interface claimed when the allocation fails
    esp_err_t ret = usb_host_transfer_alloc(iface->ep_in_mps, 0, &iface->in_xfer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to allocate transfer buffer for EP IN");
        iface->in_xfer = NULL;
        usb_host_interface_release(s_hid_driver->client_handle,
                                   iface->parent->dev_hdl,
                                   iface->dev_params.iface_num);
        return ret;
    }

    // Change state
    iface->state = HID_INTERFACE_STATE_READY;
    return ESP_OK;
}

/**
 * @brief HID Host release Interface and free transfer, change state to IDLE
 *
 * @param[in] iface       Pointer to Interface structure,
 * @return esp_err_t
 */
static esp_err_t hid_host_interface_release_and_free_transfer(hid_iface_t *iface)
{
    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface),
                        ESP_ERR_NOT_FOUND,
                        "Interface handle not found");

    HID_RETURN_ON_ERROR( usb_host_interface_release(s_hid_driver->client_handle,
                                                    iface->parent->dev_hdl,
                                                    iface->dev_params.iface_num),
                         "Unable to release HID Interface");

    // [esp32-nut, review M3] No abort() on a free error, and no dangling pointer after it
    esp_err_t ret = usb_host_transfer_free(iface->in_xfer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to free IN transfer: %s", esp_err_to_name(ret));
    }
    iface->in_xfer = NULL;

    // Change state
    iface->state = HID_INTERFACE_STATE_IDLE;
    return ESP_OK;
}

/**
 * @brief Disable active interface (graceful path — strict error propagation).
 *
 * Called from the public hid_host_device_stop() / hid_host_device_close() code
 * path while the device is still present on the bus. Any failure from
 * usb_host_endpoint_halt() / usb_host_endpoint_flush() is a real error for the
 * caller and is returned so the caller can react.
 *
 * For the disconnect cleanup path (where the device is already physically gone
 * and these calls routinely return ESP_ERR_INVALID_STATE), use
 * hid_host_disable_interface_disconnect() instead.
 *
 * @param[in] iface       Pointer to Interface structure
 * @return esp_err_t
 */
static esp_err_t hid_host_disable_interface(hid_iface_t *iface)
{
    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface),
                        ESP_ERR_NOT_FOUND,
                        "Interface handle not found");

    HID_RETURN_ON_FALSE((HID_INTERFACE_STATE_ACTIVE == iface->state ||
                         HID_INTERFACE_STATE_SUSPENDED == iface->state),
                        ESP_ERR_INVALID_STATE,
                        "Interface wrong state");

    if (HID_INTERFACE_STATE_ACTIVE == iface->state) {
        HID_RETURN_ON_ERROR( usb_host_endpoint_halt(iface->parent->dev_hdl, iface->ep_in),
                             "Unable to HALT EP");
        HID_RETURN_ON_ERROR( usb_host_endpoint_flush(iface->parent->dev_hdl, iface->ep_in),
                             "Unable to FLUSH EP");
    }
    // If interface state is suspended, the EP is already flushed and halted, only clear the EP
    // If suspended, may return ESP_ERR_INVALID_STATE
    usb_host_endpoint_clear(iface->parent->dev_hdl, iface->ep_in);

    iface->state = HID_INTERFACE_STATE_READY;

    return ESP_OK;
}

/**
 * @brief Disable interface on the disconnect cleanup path (best-effort).
 *
 * usb_host_endpoint_halt() / usb_host_endpoint_flush() are expected to fail with
 * ESP_ERR_INVALID_STATE once the device has disappeared from the bus. Aborting
 * here would prevent endpoint_clear, the state transition, and ultimately
 * hid_host_uninstall_device() from running, which leaks the USB device context
 * so badly that only a physical USB re-plug recovers it. We therefore log these
 * failures at debug level and push through to the clear/state-transition path.
 *
 * @param[in] iface       Pointer to Interface structure
 * @return esp_err_t
 */
static esp_err_t hid_host_disable_interface_disconnect(hid_iface_t *iface)
{
    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface),
                        ESP_ERR_NOT_FOUND,
                        "Interface handle not found");

    HID_RETURN_ON_FALSE((HID_INTERFACE_STATE_ACTIVE == iface->state ||
                         HID_INTERFACE_STATE_SUSPENDED == iface->state),
                        ESP_ERR_INVALID_STATE,
                        "Interface wrong state");

    if (HID_INTERFACE_STATE_ACTIVE == iface->state) {
        esp_err_t ep_err;
        ep_err = usb_host_endpoint_halt(iface->parent->dev_hdl, iface->ep_in);
        if (ep_err != ESP_OK) {
            ESP_LOGD(TAG, "EP halt failed on disconnect (device likely gone): %s",
                     esp_err_to_name(ep_err));
        }
        ep_err = usb_host_endpoint_flush(iface->parent->dev_hdl, iface->ep_in);
        if (ep_err != ESP_OK) {
            ESP_LOGD(TAG, "EP flush failed on disconnect (device likely gone): %s",
                     esp_err_to_name(ep_err));
        }
    }
    usb_host_endpoint_clear(iface->parent->dev_hdl, iface->ep_in);

    iface->state = HID_INTERFACE_STATE_READY;

    return ESP_OK;
}

/**
 * @brief HID IN Transfer complete callback
 *
 * @param[in] in_xfer  Pointer to transfer data structure
 */
static void in_xfer_done(usb_transfer_t *in_xfer)
{
    assert(in_xfer);
    assert(in_xfer->context);

    hid_iface_t *iface = (hid_iface_t *) in_xfer->context;

    switch (in_xfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED:
        // Notify user
        hid_host_user_interface_callback(iface, HID_HOST_INTERFACE_EVENT_INPUT_REPORT);
        // Relaunch transfer
        // [esp32-nut, ADR 0008] A failed re-submit silently stops INPUT reports forever:
        // report it so the application can restart the interface.
        esp_err_t err = usb_host_transfer_submit(in_xfer);
        if (err != ESP_OK && iface->state == HID_INTERFACE_STATE_ACTIVE) {
            ESP_LOGE(TAG, "Unable to re-submit IN transfer: %s", esp_err_to_name(err));
            hid_host_user_interface_callback(iface, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR);
        }
        return;
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
        // User is notified about device disconnection from usb_event_cb
        // No need to do anything
        return;
    default:
        // Any other error
        break;
    }

    ESP_LOGE(TAG, "Transfer failed, status %d", in_xfer->status);
    // Notify user about transfer or any other error
    hid_host_user_interface_callback(iface, HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR);
}

/** Lock HID device from other task
 *
 * @param[in] hid_device    Pointer to HID device structure
 * @param[in] timeout_ms    Timeout of trying to take the mutex
 * @return
 *    - ESP_OK if the mutex was successfully taken
 *    - ESP_ERR_TIMEOUT if the mutex could not be taken within the specified timeout
 */
static inline esp_err_t hid_device_try_lock(hid_device_t *hid_device, uint32_t timeout_ms)
{
    return ( xSemaphoreTake(hid_device->device_busy, pdMS_TO_TICKS(timeout_ms))
             ? ESP_OK
             : ESP_ERR_TIMEOUT );
}

/** Unlock HID device from other task
 *
 * @param[in] hid_device    Pointer to HID device structure
 */
static inline void hid_device_unlock(hid_device_t *hid_device)
{
    xSemaphoreGive(hid_device->device_busy);
}

/**
 * @brief HID Control transfer complete callback
 *
 * @param[in] ctrl_xfer  Pointer to transfer data structure
 */
static void ctrl_xfer_done(usb_transfer_t *ctrl_xfer)
{
    assert(ctrl_xfer);
    hid_device_t *hid_device = (hid_device_t *)ctrl_xfer->context;
    // [esp32-nut, ADR 0008] Hand the buffer back before waking the requester.
    // If the requester already gave up (timeout), the token left in the binary
    // semaphore is drained by the next hid_control_transfer().
    hid_device->ctrl_inflight = false;
    xSemaphoreGive(hid_device->ctrl_xfer_done);
}

/**
 * @brief Lock HID device for a control request
 *
 * [esp32-nut, ADR 0008] Besides taking the device mutex, refuses the request while a
 * previous control transfer is still owned by the USB Host stack. EP0 cannot be halted
 * or flushed by a client (usbh rejects EP0 in usb_host_endpoint_halt/flush/clear), so
 * after a timeout the only safe option is to leave ctrl_xfer untouched until its
 * callback is delivered: writing the setup packet, re-submitting or re-allocating it
 * earlier corrupts an URB that the HCD is still processing.
 *
 * @param[in] hid_device    Pointer to HID device structure
 * @param[in] timeout_ms    Timeout of trying to take the mutex
 * @return
 *    - ESP_OK if the device is locked and ctrl_xfer can be used
 *    - ESP_ERR_TIMEOUT if the mutex could not be taken within the specified timeout
 *    - ESP_ERR_INVALID_STATE if a previous control transfer is still in flight
 */
static esp_err_t hid_device_lock_ctrl(hid_device_t *hid_device, uint32_t timeout_ms)
{
    HID_RETURN_ON_ERROR( hid_device_try_lock(hid_device, timeout_ms),
                         "HID Device is busy by other task");
    if (hid_device->ctrl_inflight) {
        hid_device_unlock(hid_device);
        ESP_LOGW(TAG, "Previous control transfer still in flight, request rejected");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/**
 * @brief HID control transfer synchronous.
 *
 * @param[in] hid_device  Pointer to HID device structure
 * @param[in] len         Number of bytes to transfer
 * @param[in] timeout_ms  Timeout in ms
 * @return
 *   - ESP_OK if the transfer was successful
 *   - ESP_ERR_TIMEOUT if the transfer was not completed within the specified timeout
 *   - ESP_ERR_INVALID_RESPONSE if the device answered with a STALL
 *   - ESP_ERR_INVALID_SIZE if the device sent more data than the buffer can hold (babble)
 *   - ESP_FAIL if the transfer failed on the bus (error, device gone, cancelled)
 *
 * @note Caller must hold the device lock taken with hid_device_lock_ctrl().
 */
static esp_err_t hid_control_transfer(hid_device_t *hid_device,
                                      size_t len,
                                      uint32_t timeout_ms)
{

    usb_transfer_t *ctrl_xfer = hid_device->ctrl_xfer;

    // [esp32-nut, ADR 0008] Drop a stale completion token left by a callback that arrived
    // after a previous timeout, otherwise this transfer would "complete" immediately
    // with the data of the previous one.
    xSemaphoreTake(hid_device->ctrl_xfer_done, 0);

    ctrl_xfer->device_handle = hid_device->dev_hdl;
    ctrl_xfer->callback = ctrl_xfer_done;
    ctrl_xfer->context = hid_device;
    ctrl_xfer->bEndpointAddress = 0;
    ctrl_xfer->timeout_ms = timeout_ms;
    ctrl_xfer->num_bytes = len;

    hid_device->ctrl_inflight = true;
    esp_err_t ret = usb_host_transfer_submit_control(s_hid_driver->client_handle, ctrl_xfer);
    if (ret != ESP_OK) {
        hid_device->ctrl_inflight = false;
        ESP_LOGE(TAG, "Unable to submit control transfer: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t received = xSemaphoreTake(hid_device->ctrl_xfer_done, pdMS_TO_TICKS(timeout_ms));

    // [esp32-nut, ADR 0008] The HCD does not implement transfer timeouts and a client cannot
    // halt/flush EP0, so the URB stays owned by the stack. ctrl_inflight remains set and
    // hid_device_lock_ctrl() rejects further requests until the callback shows up.
    HID_RETURN_ON_FALSE(received == pdTRUE, ESP_ERR_TIMEOUT, "Control transfer timeout");

    switch (ctrl_xfer->status) {
    case USB_TRANSFER_STATUS_COMPLETED:
        break;
    case USB_TRANSFER_STATUS_STALL:
        ESP_LOGD(TAG, "Control transfer stalled");
        return ESP_ERR_INVALID_RESPONSE;
    case USB_TRANSFER_STATUS_OVERFLOW:
        ESP_LOGW(TAG, "Control transfer overflow");
        return ESP_ERR_INVALID_SIZE;
    default:
        ESP_LOGW(TAG, "Control transfer failed, status %d", ctrl_xfer->status);
        return ESP_FAIL;
    }
    if (ctrl_xfer->actual_num_bytes < USB_SETUP_PACKET_SIZE) {
        ESP_LOGW(TAG, "Control transfer too short (%d bytes)", ctrl_xfer->actual_num_bytes);
        return ESP_ERR_INVALID_SIZE;
    }
    // Device can return less data than requested, but it might return more data due to padding (e.g. APC UPS)
    if (ctrl_xfer->actual_num_bytes > ctrl_xfer->num_bytes) {
        ESP_LOGD(TAG, "Device returned more data than requested (%d > %d), truncating", ctrl_xfer->actual_num_bytes, ctrl_xfer->num_bytes);
        ctrl_xfer->actual_num_bytes = ctrl_xfer->num_bytes;
    }

    ESP_LOG_BUFFER_HEXDUMP(TAG, ctrl_xfer->data_buffer, ctrl_xfer->actual_num_bytes, ESP_LOG_DEBUG);

    return ESP_OK;
}

/**
 * @brief USB class standard request get descriptor
 *
 * @param[in] hid_device  Pointer to HID device structure
 * @param[in] req         Pointer to a class specific request structure
 * @param[out] received   [esp32-nut, review C3] Bytes copied into req->data, can be less than wLength
 * @param[in] recipient   [esp32-nut, review M5] USB_BM_REQUEST_TYPE_RECIP_INTERFACE (report
 *                        descriptor) or USB_BM_REQUEST_TYPE_RECIP_DEVICE (string descriptors)
 * @return esp_err_t
 */
static esp_err_t usb_class_request_get_descriptor(hid_device_t *hid_device, const hid_class_request_t *req,
                                                  size_t *received, uint8_t recipient)
{
    HID_RETURN_ON_INVALID_ARG(hid_device);
    HID_RETURN_ON_INVALID_ARG(hid_device->ctrl_xfer);
    HID_RETURN_ON_INVALID_ARG(req);
    HID_RETURN_ON_INVALID_ARG(req->data);

    // [esp32-nut, ADR 0008] Lock refuses while ctrl_xfer is in flight: freeing it below
    // would hand a dangling URB to the HCD.
    HID_RETURN_ON_ERROR( hid_device_lock_ctrl(hid_device, DEFAULT_TIMEOUT_MS),
                         "Control pipe not available");

    esp_err_t ret;
    const size_t ctrl_size = hid_device->ctrl_xfer->data_buffer_size;
    const size_t required_size = USB_SETUP_PACKET_SIZE + req->wLength;

    // Reallocate control transfer buffer if necessary
    if (ctrl_size < required_size) {
        ESP_LOGD(TAG, "Change HID ctrl xfer size from %"PRIu32" to %"PRIu32"",
                 (uint32_t) ctrl_size,
                 (uint32_t) required_size);

        usb_host_transfer_free(hid_device->ctrl_xfer);

        if (usb_host_transfer_alloc(required_size, 0, &hid_device->ctrl_xfer) != ESP_OK) {
            ESP_LOGE(TAG, "Unable to re-allocate transfer buffer for EP0");
            hid_device->ctrl_xfer = NULL;
            hid_device_unlock(hid_device);
            return ESP_ERR_NO_MEM;
        }
    }

    usb_transfer_t *ctrl_xfer = hid_device->ctrl_xfer;
    usb_setup_packet_t *setup = (usb_setup_packet_t *)ctrl_xfer->data_buffer;

    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN |
                           USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                           recipient;
    setup->bRequest = req->bRequest;
    setup->wValue = req->wValue;
    setup->wIndex = req->wIndex;
    setup->wLength = req->wLength;

    ret = hid_control_transfer(hid_device, required_size, DEFAULT_TIMEOUT_MS);

    if (ret == ESP_OK) {
        if (ctrl_xfer->actual_num_bytes < USB_SETUP_PACKET_SIZE) {
            ret = ESP_ERR_INVALID_SIZE;
        } else {
            uint32_t response_len = ctrl_xfer->actual_num_bytes - USB_SETUP_PACKET_SIZE;
            if (response_len <= req->wLength) {
                memcpy(req->data,
                       ctrl_xfer->data_buffer + USB_SETUP_PACKET_SIZE,
                       response_len);
                if (received) {
                    *received = response_len;
                }
            } else {
                ret = ESP_ERR_INVALID_SIZE;
            }
        }
    }

    hid_device_unlock(hid_device);

    return ret;
}

/**
 * @brief HID Host Request Report Descriptor
 *
 * @param[in] iface       Pointer to HID Interface configuration structure
 * @return esp_err_t
 */
static esp_err_t hid_class_request_report_descriptor(hid_iface_t *iface)
{
    HID_RETURN_ON_INVALID_ARG(iface);

    // Get Report Descriptor is possible only in Ready or Active state
    HID_RETURN_ON_FALSE((HID_INTERFACE_STATE_READY == iface->state) ||
                        (HID_INTERFACE_STATE_ACTIVE == iface->state),
                        ESP_ERR_INVALID_STATE,
                        "Unable to request report descriptor. Interface is not ready");

    // Check if the report descriptor size is within the maximum allowed size before allocating memory
    HID_RETURN_ON_FALSE(iface->report_desc_size <= HID_MAX_REPORT_DESC_LEN,
                        ESP_ERR_INVALID_SIZE,
                        "Requested descriptor size exceeds maximum");

    iface->report_desc = malloc(iface->report_desc_size);
    HID_RETURN_ON_FALSE(iface->report_desc,
                        ESP_ERR_NO_MEM,
                        "Unable to allocate memory");

    const hid_class_request_t get_desc = {
        .bRequest = USB_B_REQUEST_GET_DESCRIPTOR,
        .wValue = (HID_CLASS_DESCRIPTOR_TYPE_REPORT << 8),
        .wIndex = iface->dev_params.iface_num,
        .wLength = iface->report_desc_size,
        .data = iface->report_desc
    };

    size_t received = 0;
    esp_err_t ret = usb_class_request_get_descriptor(iface->parent, &get_desc, &received,
                                                     USB_BM_REQUEST_TYPE_RECIP_INTERFACE);

    // [esp32-nut, review C3] Some UPS answer with fewer bytes than wReportDescriptorLength:
    // expose only what was received, never the uninitialized tail of the buffer.
    if (ret == ESP_OK && received == 0) {
        ESP_LOGW(TAG, "Empty report descriptor");
        ret = ESP_ERR_INVALID_SIZE;
    }
    if (ret == ESP_OK && received < iface->report_desc_size) {
        ESP_LOGW(TAG, "Short report descriptor: %u of %u bytes",
                 (unsigned) received, (unsigned) iface->report_desc_size);
    }

    if (ret != ESP_OK) {
        free(iface->report_desc);
        iface->report_desc = NULL;
        iface->report_desc_len = 0;
    } else {
        iface->report_desc_len = (uint16_t) received;
    }
    return ret;
}

/**
 * @brief HID class specific request Set
 *
 * @param[in] hid_device Pointer to HID device structure
 * @param[in] req        Pointer to a class specific request structure
 * @return esp_err_t
 */
static esp_err_t hid_class_request_set(hid_device_t *hid_device,
                                       const hid_class_request_t *req)
{
    esp_err_t ret;
    HID_RETURN_ON_INVALID_ARG(hid_device);
    HID_RETURN_ON_INVALID_ARG(hid_device->ctrl_xfer);
    usb_transfer_t *ctrl_xfer = hid_device->ctrl_xfer;
    HID_RETURN_ON_FALSE(USB_SETUP_PACKET_SIZE + req->wLength <= ctrl_xfer->data_buffer_size,
                        ESP_ERR_INVALID_SIZE,
                        "Request exceeds control transfer buffer");

    HID_RETURN_ON_ERROR( hid_device_lock_ctrl(hid_device, DEFAULT_TIMEOUT_MS),
                         "Control pipe not available");

    usb_setup_packet_t *setup = (usb_setup_packet_t *)ctrl_xfer->data_buffer;
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                           USB_BM_REQUEST_TYPE_TYPE_CLASS |
                           USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    setup->bRequest = req->bRequest;
    setup->wValue = req->wValue;
    setup->wIndex = req->wIndex;
    setup->wLength = req->wLength;

    if (req->wLength && req->data) {
        memcpy(ctrl_xfer->data_buffer + USB_SETUP_PACKET_SIZE, req->data, req->wLength);
    }

    ret = hid_control_transfer(hid_device,
                               USB_SETUP_PACKET_SIZE + setup->wLength,
                               CTRL_REQUEST_TIMEOUT_MS);

    hid_device_unlock(hid_device);

    return ret;
}

/**
 * @brief HID class specific request Get
 *
 * @param[in] hid_device    Pointer to HID device structure
 * @param[in] req           Pointer to a class specific request structure
 * @param[out] out_length   Length of the response in data buffer of req struct
 * @return esp_err_t
 */
static esp_err_t hid_class_request_get(hid_device_t *hid_device,
                                       const hid_class_request_t *req,
                                       size_t *out_length)
{
    esp_err_t ret;
    HID_RETURN_ON_INVALID_ARG(hid_device);
    HID_RETURN_ON_INVALID_ARG(hid_device->ctrl_xfer);

    usb_transfer_t *ctrl_xfer = hid_device->ctrl_xfer;
    HID_RETURN_ON_FALSE(USB_SETUP_PACKET_SIZE + req->wLength <= ctrl_xfer->data_buffer_size,
                        ESP_ERR_INVALID_SIZE,
                        "Request exceeds control transfer buffer");

    HID_RETURN_ON_ERROR( hid_device_lock_ctrl(hid_device, DEFAULT_TIMEOUT_MS),
                         "Control pipe not available");

    usb_setup_packet_t *setup = (usb_setup_packet_t *)ctrl_xfer->data_buffer;

    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN |
                           USB_BM_REQUEST_TYPE_TYPE_CLASS |
                           USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    setup->bRequest = req->bRequest;
    setup->wValue = req->wValue;
    setup->wIndex = req->wIndex;
    setup->wLength = req->wLength;

    ret = hid_control_transfer(hid_device,
                               USB_SETUP_PACKET_SIZE + setup->wLength,
                               CTRL_REQUEST_TIMEOUT_MS);

    if (ESP_OK == ret) {
        // We do not need the setup data, which is still in the transfer data buffer
        ctrl_xfer->actual_num_bytes -= USB_SETUP_PACKET_SIZE;
        // Copy data if the size is ok
        if (ctrl_xfer->actual_num_bytes <= req->wLength) {
            memcpy(req->data, ctrl_xfer->data_buffer + USB_SETUP_PACKET_SIZE, ctrl_xfer->actual_num_bytes);
            // return actual num bytes of response
            if (out_length) {
                *out_length = ctrl_xfer->actual_num_bytes;
            }
        } else {
            ret = ESP_ERR_INVALID_SIZE;
        }
    }

    hid_device_unlock(hid_device);

    return ret;
}

// ---------------------------- Private ---------------------------------------
static esp_err_t hid_host_string_descriptor_copy(wchar_t *dest,
                                                 const usb_str_desc_t *src)
{
    if (dest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // [esp32-nut, review C4] A malformed bLength < 2 made the length negative, i.e. huge
    if (src != NULL && src->bLength >= USB_STANDARD_DESC_SIZE) {
        size_t len = MIN((src->bLength - USB_STANDARD_DESC_SIZE) / 2, HID_STR_DESC_MAX_LENGTH - 1);
        for (int i = 0; i < len; i++) {
            dest[i] = (wchar_t) src->wData[i];
        }
        // This should be always true, we just check to avoid LoadProhibited exception
        if (dest != NULL) {
            dest[len] = 0;
        }
    } else {
        dest[0] = 0;
    }
    return ESP_OK;
}

static esp_err_t hid_host_install_device(uint8_t dev_addr,
                                         usb_device_handle_t dev_hdl,
                                         hid_device_t **hid_device_handle)
{
    esp_err_t ret;
    hid_device_t *hid_device;

    HID_GOTO_ON_FALSE( hid_device = calloc(1, sizeof(hid_device_t)),
                       ESP_ERR_NO_MEM,
                       "Unable to allocate memory for HID Device");

    hid_device->dev_addr = dev_addr;
    hid_device->dev_hdl = dev_hdl;

    HID_GOTO_ON_FALSE( hid_device->ctrl_xfer_done = xSemaphoreCreateBinary(),
                       ESP_ERR_NO_MEM,
                       "Unable to create semaphore");
    HID_GOTO_ON_FALSE( hid_device->device_busy =  xSemaphoreCreateMutex(),
                       ESP_ERR_NO_MEM,
                       "Unable to create semaphore");

    /*
    * TIP: Usually, we need to allocate 'EP bMaxPacketSize0 + 1' here.
    * To take the size of a report descriptor into a consideration,
    * we need to allocate more here.
    */
    HID_GOTO_ON_ERROR(usb_host_transfer_alloc(HID_MIN_REPORT_DESC_LEN, 0, &hid_device->ctrl_xfer),
                      "Unable to allocate transfer buffer");

    HID_ENTER_CRITICAL();
    HID_GOTO_ON_FALSE_CRITICAL( s_hid_driver, ESP_ERR_INVALID_STATE );
    HID_GOTO_ON_FALSE_CRITICAL( s_hid_driver->client_handle, ESP_ERR_INVALID_STATE );
    STAILQ_INSERT_TAIL(&s_hid_driver->hid_devices_tailq, hid_device, tailq_entry);
    HID_EXIT_CRITICAL();

    if (hid_device_handle) {
        *hid_device_handle = hid_device;
    }

    return ESP_OK;

fail:
    // [esp32-nut, review C2] The device is not in the list yet and dev_hdl belongs to
    // the caller: hid_host_uninstall_device() would close it and STAILQ_REMOVE a
    // missing element. Release only what was allocated here.
    if (hid_device) {
        if (hid_device->ctrl_xfer) {
            usb_host_transfer_free(hid_device->ctrl_xfer);
        }
        if (hid_device->ctrl_xfer_done) {
            vSemaphoreDelete(hid_device->ctrl_xfer_done);
        }
        if (hid_device->device_busy) {
            vSemaphoreDelete(hid_device->device_busy);
        }
        free(hid_device);
    }
    return ret;
}

esp_err_t hid_host_uninstall_device(hid_device_t *hid_device)
{
    HID_RETURN_ON_INVALID_ARG(hid_device);

    // [esp32-nut, ADR 0008] A request running on another task may still be reading
    // ctrl_xfer: wait for it to release the device before freeing anything.
    bool locked = hid_device->device_busy &&
                  hid_device_try_lock(hid_device, DEFAULT_TIMEOUT_MS + 1000) == ESP_OK;

    // Interfaces left in WAIT_USER_DELETION must not reach the freed device through
    // their parent pointer (class requests check for a NULL parent).
    hid_iface_t *iface = NULL;
    HID_ENTER_CRITICAL();
    STAILQ_FOREACH(iface, &s_hid_driver->hid_ifaces_tailq, tailq_entry) {
        if (iface->parent == hid_device) {
            iface->parent = NULL;
        }
    }
    HID_EXIT_CRITICAL();

    if (hid_device->ctrl_inflight) {
        // The HCD still owns ctrl_xfer and its callback references hid_device:
        // freeing either would be a use-after-free. Leak them (a few hundred bytes).
        ESP_LOGE(TAG, "Control transfer still in flight on device removal, leaking device context");
        HID_ENTER_CRITICAL();
        STAILQ_REMOVE(&s_hid_driver->hid_devices_tailq, hid_device, hid_host_device, tailq_entry);
        HID_EXIT_CRITICAL();
        if (locked) {
            hid_device_unlock(hid_device);
        }
        return ESP_ERR_INVALID_STATE;
    }

    HID_RETURN_ON_ERROR( usb_host_transfer_free(hid_device->ctrl_xfer),
                         "Unable to free transfer buffer for EP0");
    HID_RETURN_ON_ERROR( usb_host_device_close(s_hid_driver->client_handle,
                                               hid_device->dev_hdl),
                         "Unable to close USB host");

    if (hid_device->ctrl_xfer_done) {
        vSemaphoreDelete(hid_device->ctrl_xfer_done);
    }

    if (hid_device->device_busy) {
        vSemaphoreDelete(hid_device->device_busy);
    }

    ESP_LOGD(TAG, "Remove addr %d device from list",
             hid_device->dev_addr);

    HID_ENTER_CRITICAL();
    STAILQ_REMOVE(&s_hid_driver->hid_devices_tailq, hid_device, hid_host_device, tailq_entry);
    HID_EXIT_CRITICAL();

    free(hid_device);
    return ESP_OK;
}

// ----------------------------- Public ----------------------------------------

esp_err_t hid_host_install(const hid_host_driver_config_t *config)
{
    esp_err_t ret;

    HID_RETURN_ON_INVALID_ARG(config);
    HID_RETURN_ON_INVALID_ARG(config->callback);

    if ( config->create_background_task ) {
        HID_RETURN_ON_FALSE(config->stack_size != 0,
                            ESP_ERR_INVALID_ARG,
                            "Wrong stack size value");
        HID_RETURN_ON_FALSE(config->task_priority != 0,
                            ESP_ERR_INVALID_ARG,
                            "Wrong task priority value");
    }

    HID_RETURN_ON_FALSE(!s_hid_driver,
                        ESP_ERR_INVALID_STATE,
                        "HID Host driver is already installed");

    // Create HID driver structure
    hid_driver_t *driver = heap_caps_calloc(1, sizeof(hid_driver_t), MALLOC_CAP_DEFAULT);
    HID_RETURN_ON_FALSE(driver,
                        ESP_ERR_NO_MEM,
                        "Unable to allocate memory");

    driver->user_cb = config->callback;
    driver->user_arg = config->callback_arg;

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .async.client_event_callback = client_event_cb,
        .async.callback_arg = NULL,
        .max_num_event_msg = 10,
    };

    driver->end_client_event_handling = false;
    driver->all_events_handled = xSemaphoreCreateBinary();
    HID_GOTO_ON_FALSE(driver->all_events_handled,
                      ESP_ERR_NO_MEM,
                      "Unable to create semaphore");

    driver->open_close_mutex = xSemaphoreCreateMutexStatic(&s_open_close_mutex_buffer);

    HID_GOTO_ON_ERROR( usb_host_client_register(&client_config,
                                                &driver->client_handle),
                       "Unable to register USB Host client");

    HID_ENTER_CRITICAL();
    HID_GOTO_ON_FALSE_CRITICAL(!s_hid_driver, ESP_ERR_INVALID_STATE);
    s_hid_driver = driver;
    STAILQ_INIT(&s_hid_driver->hid_devices_tailq);
    STAILQ_INIT(&s_hid_driver->hid_ifaces_tailq);
    HID_EXIT_CRITICAL();

    if (config->create_background_task) {
        BaseType_t task_created = xTaskCreatePinnedToCore(
                                      event_handler_task,
                                      "USB HID Host",
                                      config->stack_size,
                                      NULL,
                                      config->task_priority,
                                      NULL,
                                      config->core_id);
        HID_GOTO_ON_FALSE(task_created,
                          ESP_ERR_NO_MEM,
                          "Unable to create USB HID Host task");
    }

    return ESP_OK;

fail:
    s_hid_driver = NULL;
    if (driver->client_handle) {
        usb_host_client_deregister(driver->client_handle);
    }
    if (driver->all_events_handled) {
        vSemaphoreDelete(driver->all_events_handled);
    }
    free(driver);
    return ret;
}

esp_err_t hid_host_uninstall(void)
{
    esp_err_t ret = ESP_OK;

    // Make sure hid driver is installed,
    HID_RETURN_ON_FALSE(s_hid_driver,
                        ESP_OK,
                        "HID Host driver was not installed");

    // Wait for all open/close calls to finish
    SemaphoreHandle_t open_close_mutex = s_hid_driver->open_close_mutex;
    xSemaphoreTake(open_close_mutex, portMAX_DELAY);
    HID_GOTO_ON_FALSE(s_hid_driver, ESP_OK, "HID Driver is not installed"); // Check again after acquiring mutex - driver may have been uninstalled

    // Make sure that hid driver
    // not being uninstalled from other task
    // and no hid device is registered
    HID_ENTER_CRITICAL();
    HID_GOTO_ON_FALSE_CRITICAL(!s_hid_driver->end_client_event_handling, ESP_ERR_INVALID_STATE);
    HID_GOTO_ON_FALSE_CRITICAL(STAILQ_EMPTY(&s_hid_driver->hid_devices_tailq), ESP_ERR_INVALID_STATE);
    HID_GOTO_ON_FALSE_CRITICAL(STAILQ_EMPTY(&s_hid_driver->hid_ifaces_tailq), ESP_ERR_INVALID_STATE);
    s_hid_driver->end_client_event_handling = true;
    HID_EXIT_CRITICAL();

    if (s_hid_driver->event_handling_started) {
        ESP_ERROR_CHECK( usb_host_client_unblock(s_hid_driver->client_handle) );
        // In case the event handling started, we must wait until it finishes
        xSemaphoreTake(s_hid_driver->all_events_handled, portMAX_DELAY);
    }
    ESP_ERROR_CHECK( usb_host_client_deregister(s_hid_driver->client_handle) );

    // Delete semaphores and free driver
    vSemaphoreDelete(s_hid_driver->all_events_handled);
    free(s_hid_driver);
    s_hid_driver = NULL;
    xSemaphoreGive(open_close_mutex); // Unblock any waiting tasks
    return ESP_OK;

fail:
    xSemaphoreGive(open_close_mutex);
    return ret;
}

esp_err_t hid_host_device_open(hid_host_device_handle_t hid_dev_handle,
                               const hid_host_device_config_t *config)
{
    esp_err_t ret;
    HID_RETURN_ON_FALSE(s_hid_driver, ESP_ERR_INVALID_STATE, "HID Driver is not installed");
    SemaphoreHandle_t open_close_mutex = s_hid_driver->open_close_mutex;

    if (xSemaphoreTake(open_close_mutex, pdMS_TO_TICKS(DEFAULT_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timeout waiting for open/close mutex");
        return ESP_ERR_TIMEOUT;
    }
    HID_GOTO_ON_FALSE(s_hid_driver, ESP_ERR_INVALID_STATE, "HID Driver is not installed"); // Check again after acquiring mutex - driver may have been uninstalled

    hid_iface_t *hid_iface = get_iface_by_handle(hid_dev_handle);

    HID_GOTO_ON_FALSE(hid_iface, ESP_ERR_INVALID_ARG, "Invalid HID device handle");

    HID_GOTO_ON_FALSE((hid_iface->dev_params.proto >= HID_PROTOCOL_NONE)
                      && (hid_iface->dev_params.proto < HID_PROTOCOL_MAX),
                      ESP_ERR_INVALID_ARG,
                      "HID device protocol not supported");

    HID_GOTO_ON_FALSE(HID_INTERFACE_STATE_IDLE == hid_iface->state,
                      ESP_ERR_INVALID_STATE,
                      "Interface wrong state");

    // Claim interface, allocate xfer and save report callback
    HID_GOTO_ON_ERROR(hid_host_interface_claim_and_prepare_transfer(hid_iface),
                      "Unable to claim interface");

    // Save HID Interface callback
    hid_iface->user_cb = config->callback;
    hid_iface->user_cb_arg = config->callback_arg;

    xSemaphoreGive(open_close_mutex);
    return ESP_OK;

fail:
    xSemaphoreGive(open_close_mutex);
    return ret;
}

/**
 * @brief Shared implementation of hid_host_device_close().
 *
 * @param[in] hid_dev_handle  HID device handle
 * @param[in] best_effort     When false (graceful close path), disable/release
 *                            failures abort the function with the original
 *                            strict ESP_GOTO_ON_ERROR semantics.
 *                            When true (disconnect cleanup path), those
 *                            failures are logged and the state-transition /
 *                            user-callback / list-removal code below still
 *                            runs, so the interface is reliably removed from
 *                            s_hid_driver->hid_ifaces_tailq before
 *                            hid_host_device_disconnected() frees the owning
 *                            hid_device_t. Without this, a failed close on the
 *                            disconnect path would leave the iface dangling in
 *                            the global list with a stale parent pointer.
 */
static esp_err_t hid_host_device_close_impl(hid_host_device_handle_t hid_dev_handle, bool best_effort)
{
    esp_err_t ret = ESP_OK;
    HID_RETURN_ON_FALSE(s_hid_driver, ESP_ERR_INVALID_STATE, "HID Driver is not installed");
    SemaphoreHandle_t open_close_mutex = s_hid_driver->open_close_mutex;

    if (xSemaphoreTake(open_close_mutex, pdMS_TO_TICKS(DEFAULT_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    HID_GOTO_ON_FALSE(s_hid_driver, ESP_ERR_INVALID_STATE, "HID Driver is not installed"); // Check again after acquiring mutex - driver may have been uninstalled

    hid_iface_t *hid_iface = get_iface_by_handle(hid_dev_handle);
    HID_GOTO_ON_FALSE(hid_iface, ESP_ERR_INVALID_ARG, "Invalid HID device handle");

    ESP_LOGD(TAG, "Close addr %d, iface %d, state %d (best_effort=%d)",
             hid_iface->dev_params.addr,
             hid_iface->dev_params.iface_num,
             hid_iface->state,
             best_effort);

    if (HID_INTERFACE_STATE_ACTIVE == hid_iface->state ||
            HID_INTERFACE_STATE_SUSPENDED == hid_iface->state) {
        esp_err_t disable_err = best_effort
                                ? hid_host_disable_interface_disconnect(hid_iface)
                                : hid_host_disable_interface(hid_iface);
        if (disable_err != ESP_OK) {
            if (best_effort) {
                ESP_LOGW(TAG, "Unable to disable HID Interface on disconnect (continuing): %s",
                         esp_err_to_name(disable_err));
                // Force the state forward so the release/free path below still runs
                // and the iface eventually leaves the global list.
                hid_iface->state = HID_INTERFACE_STATE_READY;
            } else {
                ret = disable_err;
                ESP_LOGE(TAG, "Unable to disable HID Interface");
                goto fail;
            }
        }
    }

    if (HID_INTERFACE_STATE_READY == hid_iface->state) {
        esp_err_t release_err = hid_host_interface_release_and_free_transfer(hid_iface);
        if (release_err != ESP_OK) {
            if (best_effort) {
                ESP_LOGW(TAG, "Unable to release HID Interface on disconnect (continuing): %s",
                         esp_err_to_name(release_err));
                // usb_host_interface_release() failed inside
                // hid_host_interface_release_and_free_transfer(), which causes an
                // early return there before the transfer buffer is freed. The
                // iface struct itself is about to be freed by _hid_host_remove_interface()
                // below, so we'd otherwise leak iface->in_xfer. Free it here so
                // best-effort cleanup matches the leak-free invariant of the
                // strict path.
                if (hid_iface->in_xfer) {
                    esp_err_t xfer_free_err = usb_host_transfer_free(hid_iface->in_xfer);
                    if (xfer_free_err != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to free in_xfer on disconnect: %s",
                                 esp_err_to_name(xfer_free_err));
                    }
                    hid_iface->in_xfer = NULL;
                }
                // Force state transition: release failed but iface must not stay
                // in HID_INTERFACE_STATE_READY, otherwise a user second-close
                // would re-enter this branch.
                hid_iface->state = HID_INTERFACE_STATE_IDLE;
                // Fall through to the state transition / user callback / list
                // removal below so the iface is not orphaned.
            } else {
                ret = release_err;
                ESP_LOGE(TAG, "Unable to release HID Interface");
                goto fail;
            }
        }
        // If the device is closing by user before device detached we need to flush user callback here
        free(hid_iface->report_desc);
        hid_iface->report_desc = NULL;
        hid_iface->report_desc_len = 0;
    }

    if (hid_iface->user_cb && hid_iface->state != HID_INTERFACE_STATE_WAIT_USER_DELETION) {
        // Let user handle the remove process and wait for next hid_host_device_close() call
        hid_iface->state = HID_INTERFACE_STATE_WAIT_USER_DELETION;
        xSemaphoreGive(open_close_mutex); // Give mutex before calling user callback
        hid_host_user_interface_callback(hid_iface, HID_HOST_INTERFACE_EVENT_DISCONNECTED);
    } else {
        // Second call
        hid_iface->user_cb = NULL;
        hid_iface->user_cb_arg = NULL;

        /* Remove Interface from the list */
        ESP_LOGD(TAG, "Remove addr %d, iface %d from list",
                 hid_iface->dev_params.addr,
                 hid_iface->dev_params.iface_num);
        HID_ENTER_CRITICAL();
        _hid_host_remove_interface(hid_iface);
        HID_EXIT_CRITICAL();
        xSemaphoreGive(open_close_mutex);
    }

    return ESP_OK;

fail:
    xSemaphoreGive(open_close_mutex);
    return ret;
}

esp_err_t hid_host_device_close(hid_host_device_handle_t hid_dev_handle)
{
    return hid_host_device_close_impl(hid_dev_handle, /*best_effort=*/false);
}

static esp_err_t hid_host_device_close_disconnect(hid_host_device_handle_t hid_dev_handle)
{
    return hid_host_device_close_impl(hid_dev_handle, /*best_effort=*/true);
}

esp_err_t hid_host_handle_events(uint32_t timeout)
{
    HID_RETURN_ON_FALSE(s_hid_driver != NULL,
                        ESP_ERR_INVALID_STATE,
                        "HID Driver is not installed");

    ESP_LOGD(TAG, "USB HID handling");
    s_hid_driver->event_handling_started = true;
    esp_err_t ret = usb_host_client_handle_events(s_hid_driver->client_handle, timeout);
    if (s_hid_driver->end_client_event_handling) {
        xSemaphoreGive(s_hid_driver->all_events_handled);
        return ESP_FAIL;
    }
    return ret;
}

esp_err_t hid_host_device_get_params(hid_host_device_handle_t hid_dev_handle,
                                     hid_host_dev_params_t *dev_params)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_FALSE(iface,
                        ESP_ERR_INVALID_STATE,
                        "HID Interface not found");

    HID_RETURN_ON_FALSE(dev_params,
                        ESP_ERR_INVALID_ARG,
                        "Wrong argument");

    memcpy(dev_params, &iface->dev_params, sizeof(hid_host_dev_params_t));
    return ESP_OK;
}

esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t hid_dev_handle,
                                                    uint8_t *data,
                                                    size_t data_length_max,
                                                    size_t *data_length)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_FALSE(iface,
                        ESP_ERR_INVALID_STATE,
                        "HID Interface not found");

    HID_RETURN_ON_FALSE(data,
                        ESP_ERR_INVALID_ARG,
                        "Wrong argument");

    HID_RETURN_ON_FALSE(data_length,
                        ESP_ERR_INVALID_ARG,
                        "Wrong argument");

    size_t copied = (data_length_max >= iface->in_xfer->actual_num_bytes)
                    ? iface->in_xfer->actual_num_bytes
                    : data_length_max;
    memcpy(data, iface->in_xfer->data_buffer, copied);
    *data_length = copied;
    return ESP_OK;
}

#ifdef HID_HOST_REMOTE_WAKE_SUPPORTED

esp_err_t hid_host_enable_remote_wakeup(hid_host_device_handle_t hid_dev_handle, bool enable)
{
    HID_RETURN_ON_FALSE(s_hid_driver, ESP_ERR_INVALID_STATE, "HID Driver is not installed");
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);
    HID_RETURN_ON_FALSE(is_interface_in_list(iface), ESP_ERR_NOT_FOUND, "Interface handle not found");
    hid_device_t *hid_device = iface->parent;

    // Get device's config descriptor
    const usb_config_desc_t *config_desc;
    ESP_RETURN_ON_ERROR(
        usb_host_get_active_config_descriptor(hid_device->dev_hdl, &config_desc), TAG, "Unable to get configuration descriptor");

    // Check if the device reports remote wakeup feature in it's configuration descriptor
    ESP_RETURN_ON_FALSE(
        (config_desc->bmAttributes & USB_BM_ATTRIBUTES_WAKEUP), ESP_ERR_NOT_SUPPORTED, TAG, "Device does not support remote wakeup");

    HID_RETURN_ON_ERROR( hid_device_lock_ctrl(hid_device, DEFAULT_TIMEOUT_MS), "Control pipe not available");

    // Check current remote wakeup status
    // If user wants to enable it and is already enabled (or vice versa) return early, otherwise proceed to ctrl transfer
    if (hid_device->remote_wakeup_enabled == enable) {
        ESP_LOGD(TAG, "Remote wakeup already %s on this device", (enable) ? "enabled" : "disabled");
        hid_device_unlock(hid_device);
        return ESP_OK;
    }

    usb_transfer_t *ctrl_xfer = hid_device->ctrl_xfer;
    HID_RETURN_ON_INVALID_ARG(ctrl_xfer);

    usb_setup_packet_t *setup = (usb_setup_packet_t *)ctrl_xfer->data_buffer;
    if (enable) {
        // Enable remote wakeup
        USB_SETUP_PACKET_INIT_SET_FEATURE(setup, DEVICE_REMOTE_WAKEUP);
        ESP_LOGI(TAG, "Enabling remote wakeup on device");
    } else {
        // Disable remote wakeup
        USB_SETUP_PACKET_INIT_CLEAR_FEATURE(setup, DEVICE_REMOTE_WAKEUP);
        ESP_LOGI(TAG, "Disabling remote wakeup on device");
    }

    esp_err_t ret = hid_control_transfer(hid_device,
                                         USB_SETUP_PACKET_SIZE + setup->wLength,
                                         DEFAULT_TIMEOUT_MS);

    // CTRL transfer passed, update device status about remote wakeup
    if (ret == ESP_OK) {
        hid_device->remote_wakeup_enabled = enable;
    }

    hid_device_unlock(hid_device);
    return ret;
}

#endif // HID_HOST_REMOTE_WAKE_SUPPORTED

// ------------------------ USB HID Host driver API ----------------------------

esp_err_t hid_host_device_start(hid_host_device_handle_t hid_dev_handle)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->in_xfer);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface),
                        ESP_ERR_NOT_FOUND,
                        "Interface handle not found");

    HID_RETURN_ON_FALSE ((HID_INTERFACE_STATE_READY == iface->state || HID_INTERFACE_STATE_SUSPENDED == iface->state),
                         ESP_ERR_INVALID_STATE,
                         "Interface wrong state");

    // prepare transfer
    iface->in_xfer->device_handle = iface->parent->dev_hdl;
    iface->in_xfer->callback = in_xfer_done;
    iface->in_xfer->context = iface;
    iface->in_xfer->timeout_ms = DEFAULT_TIMEOUT_MS;
    iface->in_xfer->bEndpointAddress = iface->ep_in;
    iface->in_xfer->num_bytes = iface->ep_in_mps;

    iface->state = HID_INTERFACE_STATE_ACTIVE;

    // start data transfer
    esp_err_t ret = usb_host_transfer_submit(iface->in_xfer);
    if (ret != ESP_OK) {
        // [esp32-nut, ADR 0008] Keep the interface restartable: after a stop, the flushed
        // IN transfer stays in flight until the client task processes its callback, so the
        // caller retries hid_host_device_start() and needs the READY state for that.
        iface->state = HID_INTERFACE_STATE_READY;
    }
    return ret;
}

esp_err_t hid_host_device_stop(hid_host_device_handle_t hid_dev_handle)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);

    HID_RETURN_ON_FALSE(is_interface_in_list(iface), ESP_ERR_NOT_FOUND, "Interface handle not found");

    if (iface->state == HID_INTERFACE_STATE_SUSPENDED) {
        // If interface is suspended, mark the last state as READY,
        // as if the interface was stopped before entering suspended state
        iface->last_state = HID_INTERFACE_STATE_READY;
        return ESP_OK;
    }

    return hid_host_disable_interface(iface);
}

esp_err_t hid_host_device_clear_ep_in_halt(hid_host_device_handle_t hid_dev_handle)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);
    HID_RETURN_ON_FALSE(is_interface_in_list(iface), ESP_ERR_NOT_FOUND, "Interface handle not found");

    hid_device_t *hid_device = iface->parent;
    HID_RETURN_ON_INVALID_ARG(hid_device->ctrl_xfer);

    // [esp32-nut, review A3] Same guards as the class requests (ADR 0008)
    HID_RETURN_ON_ERROR( hid_device_lock_ctrl(hid_device, DEFAULT_TIMEOUT_MS),
                         "Control pipe not available");

    usb_setup_packet_t *setup = (usb_setup_packet_t *)hid_device->ctrl_xfer->data_buffer;
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                           USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                           USB_BM_REQUEST_TYPE_RECIP_ENDPOINT;
    setup->bRequest = USB_B_REQUEST_CLEAR_FEATURE;
    setup->wValue = 0; // ENDPOINT_HALT feature selector
    setup->wIndex = iface->ep_in;
    setup->wLength = 0;

    esp_err_t ret = hid_control_transfer(hid_device, USB_SETUP_PACKET_SIZE, CTRL_REQUEST_TIMEOUT_MS);

    hid_device_unlock(hid_device);
    return ret;
}

uint16_t hid_host_device_get_ep_in_mps(hid_host_device_handle_t hid_dev_handle)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);
    return iface ? iface->ep_in_mps : 0;
}

esp_err_t hid_host_device_get_string_descriptor(hid_host_device_handle_t hid_dev_handle,
                                                uint8_t index, uint16_t lang_id,
                                                uint8_t *data, size_t data_length_max,
                                                size_t *data_length)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);
    HID_RETURN_ON_INVALID_ARG(data);
    HID_RETURN_ON_INVALID_ARG(data_length);
    HID_RETURN_ON_FALSE(is_interface_in_list(iface), ESP_ERR_NOT_FOUND, "Interface handle not found");

    // [esp32-nut, review M5] String descriptors requested by the drivers (battery type,
    // battery date...): the USB Host library only fetches manufacturer, product and serial.
    const hid_class_request_t get_desc = {
        .bRequest = USB_B_REQUEST_GET_DESCRIPTOR,
        .wValue = (uint16_t)((USB_B_DESCRIPTOR_TYPE_STRING << 8) | index),
        .wIndex = lang_id,
        .wLength = (uint16_t)(data_length_max > 255 ? 255 : data_length_max),
        .data = data
    };
    *data_length = 0;
    return usb_class_request_get_descriptor(iface->parent, &get_desc, data_length,
                                            USB_BM_REQUEST_TYPE_RECIP_DEVICE);
}

esp_err_t hid_host_device_get_string_indices(hid_host_device_handle_t hid_dev_handle,
                                             uint8_t *manufacturer, uint8_t *product, uint8_t *serial)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);
    HID_RETURN_ON_FALSE(is_interface_in_list(iface), ESP_ERR_NOT_FOUND, "Interface handle not found");

    const usb_device_desc_t *desc;
    HID_RETURN_ON_ERROR( usb_host_get_device_descriptor(iface->parent->dev_hdl, &desc),
                         "Unable to get device descriptor");
    if (manufacturer) *manufacturer = desc->iManufacturer;
    if (product) *product = desc->iProduct;
    if (serial) *serial = desc->iSerialNumber;
    return ESP_OK;
}

uint8_t *hid_host_get_report_descriptor(hid_host_device_handle_t hid_dev_handle,
                                        size_t *report_desc_len)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    if (NULL == iface) {
        return NULL;
    }

    // Report Descriptor was already requested, return pointer
    // [esp32-nut, review C3] Length received, not the one declared in the HID descriptor
    if (iface->report_desc) {
        *report_desc_len = iface->report_desc_len;
        return iface->report_desc;
    }

    // Request Report Descriptor
    if (ESP_OK == hid_class_request_report_descriptor(iface)) {
        *report_desc_len = iface->report_desc_len;
        return iface->report_desc;
    }

    return NULL;
}

esp_err_t hid_host_get_device_info(hid_host_device_handle_t hid_dev_handle,
                                   hid_host_dev_info_t *hid_dev_info)
{
    HID_RETURN_ON_INVALID_ARG(hid_dev_info);

    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);
    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(iface->parent);

    hid_device_t *hid_dev = iface->parent;

    // Fill descriptor device information
    const usb_device_desc_t *desc;
    usb_device_info_t dev_info;
    HID_RETURN_ON_ERROR( usb_host_get_device_descriptor(hid_dev->dev_hdl, &desc),
                         "Unable to get device descriptor");
    HID_RETURN_ON_ERROR( usb_host_device_info(hid_dev->dev_hdl, &dev_info),
                         "Unable to get USB device info");
    // VID, PID
    hid_dev_info->VID = desc->idVendor;
    hid_dev_info->PID = desc->idProduct;
    // Strings
    hid_host_string_descriptor_copy(hid_dev_info->iManufacturer,
                                    dev_info.str_desc_manufacturer);
    hid_host_string_descriptor_copy(hid_dev_info->iProduct,
                                    dev_info.str_desc_product);
    hid_host_string_descriptor_copy(hid_dev_info->iSerialNumber,
                                    dev_info.str_desc_serial_num);
    return ESP_OK;
}

esp_err_t hid_class_request_get_report(hid_host_device_handle_t hid_dev_handle,
                                       uint8_t report_type,
                                       uint8_t report_id,
                                       uint8_t *report,
                                       size_t *report_length)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(report);

    const hid_class_request_t get_report = {
        .bRequest = HID_CLASS_SPECIFIC_REQ_GET_REPORT,
        .wValue = (report_type << 8) | report_id,
        .wIndex = iface->dev_params.iface_num,
        .wLength = *report_length,
        .data = report
    };

    return hid_class_request_get(iface->parent, &get_report, report_length);
}

esp_err_t hid_class_request_get_idle(hid_host_device_handle_t hid_dev_handle,
                                     uint8_t report_id,
                                     uint8_t *idle_rate)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(idle_rate);

    uint8_t tmp[1] = { 0xff };

    const hid_class_request_t get_idle = {
        .bRequest = HID_CLASS_SPECIFIC_REQ_GET_IDLE,
        .wValue = report_id,
        .wIndex = iface->dev_params.iface_num,
        .wLength = 1,
        .data = tmp
    };

    HID_RETURN_ON_ERROR( hid_class_request_get(iface->parent, &get_idle, NULL),
                         "HID class request transfer failure");

    *idle_rate = tmp[0];

    return ESP_OK;
}

esp_err_t hid_class_request_get_protocol(hid_host_device_handle_t hid_dev_handle,
                                         hid_report_protocol_t *protocol)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);
    HID_RETURN_ON_INVALID_ARG(protocol);

    uint8_t tmp[1] = { 0xff };

    const hid_class_request_t get_proto = {
        .bRequest = HID_CLASS_SPECIFIC_REQ_GET_PROTOCOL,
        .wValue = 0,
        .wIndex = iface->dev_params.iface_num,
        .wLength = 1,
        .data = tmp
    };

    HID_RETURN_ON_ERROR( hid_class_request_get(iface->parent, &get_proto, NULL),
                         "HID class request failure");

    *protocol = (hid_report_protocol_t) tmp[0];
    return ESP_OK;
}

esp_err_t hid_class_request_set_report(hid_host_device_handle_t hid_dev_handle,
                                       uint8_t report_type,
                                       uint8_t report_id,
                                       uint8_t *report,
                                       size_t report_length)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);

    const hid_class_request_t set_report = {
        .bRequest = HID_CLASS_SPECIFIC_REQ_SET_REPORT,
        .wValue = (report_type << 8) | report_id,
        .wIndex = iface->dev_params.iface_num,
        .wLength = report_length,
        .data = report
    };

    return hid_class_request_set(iface->parent, &set_report);
}

esp_err_t hid_class_request_set_idle(hid_host_device_handle_t hid_dev_handle,
                                     uint8_t duration,
                                     uint8_t report_id)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);

    const hid_class_request_t set_idle = {
        .bRequest = HID_CLASS_SPECIFIC_REQ_SET_IDLE,
        .wValue = (duration << 8) | report_id,
        .wIndex = iface->dev_params.iface_num,
        .wLength = 0,
        .data = NULL
    };

    return hid_class_request_set(iface->parent, &set_idle);
}

esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t hid_dev_handle,
                                         hid_report_protocol_t protocol)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);

    HID_RETURN_ON_INVALID_ARG(iface);

    const hid_class_request_t set_proto = {
        .bRequest = HID_CLASS_SPECIFIC_REQ_SET_PROTOCOL,
        .wValue = protocol,
        .wIndex = iface->dev_params.iface_num,
        .wLength = 0,
        .data = NULL
    };

    return hid_class_request_set(iface->parent, &set_proto);
}

usb_device_handle_t hid_host_get_device_handle(hid_host_device_handle_t hid_dev_handle)
{
    hid_iface_t *iface = get_iface_by_handle(hid_dev_handle);
    if (!iface || !iface->parent) return NULL;
    return iface->parent->dev_hdl;
}

