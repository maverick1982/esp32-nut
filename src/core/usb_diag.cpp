// USB attach diagnostics (-DNUT_USB_DIAG=1): a second USB Host Library client that
// logs every device the host enumerates, whatever its class, to the Web UI System Log.
// "no attach at all" points to wiring/port/power, "attach but no HID interface"
// or "attach but no 'HID Device Connected event'" points to the driver.
#if NUT_USB_DIAG
#include <Arduino.h>
#include "usb/usb_host.h"
#include "core/app_logger.h"

static usb_host_client_handle_t s_client = nullptr;
static volatile uint32_t s_attach_count = 0;

static const char* speedName(usb_speed_t s) {
    switch (s) {
        case USB_SPEED_LOW: return "low";
        case USB_SPEED_FULL: return "full";
#ifdef USB_SPEED_HIGH
        case USB_SPEED_HIGH: return "high";
#endif
        default: return "?";
    }
}

static void clientEvent(const usb_host_client_event_msg_t* msg, void*) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s_attach_count = s_attach_count + 1;
        uint8_t addr = msg->new_dev.address;
        usb_device_handle_t dev;
        esp_err_t err = usb_host_device_open(s_client, addr, &dev);
        if (err != ESP_OK) {
            AppLogger::log("WARN", "[USBDIAG] Device attached at address %u, open failed: %s",
                           (unsigned)addr, esp_err_to_name(err));
            return;
        }
        usb_device_info_t info = {};
        const usb_device_desc_t* dd = nullptr;
        const usb_config_desc_t* cd = nullptr;
        usb_host_device_info(dev, &info);
        usb_host_get_device_descriptor(dev, &dd);
        usb_host_get_active_config_descriptor(dev, &cd);
        AppLogger::log("INFO", "[USBDIAG] Device attached: addr %u, %s speed, VID %04X PID %04X, class %02X, MPS0 %u",
                       (unsigned)addr, speedName(info.speed), dd ? dd->idVendor : 0, dd ? dd->idProduct : 0,
                       dd ? dd->bDeviceClass : 0, (unsigned)info.bMaxPacketSize0);
        if (cd) {
            // Walk the configuration descriptor and list the interfaces
            const uint8_t* p = (const uint8_t*)cd;
            int off = 0;
            while (off + 2 <= cd->wTotalLength) {
                uint8_t len = p[off], type = p[off + 1];
                if (len == 0) break;
                if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && off + 9 <= cd->wTotalLength) {
                    AppLogger::log("INFO", "[USBDIAG]   interface %u: class %02X subclass %02X protocol %02X, %u endpoint(s)",
                                   p[off + 2], p[off + 5], p[off + 6], p[off + 7], p[off + 4]);
                }
                off += len;
            }
        }
        usb_host_device_close(s_client, dev);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        AppLogger::log("INFO", "[USBDIAG] Device detached");
    }
}

static void diagTask(void*) {
    const usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {.client_event_callback = clientEvent, .callback_arg = nullptr},
    };
    esp_err_t err = usb_host_client_register(&cfg, &s_client);
    if (err != ESP_OK) {
        AppLogger::log("ERROR", "[USBDIAG] Client registration failed: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    AppLogger::log("INFO", "[USBDIAG] Watching the USB host port for attached devices");
    uint32_t last_report = millis();
    while (true) {
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(1000));
        // Once a minute while nothing has enumerated: the port has seen no device
        if (s_attach_count == 0 && millis() - last_report >= 60000) {
            last_report = millis();
            AppLogger::log("WARN", "[USBDIAG] No USB device has enumerated since boot");
        }
    }
}

void usbDiagBegin() {
    xTaskCreatePinnedToCore(diagTask, "usb_diag", 4096, nullptr, 2, nullptr, tskNO_AFFINITY);
}
#endif
