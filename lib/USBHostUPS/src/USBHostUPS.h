#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <mutex>
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "Quirks.h"
#include "HIDParser.h"
#include "UsbDiagSnapshot.h"
#include "UPSData.h"
#include "IUSBHostUPS.h"
#include "LinkMonitor.h"
#include "InputReassembler.h"
#include "DescriptorRetry.h"
#include "OnlineDischargeLog.h"
#include "DriverVarStore.h"
#include <map>
#include <vector>

struct CachedReport {
    uint8_t report_id;
    uint8_t report_type;
    std::vector<uint8_t> data;
};

typedef void (*LogCallback)(const char* level, const char* msg);

class IUPSDriver;

/**
 * Threading model (issue #47, ADR 0008; review A5b):
 * - The HID host background task only runs the hid_host callbacks. They never block:
 *   they copy what they need into _event_queue and return, so the same task can keep
 *   delivering control transfer completions.
 * - Everything else (descriptor parsing, GET/SET_REPORT, decoding, UPSData updates,
 *   recovery) runs in the dedicated "ups_poll" task started by begin(), above the
 *   loopTask and below WiFi. The loopTask only serves NUT and the web UI, so a UPS that
 *   does not answer no longer blocks them.
 * - No application lock is held during a blocking control transfer. _mutex only
 *   protects UPSData, the parser and the report cache against the readers.
 * - _op_mutex serialises the control requests of the other tasks (setBeeper) with a
 *   poll iteration. Lock order: _op_mutex, then _mutex.
 */
class USBHostUPS : public IUSBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    // Installs the USB host and starts the poll task
    bool begin();
    void end();

    void lock() const override { _mutex.lock(); }
    void unlock() const override { _mutex.unlock(); }

    UPSDataLock getUPSData() const override;
    String getUPSStatusString() const override;
    // Copies the data of the USB diagnostics export (/api/usb/dump) under _mutex. The web
    // server writes the JSON from it outside the lock, in chunks (US-095, US-096)
    void snapshotUSBDiagnostics(UsbDiagSnapshot& s);

    bool setBeeper(bool enable) override;
    bool isConnected() const override;
    bool isDataStale() const override;
    bool supportsBeeperToggle() const override;
    bool shutdownCommandsSupported() const override;
    bool beeperMuteSupported() const override;
    const char* const* commandAllowList() const override;

    void setLogCallback(LogCallback cb);
    // USB peripheral the UPS is attached to, from the board profile (US-090); call before
    // begin(). 0 is the ESP-IDF default, the only one on the ESP32-S3
    void setPeripheralMap(uint32_t map) { _peripheral_map = map; }
    void logDebug(const String& msg) const override;

    // Set when in-place recovery failed: the owner must restart the system (never from an ISR)
    bool isRestartRequested() const { return _restart_requested; }
    // The UPS answered GET_REPORT and a full poll ended since it was claimed, with the
    // control pipe healthy: a controlled restart before it worked (RestartPolicy, issue #60)
    bool isControlPipeProven() const;
    // Claimed, first full poll still running: stale for NUT, but not a link problem
    bool isWaitingFirstData() const { return _is_ready_to_poll && !_first_data_ready; }
    const char* getRestartReason() const { return _restart_reason; }

    // Free stack of the poll task in bytes (diagnostics)
    uint32_t getPollTaskStackHighWater() const;

    const std::vector<HIDUsageDef>& getUsages() const override { return _hid_parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _hid_parser.getUsageDef(usage); }
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    String getActiveBeeperPath() const override;
    std::vector<const UPSCommandInfo*> getSupportedCommands() const override;
    std::vector<NutTable::RWVariable> getWritableVariables() const override;
    bool writeUsage(const HIDUsageDef& def, uint32_t value) override;
    CommandResult executeCommand(const char* name, const char* param = nullptr) override;
    NutTable::SetStatus setVariable(const char* name, const char* value) override;
    NutTable::SetStatus resetVariable(const char* name) override;
    // Store of the driver-side SET VAR values (US-067): loaded once, call it before begin()
    void setDriverVarStore(IDriverVarStore* store);
    uint32_t getQuirks() const override { return _quirks; }
    bool isPollingPaused() const override;
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) override;
    bool requestStringDescriptor(uint8_t string_index) override;
    uint32_t inputReportAgeMs(uint8_t report_id, uint32_t now) const override;
    uint16_t getVID() const override { return _vid; }
    uint16_t getPID() const override { return _pid; }

    static void populateStringsFromDeviceInfo(const hid_host_dev_info_t& dev_info, uint32_t quirks, UPSData& ups_data);
