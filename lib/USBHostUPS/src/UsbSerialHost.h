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
#include "ISerialPort.h"
#include "SerialRxBuffer.h"

/**
 * @brief The serial family on the USB host (ADR 0014): usb_host_cdc_acm next to hid_host.
 *
 * The callbacks of the CDC-ACM driver run in its task and only copy and post (ADR 0008):
 * the new-device callback copies the classification data into a UsbSerialDeviceInfo record
 * (its device handle is valid only during the callback), the event callback posts the driver
 * event, the data callback stores the bytes in a lock-free buffer. The poll task drains the
 * queue in service(): it classifies the device with DriverRegistry::matchSerial(), opens it
 * by address and keeps the result in a UsbSerialLink. The opened device is offered as an
 * ISerialPort (port()); the host attaches a protocol to it when portGeneration() changes and
 * portReady() says so (ADR 0014, section 6). No recovery is asked for.
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

    // Poll task (ADR 0014, section 6): the port of the opened device, for the protocol. The
    // generation changes on every open and close; portReady() is true while the device is open
    ISerialPort& port() { return _port; }
    uint32_t portGeneration() const { return _port_gen; }
    bool portReady() const { return _cdc != nullptr; }
    // VID, PID and product of the device behind the port
    const UsbSerialDeviceInfo& portDevice() const { return _port_device; }

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

    // ISerialPort on the opened device (ADR 0014, section 4): the line settings with the
    // standard CDC-ACM requests, the bridge specifics of its adapter (XR21V141x: no flow
    // control, XrUartSetup.h), writes through the CDC-ACM driver, reads from the buffer
    class Port : public ISerialPort {
    public:
        explicit Port(UsbSerialHost& h) : _h(h) {}
        bool configure(const SerialLineSettings& settings) override;
        int write(const uint8_t* data, size_t len, uint32_t timeoutMs) override;
        int read(uint8_t* buf, size_t len, uint32_t timeoutMs) override;
        size_t discardInput() override;
        void sleepMs(uint32_t ms) override;
        uint32_t nowMs() const override;
        const char* lastError() const override { return _error; }
    private:
        void setError(const char* what, int err);
        // XR21V141x: the registers after the setup, in the System Log (XR_GETN_REG)
        void logXrRegisters();
        bool readXrRegister(uint16_t wIndex, uint8_t& value);
#if defined(NUT_TEST_XR_LOOPBACK) && NUT_TEST_XR_LOOPBACK
        // Test build: the internal TX to RX loopback of the chip, to tell a UART that does not
        // send from a UPS that does not answer
        void xrLoopbackTest();
#endif
        UsbSerialHost& _h;
        char _error[48] = "";
    };
    friend class Port;
    static const size_t RX_BUFFER_LEN = 1024;
    // OUT transfer size: the max packet size of the bulk OUT endpoint of a full-speed bridge
    static const size_t OUT_BUFFER_LEN = 64;

    LogFn _log = nullptr;
    void* _log_ctx = nullptr;
    QueueHandle_t _queue = nullptr;
    bool _installed = false;
    cdc_acm_dev_hdl_t _cdc = nullptr;  // poll task only
    uint32_t _presence_at = 0;
    std::atomic<uint32_t> _rx_bytes{0};  // data callback -> poll task
    std::atomic<uint32_t> _rx_chunks{0};
    std::atomic<uint32_t> _tx_bytes{0};
    std::atomic<bool> _gone{false};      // the open device disconnected: reads end at once
    SerialRxBuffer<RX_BUFFER_LEN> _rx;
    Port _port{*this};
    uint32_t _port_gen = 0;
    UsbSerialDeviceInfo _port_device;
    std::atomic<uint32_t> _dropped{0};   // new-device records or events the full queue refused
    uint32_t _reported_dropped = 0;
    mutable std::mutex _mutex;  // guards _link; never held across a USB call
    UsbSerialLink _link;
};

#endif // USB_SERIAL_HOST_H
