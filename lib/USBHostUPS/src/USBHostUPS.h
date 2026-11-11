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
#include "DiagSweep.h"
#include "EnumWatchdog.h"
#include "PortCyclePolicy.h"
#include "DriverRegistry.h"
#include "IQxUsbPrimitives.h"
#include "IQxHostPort.h"
#include "EventBacklog.h"
#include "QxRequestBox.h"
#include "QxProtocols.h"
#include <atomic>
#include <map>
#include <memory>
#include <vector>

struct CachedReport {
    uint8_t report_id;
    uint8_t report_type;
    std::vector<uint8_t> data;
};

typedef void (*LogCallback)(const char* level, const char* msg);

class IUPSDriver;
class QxUsbTransport;

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
 * - The on-demand read of the diagnostics export (US-119) runs in the poll task too:
 *   requestDiagRead() only raises a flag, service() then asks one report per iteration,
 *   requestSpacingMs() after any other control request, without decoding it.
 * - Q* path (US-136, ADR 0013 section 3): the Q* driver sends its commands from the poll
 *   task through the primitives of _qx_usb, with no application lock held. An interrupt IN
 *   answer is read from _event_queue; the other events read meanwhile wait in
 *   _event_backlog and are handled first by the next processEvents(). Commands and SET VAR
 *   of the other tasks go through a one-slot request served by the poll task (never under
 *   _op_mutex, which the poll task holds during service()).
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
    // Asks the poll task to read once the FEATURE reports the export would miss (US-119).
    // False without a UPS ready; a read already running is not started again.
    bool requestDiagRead();
    // State and counters of that read; Reading while the request waits for the poll task
    DiagSweep::Status diagReadStatus() const;

    bool setBeeper(bool enable) override;
    bool isConnected() const override;
    bool isDataStale() const override;
    // "Q* UPS (USB transport: hunnox)" while a Q* UPS is bound (US-132), empty otherwise
    String unsupportedDevice() const override;
    bool supportsBeeperToggle() const override;
    bool shutdownCommandsSupported() const override;
    bool beeperMuteSupported() const override;
    const NutTable::HidInfo* commandTable(size_t& n) const override;
    bool beeperFieldRead() const override;
    bool setBeeperField(const HIDUsageDef& field, bool enable) override;

    void setLogCallback(LogCallback cb);
    // USB peripheral the UPS is attached to, from the board profile (US-090); call before
    // begin(). 0 is the ESP-IDF default, the only one on the ESP32-S3
    void setPeripheralMap(uint32_t map) { _peripheral_map = map; }
    // Consecutive controlled restarts before this boot (CrashDiag, RTC memory); call before
    // begin(). Above 0, a UPS that does not enumerate gets its port power-cycled (US-121)
    void setAutomaticRestartCount(uint8_t count) { _auto_restart_count = count; }
    // protocol and novendor of the Q* driver from Board Settings (US-138), any task: protocol
    // "" for the automatic detection. Applied by the poll task to the bound Q* driver (the
    // detection starts again, no restart of the board) and to every later Q* attach
    void setQxSettings(const char* protocol, bool novendor);
    // True while a Q* driver is bound (Board Settings shows its section); detected is the
    // claimed protocol ("Megatec 0.10"), empty before a claim
    bool qxDriverInfo(String& detected) const;
    void logDebug(const String& msg) const override;

    // Set when in-place recovery failed: the owner must restart the system (never from an ISR)
    bool isRestartRequested() const { return _restart_requested; }
    // The UPS answered GET_REPORT and a full poll ended since it was claimed, with the
    // control pipe healthy: a controlled restart before it worked (RestartPolicy, issue #60)
    bool isControlPipeProven() const;
    // Claimed, first full poll still running: stale for NUT, but not a link problem
    bool isWaitingFirstData() const override { return _is_ready_to_poll && !_first_data_ready; }
    const char* getRestartReason() const { return _restart_reason; }
    // Restart reason of the enumeration watchdog (US-121): kept in RTC memory (64 bytes)
    static constexpr const char* NOT_ENUMERATED_REASON = "UPS not enumerated after the automatic restart";
    // The pending restart request comes from the enumeration watchdog: degraded mode then
    // means the UPS never came back, and only replugging the cable helps (US-121)
    bool isNotEnumeratedRestart() const {
        return _restart_requested && _restart_reason == NOT_ENUMERATED_REASON;
    }

    // USB port power cycles since the boot that recovered a stuck control transfer (US-122)
    uint32_t getPortCyclesSinceBoot() const { return _port_cycles.cyclesSinceBoot(); }

    // Free stack of the poll task in bytes (diagnostics)
    uint32_t getPollTaskStackHighWater() const;

    const std::vector<HIDUsageDef>& getUsages() const override { return _hid_parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _hid_parser.getUsageDef(usage); }
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    String getActiveBeeperPath() const override;
    std::vector<NutTable::RWVariable> getWritableVariables() const override;
    bool writeUsage(const HIDUsageDef& def, uint32_t value) override;
    // Control of the attached driver (US-105), HidControl when there is none
    const IUpsControl& activeControl() const override;
    const UPSData& upsData() const override { return _ups_data; }
    CommandResult executeCommand(const char* name, const char* param = nullptr) override;
    NutTable::SetStatus setVariable(const char* name, const char* value) override;
    // The neutral methods of IUpsControlPort for the Q* driver (US-135, US-136): the list
    // under _mutex, the command and the SET VAR run by the poll task while the caller waits
    std::vector<const UPSCommandInfo*> driverCommands() const override;
    CommandResult runDriverCommand(const char* name, const char* param) override;
    NutTable::SetStatus runDriverSetVariable(const char* name, const char* value) override;
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
    // Steps of a SET VAR run by the driver's control (IUpsControlPort/IHidControlPort, US-105).
    // Private: reachable only through the port, after the checks of setVariable/resetVariable
    NutTable::SetStatus planSet(const char* name, const char* value, NutTable::SetPlan& plan) const override;
    NutTable::SetStatus saveDriverSideValue(const NutTable::SetPlan& plan) override;
    NutTable::SetStatus writePlannedValue(const NutTable::SetPlan& plan) override;
    NutTable::SetStatus resetDriverSideValue(const char* name, String& value) override;

    // ups.delay.* defaults of the driver (NutTable::applyDefaultDelays, US-058)
    void applyDefaultDelays();
    // Saved driver-side values other than ups.delay.* (outlet.*.desc), after each _driver->setup()
    void applySavedDriverValues();

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

    struct HidEventTraits {
        using Event = HidEvent;
        using Handle = hid_host_device_handle_t;
        static bool isInput(const Event& e) { return e.type == HidEvent::INPUT_REPORT; }
        static bool isDisconnect(const Event& e) { return e.type == HidEvent::DISCONNECTED; }
        static bool isTransferError(const Event& e) { return e.type == HidEvent::TRANSFER_ERROR; }
        static Handle handle(const Event& e) { return e.handle; }
        static const uint8_t* data(const Event& e) { return e.data; }
        static size_t length(const Event& e) { return e.length; }
    };
    // Events read while a Q* command waited for an interrupt IN chunk (US-136)
    static const size_t EVENT_BACKLOG_LEN = 8;

    // The host as the Q* driver and its transport see it (US-136): the USB primitives on the
    // bound interface and the data lock with the log. Poll task only for the primitives.
    class QxUsbPort : public IQxUsbPrimitives, public IQxHostPort {
    public:
        explicit QxUsbPort(USBHostUPS& host) : _h(host) {}
        int setReport(uint8_t reportType, uint8_t reportId, const uint8_t* data, size_t len) override;
        int getStringDescriptor(uint8_t index, uint16_t langid, uint8_t* buf, size_t buflen) override;
        int readInterrupt(uint8_t* buf, size_t buflen, uint32_t timeoutMs) override;
        void discardInput() override;
        void setDeadline(uint32_t deadlineMs) override { _deadline = deadlineMs; }
        void sleepMs(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
        uint32_t nowMs() const override { return millis(); }
        const char* lastError() const override { return _error; }
        void lock() const override { _h._mutex.lock(); }
        void unlock() const override { _h._mutex.unlock(); }
        void log(const char* level, const char* text) const override { _h.log(level, "%s", text); }
        bool pollingPaused() const override { return _h.isPollingPaused(); }
    private:
        // QX_USB_ERR_* when no control transfer may start now (no device, link back-off,
        // budget over), 0 otherwise
        int refuse(uint32_t now);
        int result(esp_err_t err);
        USBHostUPS& _h;
        uint32_t _deadline = 0;
        const char* _error = "";
    };
    friend class QxUsbPort;

    // The Q* request box as the host backs it (US-137): its locks and semaphores, and the
    // attached Q* driver. run*() in the poll task only.
    class QxRequestPort : public IQxRequestSync, public IQxRequestTarget {
    public:
        explicit QxRequestPort(USBHostUPS& host) : _h(host) {}
        void lock() override { _h._mutex.lock(); }
        void unlock() override { _h._mutex.unlock(); }
        bool ready() const override { return _h._qx_req_mutex && _h._qx_req_done; }
        bool takeCaller(uint32_t ms) override;
        void giveCaller() override { xSemaphoreGive(_h._qx_req_mutex); }
        bool waitDone(uint32_t ms) override;
        void signalDone() override {
            if (_h._qx_req_done) xSemaphoreGive(_h._qx_req_done);
        }
        void drainDone() override { xSemaphoreTake(_h._qx_req_done, 0); }
        bool inPollTask() const override { return xTaskGetCurrentTaskHandle() == _h._poll_task_handle; }
        bool hasDriver() const override;
        bool readyToPoll() const override { return _h._is_ready_to_poll; }
        uint32_t waitMs() const override;
        CommandResult runCommand(const char* name, const char* param) override;
        NutTable::SetStatus runSetVariable(const char* name, const char* value) override;
    private:
        USBHostUPS& _h;
    };
    friend class QxRequestPort;
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
    // One event of the queue or of the backlog (the body of processEvents())
    void handleEvent(const HidEvent& ev);
    // Serves a pending Q* request: true when it ran on the driver (the poll step is skipped).
    // can_run false answers it without any transfer.
    bool serviceQxRequest(bool can_run);
    // Answers a pending Q* request with result (no driver, link not usable). gen != 0 answers
    // only that request: one posted after the caller abandoned it keeps waiting for its turn.
    void answerQxRequest(CommandResult cmd, NutTable::SetStatus set, uint32_t gen = 0);
    // Posts a request and waits for the poll task; false when it timed out
    bool postQxRequest(QxRequest& req);
    void claimInterface(hid_host_device_handle_t handle);
    // nut_usb_get_string() on a device being claimed (issue #91): LANGID from string
    // descriptor 0 (0x0409 when broken), then the string, 3 tries 50 ms apart each.
    // Blocking control transfers, no application lock held. False with out = "" on failure.
    // note, when given and still empty, gets the LANGID used and the bytes of string
    // descriptor 0 ("LANGID 0x0409, string 0: 04 03 09 04") for the System Log (issue #91).
    bool readNutString(hid_host_device_handle_t handle, uint8_t index, char* out, size_t outlen,
                       char* note = nullptr, size_t notelen = 0);
    // Q* path of claimInterface() (US-132, ADR 0013 section 2): keeps interface 0, logs
    // that the UPS is recognised but not supported yet. No IN pipe, cache or diagnostics.
    // vid, pid, quirks and product go to the USB diagnostics export, so a report names the
    // device that is bound instead of the previous one
    void claimQxInterface(hid_host_device_handle_t handle, const DriverRegistry::Match& match,
                          uint16_t vid, uint16_t pid, uint32_t quirks, const char* product);
    // claimQxInterface() with a ported transport (US-136): the Q* driver on interface 0
    void attachQxDriver(hid_host_device_handle_t handle, const DriverRegistry::Match& match,
                        std::unique_ptr<QxUsbTransport> transport, uint16_t vid, uint16_t pid,
                        uint32_t quirks, const char* product);
    // Drops the state of the bound device, HID or Q* (body of handleDisconnected())
    void releaseBoundState();
    bool deferClaim(hid_host_device_handle_t handle, uint32_t now);
    void clearDeferredClaim(hid_host_device_handle_t handle);
    void processInputReport(const HidEvent& ev);
    void handleDisconnected(hid_host_device_handle_t handle);
    void logOnlineDischarge(uint32_t now);
    void closeInterface(hid_host_device_handle_t handle);
    void noteControlResult(esp_err_t err, uint32_t now);
    // On-demand read of the export (US-119): start at the flag, then one step
    void serviceDiagRead();
    // Stops a running read with the results so far (the link no longer allows requests)
    void interruptDiagRead();
    void logDiagReadEnd();
    // GET_REPORT of the on-demand read: no decoding, no cache, no ERROR log. The bytes stay
    // in _request_buffer, length is their count (0 unless Read).
    DiagSweep::Result readReportRaw(uint8_t report_id, uint8_t report_type, uint16_t expected_length, size_t& length);
    void recoverInterface(const char* why, uint32_t now, bool clear_in_halt = false);
    void retryInterfaceStart(uint32_t now);
    void requestRestart(const char* why);
    void serviceEnumWatchdog(uint32_t now);
    void cycleRootPort();
    // LinkMonitor asked for a restart with a control transfer stuck in the stack (US-122)
    void recoverStuckTransfer(uint32_t now);
    // After a port cycle: true when the poll must not go on (US-122)
    bool servicePortCycleWait(uint32_t now);
    void logStats(uint32_t now);
    // _op_mutex held; field: the beeper usage to write, nullptr = the active beeper (US-074)
    bool setBeeperLocked(bool enable, const HIDUsageDef* field = nullptr);
    bool writeUsageLocked(const HIDUsageDef& def, uint32_t value); // _op_mutex held
    void log(const char* level, const char* fmt, ...) const;

    QueueHandle_t _event_queue;
    // Events read during a Q* command, handled first by processEvents() (poll task only)
    EventBacklog<HidEvent, EVENT_BACKLOG_LEN> _event_backlog;
    QxUsbPort _qx_usb{*this};
    // The IN pipe is started and recovered for this interface: always for HID, for Q* only
    // when the transport reads interrupt IN (US-136)
    bool _in_pipe_wanted = true;
    // Q* request of another task (US-136): the box state under _mutex, callers serialised by
    // _qx_req_mutex, the poll task gives _qx_req_done when it answered (QxRequestBox, US-137)
    SemaphoreHandle_t _qx_req_mutex = NULL;
    SemaphoreHandle_t _qx_req_done = NULL;
    QxRequestPort _qx_req_port{*this};
    QxRequestBox _qx_box{_qx_req_port};
    uint32_t _stat_qx_ok = 0;     // Q* transfers of the traffic summary
    uint32_t _stat_qx_failed = 0;
    // Board Settings of the Q* driver (US-138), under _mutex: setQxSettings() bumps the
    // generation, the poll task applies it to the bound driver (applyQxSettings())
    char _qx_protocol[QX_PROTOCOL_NAME_MAX] = "";
    bool _qx_novendor = false;
    uint32_t _qx_settings_gen = 0;
    uint32_t _qx_settings_applied = 0;
    // Poll task: a new generation of the settings reaches the Q* driver, with no lock held
    // during QxDriver::configure() (ADR 0008)
    void applyQxSettings();
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
    // Q* UPS bound but not supported yet (US-132): NUT USB subdriver name and the text of
    // unsupportedDevice(); nullptr / empty on the HID path and without a device
    const char* _qx_transport = nullptr;
    String _unsupported_device;
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
    uint32_t _stat_diag_empty = 0; // empty answers of the export read (US-119)
    uint32_t _stat_port_cycles = 0; // USB port power cycles for a stuck transfer (US-122)
    bool _in_restart_pending;
    uint8_t _in_start_attempts;
    uint32_t _in_restart_at;
    uint8_t _in_recoveries;
    bool _restart_requested;
    const char* _restart_reason;

    // UPS not enumerated after an automatic restart (US-121): poll task only
    EnumWatchdog _enum_wd;
    uint8_t _auto_restart_count = 0;
    bool _enum_seen = false;  // a HID device connected (or failed to open) since the boot

    // Stuck control transfer recovered with a USB port power cycle (US-122): poll task only
    PortCyclePolicy _port_cycles;
#if defined(NUT_TEST_STUCK_CYCLE) && NUT_TEST_STUCK_CYCLE
    static const uint32_t TEST_STUCK_EVERY_MS = 90000;
    uint32_t _test_stuck_at = 0;    // last time the pipe was not proven, or a cycle waited
    bool _test_stuck_active = false; // simulated stuck transfer, until the cycle starts
#endif

    UPSData _ups_data;
    IUPSDriver* _driver;
    LogCallback _log_cb;
    IDriverVarStore* _var_store;
    // Cache of what _var_store holds, under _mutex (US-067)
    std::vector<std::pair<String, String>> _saved_vars;

    std::map<uint16_t, CachedReport> _cached_reports;

    // On-demand read of the export (US-119): written by the poll task under _mutex
    DiagSweep _diag;
    std::atomic<bool> _diag_requested{false};
    uint32_t _last_ctrl_at = 0;   // end of the last control request (noteControlResult)
    uint32_t _diag_last_at = 0;   // end of the last request of the on-demand read

    uint32_t _quirks;
    uint32_t _peripheral_map = 0;
    uint8_t _request_buffer[256];
};

#endif // USB_HOST_UPS_H