private:
    // ups.delay.* defaults of the driver (CommandCatalog::applyDefaultDelays, US-058)
    void applyDefaultDelays();
    // Saved driver-side values other than ups.delay.* (outlet.*.desc), after each _driver->setup()
    void applySavedDriverValues();
    // setVariable() without the log line
    NutTable::SetStatus runSetVariable(const char* name, const char* value);
    // resetVariable() without the log line; value is the default now in force
    NutTable::SetStatus runResetVariable(const char* name, String& value);

    struct HidEvent {
        enum Type : uint8_t { CONNECTED, OPEN_FAILED, INPUT_REPORT, DISCONNECTED, TRANSFER_ERROR };
        Type type;
        uint16_t length;
        uint32_t ts; // millis() when the HID task received it
        hid_host_device_handle_t handle;
        uint8_t data[64]; // Full-speed interrupt IN max packet size
    };
    // A blocking GET_REPORT keeps the poll task away for up to its 1.5 s timeout: an APC
    // Back-UPS BX streams ~18 INPUT reports/s, so 16 slots dropped some (issue #60)
    static const UBaseType_t EVENT_QUEUE_LEN = 48;
    // Slots kept free for connect/disconnect/error events: INPUT reports are dropped first
    static const UBaseType_t EVENT_QUEUE_RESERVED = 2;
    static const uint8_t MAX_IN_RECOVERIES = 3;
    static const uint8_t MAX_IN_START_ATTEMPTS = 40;
    // Longest wait for the first full poll after a claim: past it the data is served
    // anyway, and LinkMonitor judges the link (a UPS without status usages never ends it)
    static const uint32_t FIRST_DATA_MAX_MS = 30000;
    static const uint32_t STATS_PERIOD_MS = 60000;
    static const uint32_t POLL_TASK_STACK = 8192;
    static const UBaseType_t POLL_TASK_PRIORITY = 3; // HID task 5, usb_host_events 2, loopTask 1
    static const uint32_t POLL_TASK_PERIOD_MS = 10;
    // Longest wait of setBeeper() for a poll step. The longest step is a string request:
    // language ID + string, 2 x USBUPS_CTRL_TIMEOUT_MS (1.5 s) when the UPS does not answer.
    static const uint32_t OP_WAIT_MS = 4000;

    mutable std::recursive_mutex _mutex;
    HIDParser _hid_parser;
    static void hid_host_driver_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);
    static void hid_host_interface_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg);
    static void usb_host_lib_task(void *arg);
    TaskHandle_t _usb_task_handle;
    volatile bool _usb_task_run;
    TaskHandle_t _poll_task_handle;
    volatile bool _poll_task_run;
    SemaphoreHandle_t _op_mutex;

    // HID task context: must not block
    void handle_driver_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event);
    void handle_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event);
    void postEvent(const HidEvent& ev, bool reserved_slot);

    // Poll task context
    static void poll_task(void *arg);
    void service();
    void processEvents();
    void claimInterface(hid_host_device_handle_t handle);
    bool deferClaim(hid_host_device_handle_t handle, uint32_t now);
    void clearDeferredClaim(hid_host_device_handle_t handle);
    void processInputReport(const HidEvent& ev);
    void handleDisconnected(hid_host_device_handle_t handle);
    void logOnlineDischarge(uint32_t now);
    void closeInterface(hid_host_device_handle_t handle);
    void noteControlResult(esp_err_t err, uint32_t now);
    void recoverInterface(const char* why, uint32_t now, bool clear_in_halt = false);
    void retryInterfaceStart(uint32_t now);
    void requestRestart(const char* why);
    void logStats(uint32_t now);
    bool setBeeperLocked(bool enable); // _op_mutex held
    bool writeUsageLocked(const HIDUsageDef& def, uint32_t value); // _op_mutex held
    void log(const char* level, const char* fmt, ...) const;

    QueueHandle_t _event_queue;
    // Interface being closed by the loopTask: its synchronous DISCONNECTED is not queued
    volatile hid_host_device_handle_t _self_close_handle;
    volatile uint32_t _dropped_events;
    uint32_t _reported_dropped_events;

    hid_host_device_handle_t _hid_dev_handle;
    // Interface opened but without a report descriptor yet: claimInterface() runs again (issue #97)
    hid_host_device_handle_t _desc_retry_handle;
    DescriptorRetry _desc_retry;
    // Throttle of the OL+DISCHRG warning, as usbhid-ups (issue #98)
    OnlineDischargeLog _ol_dischrg_log;
    uint32_t _ol_dischrg_checked_at = 0;

    String _cached_report_descriptor_hex;
    // Product string as the device sent it, before formatDeviceStrings() splits it
    // (APC "FW:"): the dump must carry it, or a replay cannot rebuild ups.firmware
    String _device_product;
    uint16_t _vid;
    uint16_t _pid;
    bool _initialized;
    bool _is_ready_to_poll;
    volatile bool _first_data_ready; // first full poll ended (or FIRST_DATA_MAX_MS) since the claim
    uint32_t _claimed_at;
    uint32_t _get_ok_since_claim;

    LinkMonitor _link;
    InputWatchdog _in_wd;
    InputReassembler _in_reasm;
    uint16_t _ep_in_mps;
    bool _uses_report_ids;
    uint16_t _lang_id;             // of the string descriptors, 0 = not read yet
    uint32_t _failed_strings[8];   // bitmap of the string indices that failed (256)
    // When each INPUT report ID was last received (HID task time), poll task only (issue #60)
    uint32_t _input_ts[256];
    uint32_t _input_seen[8];       // bitmap: _input_ts valid

    // Traffic summary logged every STATS_PERIOD_MS (review S1)
    uint32_t _stat_since;
    uint32_t _stat_input;       // complete INPUT reports
    uint32_t _stat_in_packets;  // INPUT events from the HID task, before reassembly
    uint32_t _stat_get_ok;
    uint32_t _stat_get_failed;
    bool _in_restart_pending;
    uint8_t _in_start_attempts;
    uint32_t _in_restart_at;
    uint8_t _in_recoveries;
    bool _restart_requested;
    const char* _restart_reason;

    UPSData _ups_data;
    IUPSDriver* _driver;
    LogCallback _log_cb;
    IDriverVarStore* _var_store;
    // Cache of what _var_store holds, under _mutex (US-067)
    std::vector<std::pair<String, String>> _saved_vars;

    std::map<uint16_t, CachedReport> _cached_reports;

    uint32_t _quirks;
    uint32_t _peripheral_map = 0;
    uint8_t _request_buffer[256];
};

#endif // USB_HOST_UPS_H
