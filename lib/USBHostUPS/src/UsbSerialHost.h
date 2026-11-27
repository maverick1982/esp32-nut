#ifndef USB_SERIAL_HOST_H
#define USB_SERIAL_HOST_H

#include <atomic>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "UsbDiagSnapshot.h"
#include "UsbSerialLink.h"

/**
 * @brief The serial family on the USB host (ADR 0014): usb_host_cdc_acm next to hid_host.
 *
 * The callbacks of the CDC-ACM driver run in its task and only copy and post (ADR 0008):
 * the new-device callback copies the classification data into a UsbSerialDeviceInfo record
 * (its device handle is valid only during the callback), the event callback posts the driver
 * event, the data callback counts bytes. The poll task drains the queue in service(): it
 * classifies the device with DriverRegistry::matchSerial(), opens it by address and keeps
 * the result in a UsbSerialLink. No protocol yet: nothing is sent, no line settings are
 * applied and no recovery is asked for (ADR 0014, sections 4 and 5).
 *
 * A single instance: the new-device callback of usb_host_cdc_acm has no user argument.
 */
class UsbSerialHost {
public:
    using LogFn = void (*)(void* ctx, const char* level, const char* text);

    UsbSerialHost();
    ~UsbSerialHost();

    void setLog(LogFn fn, void* ctx) {
        _log = fn;
        _log_ctx = ctx;
    }
    // After usb_host_install(), from the task that installs hid_host. True when the driver is
    // installed (also when it already was)
    bool begin();
    // Closes the open device and uninstalls the driver
    void end();

    // Poll task. True when a new device record arrived since the last call (the enumeration
    // watchdog counts it as an enumeration, as the HID CONNECTED event)
    bool service(uint32_t now);

    // Any task: the "Not supported yet" text (empty with no serial device) and the export
    void unsupportedText(char* out, size_t out_size) const;
    void fillDiagnostics(UsbDiagSerial& d) const;

private:
    struct Event {
        enum Type : uint8_t { NEW_DEVICE, DEVICE_EVENT };
        Type type;
        UsbSerialDeviceInfo info;            // NEW_DEVICE
        cdc_acm_host_dev_event_data_t data;  // DEVICE_EVENT
    };
    static const UBaseType_t QUEUE_LEN = 4;
    static const uint32_t OPEN_TIMEOUT_MS = 1000;
    static const uint32_t PRESENCE_CHECK_MS = 1000;

    static void onNewDevice(usb_device_handle_t dev);
    static void onDeviceEvent(const cdc_acm_host_dev_event_data_t* event, void* user_ctx);
    static bool onData(const uint8_t* data, size_t len, void* user_ctx);

    void handleNewDevice(const UsbSerialDeviceInfo& info, uint32_t now);
    void handleDeviceEvent(const cdc_acm_host_dev_event_data_t& data);
    void closeDevice();
    void checkPresence(uint32_t now);
    void log(const char* level, const char* fmt, ...) const;

    static UsbSerialHost* s_instance;

    LogFn _log = nullptr;
    void* _log_ctx = nullptr;
    QueueHandle_t _queue = nullptr;
    bool _installed = false;
    cdc_acm_dev_hdl_t _cdc = nullptr;  // poll task only
    uint32_t _presence_at = 0;
    std::atomic<uint32_t> _rx_bytes{0};  // data callback -> poll task
    std::atomic<uint32_t> _rx_chunks{0};
    std::atomic<uint32_t> _dropped{0};   // new-device records or events the full queue refused
    uint32_t _reported_dropped = 0;
    mutable std::mutex _mutex;  // guards _link; never held across a USB call
    UsbSerialLink _link;
};

#endif // USB_SERIAL_HOST_H
