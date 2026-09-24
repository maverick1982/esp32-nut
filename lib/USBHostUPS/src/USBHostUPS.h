#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <mutex>
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "Quirks.h"
#include "HIDParser.h"
#include "UPSData.h"
#include "IUSBHostUPS.h"
#include "LinkMonitor.h"
#include "InputReassembler.h"
#include <map>
#include <vector>

// Time after boot during which a missing UPS is not reported as stale (review A1)
#ifndef USBUPS_NO_DEVICE_BOOT_GRACE_MS
#define USBUPS_NO_DEVICE_BOOT_GRACE_MS 15000
#endif

struct CachedReport {
    uint8_t report_id;
    uint8_t report_type;
    std::vector<uint8_t> data;
};

typedef void (*LogCallback)(const char* level, const char* msg);

class IUPSDriver;

/**
 * Threading model (issue #47, ADR 0008):
 * - The HID host background task only runs the hid_host callbacks. They never block:
 *   they copy what they need into _event_queue and return, so the same task can keep
 *   delivering control transfer completions.
 * - Everything else (descriptor parsing, GET/SET_REPORT, decoding, UPSData updates,
 *   recovery) runs in the caller of loop(), i.e. the Arduino loopTask.
 * - No application lock is held during a blocking control transfer. _mutex only
 *   protects UPSData and the report cache against readers.
 */
class USBHostUPS : public IUSBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    bool begin();
    void end();
    void loop();

    void lock() const override { _mutex.lock(); }
    void unlock() const override { _mutex.unlock(); }

    UPSDataLock getUPSData() const override;
    String getUPSStatusString() const override;
    String dumpUSBDiagnostics();

    bool setBeeper(bool enable) override;
    bool isConnected() const override;
    bool isDataStale() const override;
    bool supportsBeeperToggle() const override;

    void setLogCallback(LogCallback cb);
    void logDebug(const String& msg) const override;

    // Set when in-place recovery failed: the owner must restart the system (never from an ISR)
    bool isRestartRequested() const { return _restart_requested; }
    const char* getRestartReason() const { return _restart_reason; }

    const std::vector<HIDUsageDef>& getUsages() const override { return _hid_parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _hid_parser.getUsageDef(usage); }
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    String getActiveBeeperPath() const override;
    uint32_t getQuirks() const override { return _quirks; }
    bool isControlPending() const override;
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) override;
    bool requestStringDescriptor(uint8_t string_index) override;
    uint16_t getVID() const override { return _vid; }
    uint16_t getPID() const override { return _pid; }

    HIDParser _hid_parser;

public:
    static void populateStringsFromDeviceInfo(const hid_host_dev_info_t& dev_info, uint32_t quirks, UPSData& ups_data);
private:
    struct HidEvent {
        enum Type : uint8_t { CONNECTED, OPEN_FAILED, INPUT_REPORT, DISCONNECTED, TRANSFER_ERROR };
        Type type;
        uint16_t length;
        uint32_t ts; // millis() when the HID task received it
        hid_host_device_handle_t handle;
        uint8_t data[64]; // Full-speed interrupt IN max packet size
    };
    static const UBaseType_t EVENT_QUEUE_LEN = 16;
    // Slots kept free for connect/disconnect/error events: INPUT reports are dropped first
    static const UBaseType_t EVENT_QUEUE_RESERVED = 2;
    static const uint8_t MAX_IN_RECOVERIES = 3;
    static const uint8_t MAX_IN_START_ATTEMPTS = 40;
    static const uint32_t NO_DEVICE_BOOT_GRACE_MS = USBUPS_NO_DEVICE_BOOT_GRACE_MS;

    mutable std::recursive_mutex _mutex;
    static void hid_host_driver_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);
    static void hid_host_interface_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg);
    static void usb_host_lib_task(void *arg);
    TaskHandle_t _usb_task_handle;
    volatile bool _usb_task_run;

    // HID task context: must not block
    void handle_driver_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event);
    void handle_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event);
    void postEvent(const HidEvent& ev, bool reserved_slot);

    // loopTask context
    void processEvents();
    void claimInterface(hid_host_device_handle_t handle);
    void processInputReport(const HidEvent& ev);
    void handleDisconnected(hid_host_device_handle_t handle);
    void closeInterface(hid_host_device_handle_t handle);
    void noteControlResult(esp_err_t err, uint32_t now);
    void recoverInterface(const char* why, uint32_t now, bool clear_in_halt = false);
    void retryInterfaceStart(uint32_t now);
    void requestRestart(const char* why);
    void log(const char* level, const char* fmt, ...) const;

    QueueHandle_t _event_queue;
    // Interface being closed by the loopTask: its synchronous DISCONNECTED is not queued
    volatile hid_host_device_handle_t _self_close_handle;
    volatile uint32_t _dropped_events;
    uint32_t _reported_dropped_events;

    hid_host_device_handle_t _hid_dev_handle;

    String _cached_report_descriptor_hex;
    uint16_t _vid;
    uint16_t _pid;
    bool _initialized;
    bool _is_ready_to_poll;
    bool _device_seen; // a UPS was claimed since boot

    LinkMonitor _link;
    InputWatchdog _in_wd;
    InputReassembler _in_reasm;
    uint16_t _ep_in_mps;
    bool _uses_report_ids;
    bool _in_restart_pending;
    uint8_t _in_start_attempts;
    uint32_t _in_restart_at;
    uint8_t _in_recoveries;
    bool _restart_requested;
    const char* _restart_reason;

    UPSData _ups_data;
    IUPSDriver* _driver;
    LogCallback _log_cb;

    std::map<uint16_t, CachedReport> _cached_reports;

    uint32_t _quirks;
    uint8_t _request_buffer[256];
};

#endif // USB_HOST_UPS_H
