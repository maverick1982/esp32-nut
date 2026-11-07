#include "BeeperLogic.h"
#include "DeviceStrings.h"
#include "USBHostUPS.h"
#include "IUPSDriver.h"
#include "DriverDispatch.h"
#include "DriverRegistry.h"
#include "FirstData.h"
#include "QxDriver.h"
#include "QxUsbTransports.h"
#include <stdarg.h>
#include <math.h>
#include "esp_task_wdt.h"

// Beeper field setBeeper() writes: the path getActiveBeeperPath() picks (first
// AudibleAlarmControl in descriptor order), in its Feature or Output item. Some devices
// (APC Smart-UPS, #78) declare the same path as Input first: writing that definition
// would use the Input layout. The Input item is returned only when the path has no writable one.
static const HIDUsageDef* activeBeeper(const std::vector<HIDUsageDef>& usages) {
    const HIDUsageDef* first = nullptr;
    for (const auto& u : usages) {
        if (strcmp(u.path, "UPS.PowerSummary.AudibleAlarmControl") == 0 ||
            strcmp(u.path, "UPS.BatterySystem.Battery.AudibleAlarmControl") == 0 ||
            strcmp(u.path, "UPS.AudibleAlarmControl") == 0) {
            first = &u;
            break;
        }
    }
    if (!first) return nullptr;
    for (const auto& u : usages) {
        if (strcmp(u.path, first->path) == 0 && (u.report_type == 0x03 || u.report_type == 0x02)) return &u;
    }
    return first;
}

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

USBHostUPS::USBHostUPS() :
    _usb_task_handle(NULL), _usb_task_run(false),
    _poll_task_handle(NULL), _poll_task_run(false), _op_mutex(NULL),
    _event_queue(NULL), _self_close_handle(NULL), _dropped_events(0), _reported_dropped_events(0),
    _hid_dev_handle(NULL), _desc_retry_handle(NULL),
    _vid(0), _pid(0),
    _initialized(false), _is_ready_to_poll(false),
    _first_data_ready(false), _claimed_at(0), _get_ok_since_claim(0),
    _ep_in_mps(0), _uses_report_ids(false), _lang_id(0), _failed_strings{},
    _input_ts{}, _input_seen{},
    _stat_since(0), _stat_input(0), _stat_in_packets(0), _stat_get_ok(0), _stat_get_failed(0),
    _in_restart_pending(false), _in_start_attempts(0), _in_restart_at(0), _in_recoveries(0),
    _restart_requested(false), _restart_reason(""),
    _driver(nullptr), _log_cb(nullptr), _var_store(nullptr), _quirks(0)
{
}

USBHostUPS::~USBHostUPS() {
    end();
}

bool USBHostUPS::begin() {
    if (_initialized) return true;

    if (!_event_queue) {
        _event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(HidEvent));
        if (!_event_queue) {
            log("ERROR", "HID event queue allocation failed");
            return false;
        }
    }
    if (!_op_mutex) {
        _op_mutex = xSemaphoreCreateMutex();
        if (!_op_mutex) {
            log("ERROR", "USB operation mutex allocation failed");
            return false;
        }
    }
    if (!_qx_req_mutex) _qx_req_mutex = xSemaphoreCreateMutex();
    if (!_qx_req_done) _qx_req_done = xSemaphoreCreateBinary();
    if (!_qx_req_mutex || !_qx_req_done) {
        log("ERROR", "Q* request semaphores allocation failed");
        return false;
    }

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    host_config.peripheral_map = _peripheral_map;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    // Arduino core 3.3.11+ (ESP-IDF 5.5.5) enables the enumeration filter, and the
    // stack then needs a callback: with NULL no device enumerates ("Root port reset
    // failed", arduino-esp32 issue #12783). Accept every device, keep its configuration
    host_config.enum_filter_cb = [](const usb_device_desc_t*, uint8_t*) { return true; };
#endif
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log("ERROR", "usb_host_install failed");
        return false;
    }

    if (!_usb_task_handle) {
        _usb_task_run = true;
        xTaskCreatePinnedToCore(
            USBHostUPS::usb_host_lib_task,
            "usb_host_events",
            4096,
            this,
            2,
            &_usb_task_handle,
            tskNO_AFFINITY
        );
    }

    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 8192,
        .core_id = tskNO_AFFINITY,
        .callback = USBHostUPS::hid_host_driver_event_cb,
        .callback_arg = this
    };

    err = hid_host_install(&hid_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log("ERROR", "hid_host_install failed");
        return false;
    }

    // A UPS that does not enumerate gets its root port power-cycled (ESP-IDF does not retry a
    // cancelled enumeration). After a controlled restart a new restart follows (US-121); after
    // any other boot (power on, OTA, settings saved) only the two cycles (US-139, #78).
    // Armed before the poll task starts: the task reads the watchdog without a lock.
#if defined(NUT_TEST_ENUM_WATCHDOG) && NUT_TEST_ENUM_WATCHDOG
    if (_auto_restart_count == 0 && !_poll_task_handle) {
#if NUT_TEST_ENUM_WATCHDOG == 2
        log("WARN", "[TEST] Enumeration watchdog of a normal boot, enumerations ignored "
                    "(NUT_TEST_ENUM_WATCHDOG=2)");
        _enum_wd.arm(millis(), EnumWatchdog::Mode::AFTER_NORMAL_BOOT);
#else
        log("WARN", "[TEST] Enumeration watchdog of an automatic restart armed on a normal boot, "
                    "enumerations ignored (NUT_TEST_ENUM_WATCHDOG=1)");
        _enum_wd.arm(millis(), EnumWatchdog::Mode::AFTER_AUTO_RESTART);
#endif
    }
#endif
    if (!_enum_wd.armed() && !_poll_task_handle) {
        if (_auto_restart_count > 0) {
            log("INFO", "[USB] Automatic restart #%u: waiting for the UPS to enumerate",
                (unsigned)_auto_restart_count);
        }
        _enum_wd.arm(millis(), EnumWatchdog::modeFor(_auto_restart_count));
    }

    if (!_poll_task_handle) {
        _poll_task_run = true;
        if (xTaskCreatePinnedToCore(USBHostUPS::poll_task, "ups_poll", POLL_TASK_STACK, this,
                                    POLL_TASK_PRIORITY, &_poll_task_handle, tskNO_AFFINITY) != pdPASS) {
            _poll_task_run = false;
            _poll_task_handle = NULL;
            log("ERROR", "USB poll task creation failed");
            return false;
        }
    }

    _initialized = true;
    return true;
}

void USBHostUPS::poll_task(void *arg) {
    USBHostUPS *self = static_cast<USBHostUPS*>(arg);
    // Under the task watchdog like the loopTask: a hang becomes a panic with a backtrace.
    // An iteration blocks at most for a few control transfer timeouts.
    bool wdt = esp_task_wdt_add(NULL) == ESP_OK;
    if (!wdt) self->log("WARN", "[USB] Poll task not under the task watchdog");
#if defined(NUT_TEST_TWDT_HANG) && NUT_TEST_TWDT_HANG
    // US-110 test build only (PLATFORMIO_BUILD_FLAGS=-DNUT_TEST_TWDT_HANG=1): 60 s after
    // the start the task stops feeding the watchdog, without holding _op_mutex
    const TickType_t hang_start = xTaskGetTickCount();
#endif

    while (self->_poll_task_run) {
        if (wdt) esp_task_wdt_reset();
#if defined(NUT_TEST_TWDT_HANG) && NUT_TEST_TWDT_HANG
        if (xTaskGetTickCount() - hang_start >= pdMS_TO_TICKS(60000)) {
            self->log("WARN", "[TEST] Poll task stops feeding the task watchdog (NUT_TEST_TWDT_HANG)");
            while (true) vTaskDelay(pdMS_TO_TICKS(1000));
        }
#endif
        if (xSemaphoreTake(self->_op_mutex, portMAX_DELAY) == pdTRUE) {
            self->service();
            xSemaphoreGive(self->_op_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_TASK_PERIOD_MS));
    }

    if (wdt) esp_task_wdt_delete(NULL);
    self->_poll_task_handle = NULL;
    vTaskDelete(NULL);
}

uint32_t USBHostUPS::getPollTaskStackHighWater() const {
    TaskHandle_t h = _poll_task_handle;
    return h ? (uint32_t)uxTaskGetStackHighWaterMark(h) : 0;
}

void USBHostUPS::end() {
    if (!_initialized) return;

    if (_poll_task_handle) {
        _poll_task_run = false;
        for (int i = 0; i < 400 && _poll_task_handle; i++) vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (_hid_dev_handle) {
        closeInterface(_hid_dev_handle);
        _hid_dev_handle = NULL;
    }
    if (_desc_retry_handle) {
        closeInterface(_desc_retry_handle);
        clearDeferredClaim(_desc_retry_handle);
    }

    hid_host_uninstall();

    if (_usb_task_handle) {
        _usb_task_run = false;
        // The task will exit on next wakeup, or we can just let it clean up on shutdown if possible.
        // Espressif's usb_host_lib requires all clients to be deregistered before it can be uninstalled.
        // Actually, we don't strictly need to kill it unless we want to totally uninstall usb_host.
        _usb_task_handle = NULL;
    }
    _initialized = false;
}

void USBHostUPS::log(const char* level, const char* fmt, ...) const {
    if (!_log_cb) return;
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log_cb(level, buf);
}

// ---------------------------------------------------------------------------
// HID host task context. Nothing here may block or take _mutex: while this task
// is stuck it cannot deliver the completion of the control transfer the loopTask
// is waiting for (the deadlock behind the control transfer timeouts of issue #47).
// ---------------------------------------------------------------------------

void USBHostUPS::hid_host_driver_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    USBHostUPS* ups = static_cast<USBHostUPS*>(arg);
    ups->handle_driver_event(hid_device_handle, event);
}

void USBHostUPS::handle_driver_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event) {
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        const hid_host_device_config_t dev_config = {
            .callback = USBHostUPS::hid_host_interface_event_cb,
            .callback_arg = this
        };

        HidEvent ev = {};
        ev.handle = hid_device_handle;
        ev.type = (hid_host_device_open(hid_device_handle, &dev_config) == ESP_OK)
                  ? HidEvent::CONNECTED : HidEvent::OPEN_FAILED;
        postEvent(ev, true);
    }
}

void USBHostUPS::hid_host_interface_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    USBHostUPS* ups = static_cast<USBHostUPS*>(arg);
    ups->handle_interface_event(hid_device_handle, event);
}

void USBHostUPS::handle_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event) {
    HidEvent ev = {};
    ev.handle = hid_device_handle;
    ev.ts = millis();

    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        size_t length = 0;
        if (hid_host_device_get_raw_input_report_data(hid_device_handle, ev.data, sizeof(ev.data), &length) != ESP_OK) return;
        ev.type = HidEvent::INPUT_REPORT;
        ev.length = (uint16_t)length;
        postEvent(ev, false);
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        if (hid_device_handle == _self_close_handle) return; // closeInterface() finishes the removal
        ev.type = HidEvent::DISCONNECTED;
        postEvent(ev, true);
    } else if (event == HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR) {
        ev.type = HidEvent::TRANSFER_ERROR;
        postEvent(ev, true);
    }
}

void USBHostUPS::postEvent(const HidEvent& ev, bool reserved_slot) {
    if (!_event_queue) return;
    // INPUT reports come only from the HID task. A DISCONNECTED raised synchronously by a
    // hid_host_device_close() in the loopTask may take a slot concurrently: that only eats
    // into the reserve, which is sized for it.
    if (!reserved_slot && uxQueueSpacesAvailable(_event_queue) <= EVENT_QUEUE_RESERVED) {
        _dropped_events = _dropped_events + 1;
        return;
    }
    if (xQueueSend(_event_queue, &ev, 0) != pdTRUE) {
        _dropped_events = _dropped_events + 1;
    }
}

// ---------------------------------------------------------------------------
// Poll task context (review A5b)
// ---------------------------------------------------------------------------

void USBHostUPS::service() {
    processEvents();

    uint32_t now = millis();
    if (_desc_retry.due(now)) {
        claimInterface(_desc_retry_handle);
        now = millis(); // the read can block up to its 5 s timeout
    }

    serviceEnumWatchdog(now);

    // The export read stops when the recovery takes over the link (US-119, ADR 0008)
    if (_restart_requested || _in_restart_pending) interruptDiagRead();

    // A Q* request of another task is answered at once, without transfers, when the poll
    // cannot run (US-136)
    if (_restart_requested) {
        serviceQxRequest(false);
        return;
    }

    if (_in_restart_pending) {
        serviceQxRequest(false);
        retryInterfaceStart(now);
        return;
    }

    if (servicePortCycleWait(now)) {
        serviceQxRequest(false);
        return;
    }

    if (!_is_ready_to_poll || !_driver) {
        serviceQxRequest(false);
        return;
    }

#if defined(NUT_TEST_STUCK_CYCLE) && NUT_TEST_STUCK_CYCLE
    // Test build (US-122): TEST_STUCK_EVERY_MS after the pipe is proven, act as if a control
    // transfer were stuck. No request goes out meanwhile, so nothing clears the failure.
    if (!_test_stuck_active) {
        if (!isControlPipeProven() || _port_cycles.waiting()) {
            _test_stuck_at = now;
        } else if (now - _test_stuck_at >= TEST_STUCK_EVERY_MS) {
            log("WARN", "[TEST] Simulated stuck control transfer (NUT_TEST_STUCK_CYCLE)");
            _test_stuck_active = true;
            _link.onLinkFailure(now);
        }
    }
#endif

    // Under _op_mutex: no request of ours is in progress, so in flight means stuck
    bool ctrl_stuck = _link.failures() > 0 && hid_host_device_ctrl_stuck(_hid_dev_handle);
#if defined(NUT_TEST_STUCK_CYCLE) && NUT_TEST_STUCK_CYCLE
    if (_test_stuck_active) ctrl_stuck = true;
#endif
    switch (_link.tick(now, ctrl_stuck)) {
    case LinkMonitor::Action::RECOVER:
        serviceQxRequest(false);
        recoverInterface("control pipe not answering", now);
        return;
    case LinkMonitor::Action::RESTART:
        serviceQxRequest(false);
        if (ctrl_stuck) {
            recoverStuckTransfer(now);
        } else {
            requestRestart("control pipe not answering after recovery");
        }
        return;
    default:
        break;
    }
#if defined(NUT_TEST_STUCK_CYCLE) && NUT_TEST_STUCK_CYCLE
    if (_test_stuck_active) {
        serviceQxRequest(false);
        return;
    }
#endif

    // A command or SET VAR of the Q* driver takes the place of this iteration's poll step:
    // one step never waits for more than qxStepBoundMs() of the transport (US-136)
    if (serviceQxRequest(true)) return;

    // US-123 (issue #111): when the poll requests every value of the INPUT reports, a silence
    // freezes nothing, and the watchdog neither makes the data stale nor recovers or restarts
    _in_wd.setValuesPolled(DriverDispatch::inputValuesPolled(_driver));
    switch (_in_wd.tick(now)) {
    case LinkMonitor::Action::RECOVER:
        log("WARN", "[USB] No INPUT report for %u s (period %u ms)",
            (unsigned)(_in_wd.silenceThresholdMs() / 1000), (unsigned)_in_wd.periodMs());
        recoverInterface("INPUT reports stopped", now, true);
        return;
    case LinkMonitor::Action::RESTART:
        requestRestart("INPUT reports stopped after recovery");
        return;
    default:
        break;
    }
    if (_in_wd.takeSilenceNotice(now)) {
        log("INFO", "[USB] No INPUT report for %u s (period %u ms), values still read with GET_REPORT",
            (unsigned)(_in_wd.silenceThresholdMs() / 1000), (unsigned)_in_wd.periodMs());
    }

    logStats(now);

    // Drivers skip their poll steps while isPollingPaused() reports a backoff. Right after a
    // request of the export read the poll waits for the spacing too (US-119): the driver
    // only knows the time of its own steps.
    bool diag_spacing = _diag.state() == DiagSweep::State::Reading &&
                        (now - _diag_last_at) < _driver->requestSpacingMs();
    if (!diag_spacing) _driver->poll(_ups_data, now); // on this transport (DriverDispatch::attach)
    logOnlineDischarge(now);
    // After the poll, so the poll goes first when both are due
    serviceDiagRead();

    if (!_first_data_ready) {
        const bool polled = _driver->initialPollDone();
        bool status_known = false;
        if (polled) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            status_known = UPSData::computeUPSStatusString(_ups_data) != "Unknown";
        }
        // The Q* driver waits without limit (firstDataWaitBounded(), US-136)
        bool timed_out = false;
        if (firstDataReady(polled, status_known, _driver->firstDataWaitBounded(), now - _claimed_at,
                           FIRST_DATA_MAX_MS, timed_out)) {
            _first_data_ready = true;
            log(timed_out ? "WARN" : "INFO", "[USB] First full poll %s after %u ms: data served",
                timed_out ? "not done" : "done", (unsigned)(now - _claimed_at));
        }
    }
}

void USBHostUPS::logStats(uint32_t now) {
    if ((now - _stat_since) < STATS_PERIOD_MS) return;
    if (_stat_input || _stat_in_packets || _stat_get_ok || _stat_get_failed || _stat_diag_empty ||
        _stat_port_cycles || _stat_qx_ok || _stat_qx_failed) {
        // The empty answers of the export read (US-119) are not failures: their own count,
        // written only when there are some, so the line stays as before without an export.
        // Same for the USB port power cycles of a stuck transfer (US-122).
        char empty[40] = "";
        if (_stat_diag_empty) {
            snprintf(empty, sizeof(empty), ", %u empty (export read)", (unsigned)_stat_diag_empty);
        }
        char cycles[40] = "";
        if (_stat_port_cycles) {
            snprintf(cycles, sizeof(cycles), ", %u USB port power cycles", (unsigned)_stat_port_cycles);
        }
        // And the transfers of the Q* driver (US-136)
        char qx[48] = "";
        if (_stat_qx_ok || _stat_qx_failed) {
            snprintf(qx, sizeof(qx), ", %u Q* transfers ok, %u failed", (unsigned)_stat_qx_ok,
                     (unsigned)_stat_qx_failed);
        }
        log("INFO", "[USB] Last %u s: %u INPUT reports (%u packets), %u GET_REPORT ok, %u failed%s%s%s",
            (unsigned)((now - _stat_since) / 1000), (unsigned)_stat_input, (unsigned)_stat_in_packets,
            (unsigned)_stat_get_ok, (unsigned)_stat_get_failed, empty, cycles, qx);
    }
    _stat_since = now;
    _stat_input = _stat_in_packets = _stat_get_ok = _stat_get_failed = _stat_diag_empty = 0;
    _stat_port_cycles = 0;
    _stat_qx_ok = _stat_qx_failed = 0;
}

void USBHostUPS::processEvents() {
    if (!_event_queue) return;

    HidEvent ev;
    // First the events a Q* command read while it waited (US-136): they came before the
    // ones still in the queue. Always empty on the HID path.
    while (_event_backlog.pop(ev)) handleEvent(ev);
    for (UBaseType_t i = 0; i < EVENT_QUEUE_LEN && xQueueReceive(_event_queue, &ev, 0) == pdTRUE; i++) {
        handleEvent(ev);
    }

    uint32_t dropped = _dropped_events;
    if (dropped != _reported_dropped_events) {
        log("WARN", "HID event queue full: %u INPUT reports dropped so far", (unsigned)dropped);
        _reported_dropped_events = dropped;
    }
}

void USBHostUPS::handleEvent(const HidEvent& ev) {
    switch (ev.type) {
    case HidEvent::CONNECTED:
        _enum_seen = true;
        log("INFO", "HID Device Connected event");
        claimInterface(ev.handle);
        break;
    case HidEvent::OPEN_FAILED:
        _enum_seen = true; // enumerated: the HID driver saw it
        log("ERROR", "hid_host_device_open failed");
        break;
    case HidEvent::INPUT_REPORT:
        processInputReport(ev);
        break;
    case HidEvent::DISCONNECTED:
        handleDisconnected(ev.handle);
        break;
    case HidEvent::TRANSFER_ERROR:
        if (ev.handle == _hid_dev_handle && (_quirks & QUIRK_NO_INTERRUPT_PIPE)) {
            // US-108: upstream never reads this pipe (usbhid-ups.c:1432), so its errors
            // are not a fault and must not climb to a restart
            logDebug("[USB] INPUT transfer error ignored: interrupt pipe disabled");
        } else if (ev.handle == _hid_dev_handle && _is_ready_to_poll && !_in_restart_pending && !_restart_requested) {
            if (++_in_recoveries > MAX_IN_RECOVERIES) {
                requestRestart("INPUT transfer keeps failing");
            } else {
                recoverInterface("INPUT transfer error", millis(), true);
            }
        }
        break;
    }
}

void USBHostUPS::claimInterface(hid_host_device_handle_t handle) {
    // The family is chosen before the report descriptor (US-132, ADR 0013 section 2), from
    // what hid_host_get_device_info() copies out of the descriptors read at enumeration:
    // no transfer goes out for it.
    hid_host_dev_info_t dev_info;
    const bool have_info = hid_host_get_device_info(handle, &dev_info) == ESP_OK;
    uint32_t quirks = 0;
    char mfr[HID_STR_DESC_MAX_LENGTH + 1] = {0};
    char product[HID_STR_DESC_MAX_LENGTH + 1] = {0};
    if (have_info) {
        // Quirks first: the strings the registry looks at need QUIRK_INVERT_STRINGS
        for (int q = 0; UPS_QUIRKS[q].vid != 0; q++) {
            if (UPS_QUIRKS[q].vid == dev_info.VID && (UPS_QUIRKS[q].pid == 0xFFFF || UPS_QUIRKS[q].pid == dev_info.PID)) {
                quirks |= UPS_QUIRKS[q].flags;
            }
        }

        // Some VID/PID are shared by subdrivers that tell the devices apart by their
        // strings, as usbhid-ups does (US-097, Phoenixtec 06da:ffff), and so does nutdrv_qx
        const bool invert = (quirks & QUIRK_INVERT_STRINGS) != 0;
        DeviceStrings::toAscii(dev_info.iManufacturer, HID_STR_DESC_MAX_LENGTH, invert, mfr, sizeof(mfr));
        DeviceStrings::toAscii(dev_info.iProduct, HID_STR_DESC_MAX_LENGTH, invert, product, sizeof(product));

        DriverRegistry::Match match = DriverRegistry::match(dev_info.VID, dev_info.PID, mfr, product);
        if (match.family == DriverRegistry::Family::QX) {
            claimQxInterface(handle, match, dev_info.VID, dev_info.PID, quirks, product);
            return;
        }
    }

    // HID path: without device information the interface is taken as before, with no driver
    size_t desc_len = 0;
    uint8_t *desc = hid_host_get_report_descriptor(handle, &desc_len);

    if (!desc) {
        // Not "non-UPS": the interface never answered (issue #97)
        if (deferClaim(handle, millis())) return;
        log("ERROR", "[USB] Report descriptor not available: interface dropped");
        // A timed-out read keeps EP0 busy until the URB comes back, and that takes a
        // restart (ADR 0008). Not while another interface is serving the UPS.
        bool ctrl_stuck = hid_host_device_ctrl_stuck(handle);
        closeInterface(handle);
        if (ctrl_stuck && _hid_dev_handle == NULL) requestRestart("report descriptor not available");
        return;
    }
    clearDeferredClaim(handle);

    HIDParser temp_parser;
    temp_parser.parseReportDescriptor(desc, desc_len);

    // Explicit HID rows and the generic fallback alike: only an interface with UPS or
    // Battery pages is a UPS
    if (!DriverRegistry::hasUpsPages(temp_parser.getUsages())) {
        // Not a UPS! Close it!
        log("INFO", "Ignoring non-UPS interface.");
        closeInterface(handle);
        return;
    }

    if (_hid_dev_handle != NULL && _hid_dev_handle != handle) {
        // Replace the previously bound interface
        closeInterface(_hid_dev_handle);
    }

    std::lock_guard<std::recursive_mutex> lock(_mutex);

    _hid_dev_handle = handle;
    _qx_transport = nullptr;
    _unsupported_device = "";
    _in_pipe_wanted = true;
    _hid_parser = temp_parser;
    _cached_reports.clear();
    _diag.clear();
    _diag_requested = false;

    _cached_report_descriptor_hex = "";
    for (size_t i = 0; i < desc_len; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", desc[i]);
        _cached_report_descriptor_hex += hex;
    }

    if (have_info) {
        // Device information, quirks and strings read at the top of claimInterface()
        _vid = dev_info.VID;
        _pid = dev_info.PID;
        _quirks = quirks;

        if (_driver) { delete _driver; _driver = nullptr; }
        _driver = DriverRegistry::create(_vid, _pid, mfr, product);
        DriverDispatch::attach(_driver, this); // the HID transport poll() runs on (US-131)
        _driver->setup();
        applySavedDriverValues();
        populateStringsFromDeviceInfo(dev_info, _quirks, _ups_data);
        _device_product = _ups_data.get("ups.model");
        _driver->formatDeviceStrings(_ups_data);
        applyDefaultDelays();
    }

    _link.reset(millis());
    if (_quirks & QUIRK_NO_INTERRUPT_PIPE) {
        // upslogx(LOG_INFO, "interrupt pipe disabled ...") of apc-hid.c:51-62 (US-108)
        log("INFO", "[USB] Interrupt pipe disabled: values read with GET_REPORT (APC 5G, apc-hid)");
    }
    _in_wd.reset(millis());
    _in_reasm.reset();
    _ep_in_mps = hid_host_device_get_ep_in_mps(handle);
    _uses_report_ids = _hid_parser.usesReportIds();
    // String indices, so the drivers can fetch a string the USB Host library missed (review M5)
    _iManufacturer = _iProduct = _iSerialNumber = 0;
    hid_host_device_get_string_indices(handle, &_iManufacturer, &_iProduct, &_iSerialNumber);
    _lang_id = 0;
    memset(_failed_strings, 0, sizeof(_failed_strings));
    memset(_input_seen, 0, sizeof(_input_seen));
    _in_restart_pending = false;
    _in_recoveries = 0;
    // Stale until the first full poll: the values are partial (issue #60)
    _first_data_ready = false;
    _claimed_at = millis();
    _get_ok_since_claim = 0;
    if (_restart_requested) {
        // The UPS enumerated again: whatever needed the restart is gone (review A7)
        log("INFO", "[USB] UPS enumerated again: restart request cancelled");
        _restart_requested = false;
        _restart_reason = "";
    }
    // A failed start used to go unnoticed: no INPUT report would ever arrive and nothing
    // said why. Retry it like after a recovery (retryInterfaceStart).
    esp_err_t start_err = hid_host_device_start(_hid_dev_handle);
    if (start_err != ESP_OK) {
        log("WARN", "[USB] INPUT pipe not started: %s, retrying", esp_err_to_name(start_err));
        _in_restart_pending = true;
        _in_start_attempts = 0;
        _in_restart_at = millis() + 50;
    }
    _is_ready_to_poll = true;

    log("INFO", "UPS interface claimed and ready (IN endpoint MPS %u).", (unsigned)_ep_in_mps);
}

bool USBHostUPS::deferClaim(hid_host_device_handle_t handle, uint32_t now) {
    if (_desc_retry_handle != NULL && _desc_retry_handle != handle) return false; // one at a time
    _desc_retry_handle = handle;
    if (!_desc_retry.onFailure(now)) {
        _desc_retry_handle = NULL;
        return false;
    }
    log("WARN", "[USB] Report descriptor not available (attempt %u of %u), retry in %u ms",
        (unsigned)_desc_retry.attempts(), (unsigned)DescriptorRetry::MAX_ATTEMPTS,
        (unsigned)DescriptorRetry::RETRY_DELAY_MS);
    return true;
}

void USBHostUPS::clearDeferredClaim(hid_host_device_handle_t handle) {
    if (handle != _desc_retry_handle) return;
    _desc_retry_handle = NULL;
    _desc_retry.clear();
}

void USBHostUPS::processInputReport(const HidEvent& ev) {
    // A driver without the HID extension (Q*, US-136) reads its interrupt IN chunks inside its
    // commands; one arriving outside a command is stale and dropped
    if (ev.handle != _hid_dev_handle || !DriverDispatch::hidOf(_driver)) return;
    // disable_interrupt_pipe of apc-hid.c:51-62 (US-108): on the APC 5G the interrupt pipe
    // carries a proprietary protocol. Its transfers are dropped before the statistics and the
    // InputWatchdog, which is never fed and never acts: no INPUT is not a failure here.
    if (_quirks & QUIRK_NO_INTERRUPT_PIPE) return;

    _in_recoveries = 0;
    _stat_in_packets++;
    _in_wd.onInput(ev.ts);

    // Reports longer than one packet arrive in several events (review A4)
    size_t expected = 0;
    if (ev.length > 0) {
        expected = _hid_parser.getInputLength(_uses_report_ids ? ev.data[0] : 0);
    }
    const uint8_t* data = nullptr;
    size_t length = 0;
    uint32_t dropped_before = _in_reasm.dropped();
    bool complete = _in_reasm.feed(ev.data, ev.length, ev.ts, _ep_in_mps, expected, data, length);
    if (_in_reasm.dropped() != dropped_before) {
        log("WARN", "[USB] Incomplete multi-packet INPUT report dropped");
    }
    if (!complete) return;

    uint8_t r_id = (length > 0) ? data[0] : 0;
    _stat_input++; // summarised every STATS_PERIOD_MS instead of one log per report (review S1)
    _input_ts[r_id] = ev.ts;
    _input_seen[r_id / 32] |= 1u << (r_id % 32);

    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (length > 0) {
        uint16_t key = (1 << 8) | r_id; // type 1 = INPUT
        auto& cached = _cached_reports[key];
        cached.report_id = r_id;
        cached.report_type = 1;
        cached.data.assign(data, data + length);
    }
    // decodeReport(), then the NUT table writes its variables last (US-066)
    DriverDispatch::decode(_driver, this, r_id, 1, data, length, _ups_data);
}

void USBHostUPS::handleDisconnected(hid_host_device_handle_t handle) {
    // The first hid_host_device_close() (ours, or the HID driver's on physical removal)
    // raised this event; this second one releases the interface.
    hid_host_device_close(handle);
    clearDeferredClaim(handle);
    if (_hid_dev_handle != handle) return;

    log("INFO", "HID Device Disconnected");
    releaseBoundState();
}

void USBHostUPS::releaseBoundState() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _hid_dev_handle = NULL;
    _is_ready_to_poll = false;
    _in_restart_pending = false;
    _ups_data = UPSData();
    _device_product = "";
    if (_driver) { delete _driver; _driver = nullptr; }
    _hid_parser = HIDParser();
    _cached_reports.clear();
    _diag.clear();
    _diag_requested = false;
    _ol_dischrg_log = OnlineDischargeLog();
    _qx_transport = nullptr;
    _unsupported_device = "";
    // A Q* request waiting for the driver just deleted (US-136)
    answerQxRequest(CommandResult::NOT_CONNECTED, NutTable::SetStatus::NOT_CONNECTED);
}

void USBHostUPS::claimQxInterface(hid_host_device_handle_t handle, const DriverRegistry::Match& match,
                                  uint16_t vid, uint16_t pid, uint32_t quirks, const char* product) {
    // nutdrv_qx claims usb_subdriver.hid_rep_index, LIBUSB_DEFAULT_INTERFACE = 0 unless the
    // usb_hid_rep_index option is given (nut_repo/drivers/libusb1.c:244-245, nut_libusb.h:41)
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(handle, &params) != ESP_OK || params.iface_num != 0) {
        logDebug("[USB] Q* UPS: interface other than 0 dropped");
        closeInterface(handle);
        return;
    }

    if (_hid_dev_handle != NULL && _hid_dev_handle != handle) {
        // Replace the previously bound interface: its DISCONNECTED is not queued
        // (closeInterface()), so its state goes here
        closeInterface(_hid_dev_handle);
        releaseBoundState();
    }
    clearDeferredClaim(handle);

    // The ported USB transport of the row (US-136): with one, the Q* driver; without, the
    // UPS stays "not supported yet" (US-132)
    std::unique_ptr<QxUsbTransport> transport = QxUsbTransports::create(match.qx_transport, _qx_usb);
    if (transport) {
        attachQxDriver(handle, match, std::move(transport), vid, pid, quirks, product);
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        // The bound interface for both families: handleDisconnected() releases it as usual.
        // _is_ready_to_poll stays false and _driver nullptr, so service() stops before the
        // link monitor, the INPUT watchdog and the export read, processInputReport() drops
        // everything and TRANSFER_ERROR is ignored: no port reset, no restart (US-132).
        _hid_dev_handle = handle;
        _qx_transport = match.qx_transport;
        _unsupported_device = String("Q* UPS (USB transport: ") + match.qx_transport + ")";
        // What the USB diagnostics export shows: this device, not the one bound before it.
        // No report descriptor is read on this path, and _ups_data stays empty (no UPS for
        // the consumers): the product string goes only to the export.
        _vid = vid;
        _pid = pid;
        _quirks = quirks;
        _device_product = product;
        _cached_report_descriptor_hex = "";
    }
    if (_restart_requested) {
        // The UPS enumerated again: whatever needed the restart is gone (review A7)
        log("INFO", "[USB] UPS enumerated again: restart request cancelled");
        _restart_requested = false;
        _restart_reason = "";
    }

    log("INFO", "Q* UPS recognized (USB transport: %s), not supported yet", match.qx_transport);
}

void USBHostUPS::attachQxDriver(hid_host_device_handle_t handle, const DriverRegistry::Match& match,
                                std::unique_ptr<QxUsbTransport> transport, uint16_t vid, uint16_t pid,
                                uint32_t quirks, const char* product) {
    const bool wants_in = transport->needsInterruptIn();
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _hid_dev_handle = handle;
        _qx_transport = match.qx_transport;
        _unsupported_device = "";
        _vid = vid;
        _pid = pid;
        _quirks = quirks;
        _device_product = product;
        _cached_report_descriptor_hex = "";
        _hid_parser = HIDParser();
        _cached_reports.clear();
        _diag.clear();
        _diag_requested = false;
        if (_driver) { delete _driver; _driver = nullptr; }
        _driver = new QxDriver(std::move(transport), _qx_usb, vid, pid);
        _driver->setup();
        _in_pipe_wanted = wants_in;
    }

    // Shared supervision as on the HID path (ADR 0013 section 2): link monitor, recovery
    // ladder, first data; the INPUT watchdog stays inert without the HID extension
    _link.reset(millis());
    _in_wd.reset(millis());
    _in_reasm.reset();
    _ep_in_mps = hid_host_device_get_ep_in_mps(handle);
    _in_restart_pending = false;
    _in_recoveries = 0;
    _first_data_ready = false;
    _claimed_at = millis();
    _get_ok_since_claim = 0;
    if (_restart_requested) {
        // The UPS enumerated again: whatever needed the restart is gone (review A7)
        log("INFO", "[USB] UPS enumerated again: restart request cancelled");
        _restart_requested = false;
        _restart_reason = "";
    }
    if (_in_pipe_wanted) {
        esp_err_t start_err = hid_host_device_start(_hid_dev_handle);
        if (start_err != ESP_OK) {
            log("WARN", "[USB] INPUT pipe not started: %s, retrying", esp_err_to_name(start_err));
            _in_restart_pending = true;
            _in_start_attempts = 0;
            _in_restart_at = millis() + 50;
        }
    }
    _is_ready_to_poll = true;

    log("INFO", "Q* UPS attached (USB transport: %s): probing the protocol", match.qx_transport);
}

String USBHostUPS::unsupportedDevice() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _unsupported_device;
}

void USBHostUPS::logOnlineDischarge(uint32_t now) {
    // usbhid-ups warns when the UPS reports ACPresent and Discharging together: ups.status
    // is OL DISCHRG then, which may be a calibration or a UPS that is really on battery.
    // Checked at the quick poll pace, not on every 10 ms iteration
    if (now - _ol_dischrg_checked_at < 2000) return;
    _ol_dischrg_checked_at = now;
    bool due;
    int charge, previous;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_ups_data.hasKey("ups.status.ac_present")) return;
        int served = _ups_data.hasKey("battery.charge") ? (int)lroundf(_ups_data.getFloat("battery.charge", -1)) : -1;
        due = _ol_dischrg_log.update(_ups_data.getBool("ups.status.ac_present"),
                                     _ups_data.getBool("ups.status.discharging"), served);
        charge = _ol_dischrg_log.charge();
        previous = _ol_dischrg_log.previousCharge();
    }
    if (!due) return;
    if (previous < 0) {
        log("WARN", "[USB] OL+DISCHRG: AC present while discharging, battery charge %d %%. "
                    "Calibrating? Some UPS models report this on battery", charge);
    } else {
        log("WARN", "[USB] OL+DISCHRG: battery charge %d %% -> %d %% (%s). "
                    "Calibrating? Some UPS models report this on battery",
            previous, charge, charge > previous ? "charging" : "draining");
    }
}

void USBHostUPS::closeInterface(hid_host_device_handle_t handle) {
    // The first close raises DISCONNECTED synchronously (in this task) and leaves the
    // interface in WAIT_USER_DELETION; the second one removes it right away, so it does
    // not linger in the hid_host list while the HID task may be walking it.
    _self_close_handle = handle;
    hid_host_device_close(handle);
    _self_close_handle = NULL;
    hid_host_device_close(handle);
}

void USBHostUPS::noteControlResult(esp_err_t err, uint32_t now) {
    // Every control request passes here: the export read keeps its spacing from it (US-119)
    _last_ctrl_at = now;
    // A STALL or an oversized answer still proves the device is talking to us
    if (err == ESP_OK || err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_INVALID_SIZE) {
        if (_link.failures() > 0) log("INFO", "[USB] Control pipe answering again");
        _link.onAlive(now);
    } else {
        _link.onLinkFailure(now);
    }
}

void USBHostUPS::recoverInterface(const char* why, uint32_t now, bool clear_in_halt) {
    if (!_in_pipe_wanted) {
        // A Q* transport without interrupt IN (US-136): no IN pipe to stop, clear or start,
        // only the driver starts again (the protocol is detected again)
        log("WARN", "[USB] Restarting the Q* driver: %s", why);
        if (_driver) {
            _driver->setup();
            applySavedDriverValues();
        }
        return;
    }
    log("WARN", "[USB] Restarting HID interface: %s", why);
    esp_err_t err = hid_host_device_stop(_hid_dev_handle); // halt + flush + clear of the IN endpoint
    if (err != ESP_OK) log("WARN", "[USB] hid_host_device_stop failed: %s", esp_err_to_name(err));
    _in_reasm.reset();

    if (clear_in_halt) {
        // The stop only resets the host side: an endpoint the device put in STALL stays
        // halted until CLEAR_FEATURE(ENDPOINT_HALT) (review A3). Blocking, no lock held.
        err = hid_host_device_clear_ep_in_halt(_hid_dev_handle);
        noteControlResult(err, millis());
        if (err == ESP_OK) {
            log("INFO", "[USB] IN endpoint halt cleared");
        } else {
            log("WARN", "[USB] CLEAR_FEATURE(ENDPOINT_HALT) failed: %s", esp_err_to_name(err));
        }
    }
    if (_driver) {
        _driver->setup(); // restart the poll cycle from scratch
        applySavedDriverValues();
    }
    _in_restart_pending = true;
    _in_start_attempts = 0;
    _in_restart_at = now + 50;
}

void USBHostUPS::retryInterfaceStart(uint32_t now) {
    if ((int32_t)(now - _in_restart_at) < 0) return;

    // The flushed IN transfer is handed back asynchronously by the HID task:
    // until then the submit fails with ESP_ERR_NOT_FINISHED.
    esp_err_t err = hid_host_device_start(_hid_dev_handle);
    if (err == ESP_OK) {
        _in_restart_pending = false;
        log("INFO", "[USB] HID interface restarted");
        return;
    }
    if (++_in_start_attempts >= MAX_IN_START_ATTEMPTS) {
        _in_restart_pending = false;
        log("ERROR", "[USB] HID interface restart failed: %s", esp_err_to_name(err));
        requestRestart("HID interface restart failed");
        return;
    }
    _in_restart_at = now + 50;
}

void USBHostUPS::requestRestart(const char* why) {
    if (_restart_requested) return;
    log("ERROR", "[USB] Recovery exhausted (%s): system restart requested", why);
    _restart_reason = why;
    _restart_requested = true;
}

void USBHostUPS::serviceEnumWatchdog(uint32_t now) {
    if (!_enum_wd.armed()) return;
    bool enumerated = _enum_seen;
    if (!enumerated) {
        // Also a device the HID driver does not report, or an event we missed
        usb_host_lib_info_t info;
        enumerated = usb_host_lib_info(&info) == ESP_OK && info.num_devices > 0;
    }
#if defined(NUT_TEST_ENUM_WATCHDOG) && NUT_TEST_ENUM_WATCHDOG
    if (_auto_restart_count == 0) enumerated = false; // test build: the UPS "never" enumerates
#endif
    switch (_enum_wd.tick(enumerated, now)) {
    case EnumWatchdog::Action::CYCLE_PORT:
        if (_enum_wd.mode() == EnumWatchdog::Mode::AFTER_NORMAL_BOOT) {
            // INFO: a board powered on without a UPS goes through here too (US-139)
            log("INFO", "[USB] No UPS enumerated %u min after boot: power-cycling the USB port (%u/%u)",
                (unsigned)(now / 60000), (unsigned)_enum_wd.cycles(), (unsigned)EnumWatchdog::MAX_PORT_CYCLES);
        } else {
            log("WARN", "[USB] UPS not enumerated %u min after the automatic restart: "
                        "power-cycling the USB port (attempt %u of %u)",
                (unsigned)(now / 60000), (unsigned)_enum_wd.cycles(), (unsigned)EnumWatchdog::MAX_PORT_CYCLES);
        }
        cycleRootPort();
        break;
    case EnumWatchdog::Action::REQUEST_RESTART:
        requestRestart(NOT_ENUMERATED_REASON);
        break;
    default:
        break;
    }
}

// The root port of a cancelled enumeration stays disabled until a detach (hub.c). Powering
// it off frees the device node, powering it on lets an attached device connect again. The
// boards do not switch VBUS with it: the UPS stays powered, like across esp_restart().
// With a device attached (US-122) the USBH gives the stuck control URB back before DEV_GONE.
// Poll task, _op_mutex held: the pause blocks no request of ours.
void USBHostUPS::cycleRootPort() {
    esp_err_t err = usb_host_lib_set_root_port_power(false);
    if (err != ESP_OK) {
        log("WARN", "[USB] USB port power off failed: %s", esp_err_to_name(err));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    err = usb_host_lib_set_root_port_power(true);
    if (err != ESP_OK) {
        log("WARN", "[USB] USB port power on failed: %s", esp_err_to_name(err));
    }
}

// A timed-out control URB stays in flight in the ESP-IDF stack (issue #120): switching the
// port off makes the device go away, and the USBH halts and flushes EP0 and hands the URB
// back with its callback before DEV_GONE (usbh.c, ADR 0008). The UPS then re-enumerates;
// a restart only if the cycle did not help (PortCyclePolicy).
void USBHostUPS::recoverStuckTransfer(uint32_t now) {
#if defined(NUT_TEST_STUCK_CYCLE) && NUT_TEST_STUCK_CYCLE
    _test_stuck_active = false;
#endif
    if (_port_cycles.onStuck(now) == PortCyclePolicy::Decision::RESTART) {
        requestRestart(_port_cycles.lastRestartReason());
        return;
    }
    log("WARN", "[USB] Control transfer stuck: power-cycling the USB port (cycle %u since boot)",
        (unsigned)_port_cycles.cyclesSinceBoot());
    {
        // Stale at once, and no more requests to a device that is going away
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _is_ready_to_poll = false;
    }
    // No other RESTART before the disconnection event; claimInterface() resets it again
    _link.reset(now);
    _stat_port_cycles++;
    cycleRootPort();
}

bool USBHostUPS::servicePortCycleWait(uint32_t now) {
    if (!_port_cycles.waiting()) return false;
    if (isControlPipeProven()) {
        _port_cycles.onProven();
        log("INFO", "[USB] UPS back after the USB port power cycle in %u ms",
            (unsigned)(now - _port_cycles.cycleStartedAt()));
        return false;
    }
    if (_port_cycles.backTimedOut(now)) {
        requestRestart(PortCyclePolicy::NOT_BACK_REASON);
        return true;
    }
    return false;
}

bool USBHostUPS::isPollingPaused() const {
    return !_link.canPoll(millis());
}

bool USBHostUPS::isDataStale() const {
    uint32_t now = millis();
    // No device, or not claimed yet after boot (review A1). Like upsd while the driver is
    // not up: before issue #60 the first 15 s after boot were exempt, and NUT served an
    // empty status (Home Assistant: "unknown").
    if (!_is_ready_to_poll) return true;
    // Partial values until the first full poll (usbhid-ups upsdrv_initinfo())
    if (!_first_data_ready) return true;
    // Recovery exhausted: nothing refreshes the values while the restart waits (review A7)
    if (_restart_requested) return true;
    if (_link.isStale(now) || _in_wd.isStale(now)) return true;
    // The driver's own judgement: the Q* driver after the failed polls of
    // upsdrv_updateinfo() (US-136). _driver is deleted under _mutex
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _driver && _driver->dataStale();
}

bool USBHostUPS::isControlPipeProven() const {
    return _is_ready_to_poll && _first_data_ready && _get_ok_since_claim > 0 && _link.failures() == 0;
}

bool USBHostUPS::requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length) {
    if (!_is_ready_to_poll || !_hid_dev_handle) return false;
    if (!_link.canPoll(millis())) return false;

    size_t length = expected_length > 0 ? expected_length : 255;
    if (length > sizeof(_request_buffer)) length = sizeof(_request_buffer);

    // Blocking control transfer: no application lock may be held here (ADR 0008)
    esp_err_t err = hid_class_request_get_report(_hid_dev_handle, report_type, report_id, _request_buffer, &length);
    noteControlResult(err, millis());

    if (err == ESP_OK && length > 0) {
        _stat_get_ok++;
        _get_ok_since_claim++;
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        uint16_t key = (report_type << 8) | report_id;
        auto& cached = _cached_reports[key];
        cached.report_id = report_id;
        cached.report_type = report_type;
        cached.data.assign(_request_buffer, _request_buffer + length);

        DriverDispatch::decode(_driver, this, report_id, report_type, _request_buffer, length, _ups_data);
        return true;
    }

    _stat_get_failed++;
    if (err == ESP_OK) {
        log("ERROR", "requestReport FAILED: type=%d, id=%d, empty response", report_type, report_id);
    } else if (_link.failures() > 0) {
        log("ERROR", "requestReport FAILED: type=%d, id=%d, err=0x%x (%s), link failures=%u, polling paused",
            report_type, report_id, err, esp_err_to_name(err), (unsigned)_link.failures());
    } else {
        log("ERROR", "requestReport FAILED: type=%d, id=%d, err=0x%x (%s)", report_type, report_id, err, esp_err_to_name(err));
    }
    return false;
}

// ---------------------------------------------------------------------------
// On-demand read of the USB diagnostics export (US-119, issue #123)
// ---------------------------------------------------------------------------

bool USBHostUPS::requestDiagRead() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_is_ready_to_poll || !_driver) return false;
    if (_diag.state() == DiagSweep::State::Reading) return true; // not started again
    _diag_requested = true;
    return true;
}

DiagSweep::Status USBHostUPS::diagReadStatus() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_diag_requested) {
        // Waiting for the poll task: the previous results are about to be dropped
        DiagSweep::Status s;
        s.state = DiagSweep::State::Reading;
        return s;
    }
    return _diag.status();
}

void USBHostUPS::serviceDiagRead() {
    if (_diag_requested) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_diag_requested.exchange(false)) {
            std::set<uint16_t> cached;
            for (const auto& kv : _cached_reports) cached.insert(kv.first);
            const IUPSDriver* drv = _driver;
            _diag.start(_hid_parser.getUsages(), cached, [drv](uint8_t type, uint8_t id) {
                return DriverDispatch::acceptDiagReport(drv, type, id);
            });
            if (_diag.state() == DiagSweep::State::Done) logDiagReadEnd();
        }
    }
    if (_diag.state() != DiagSweep::State::Reading) return;

    // The poll's request (if any) may have just blocked: time taken again
    uint32_t now = millis();
    if (!_link.canPoll(now)) {
        interruptDiagRead();
        return;
    }
    if ((now - _last_ctrl_at) < _driver->requestSpacingMs()) return;

    DiagSweep::Item item;
    if (!_diag.next(item)) return;
    uint16_t expected = _hid_parser.getExpectedLength(item.id, item.type);
    size_t length = 0;
    DiagSweep::Result r = readReportRaw(item.id, item.type, DriverDispatch::requestLength(_driver, item.type, item.id, expected), length);
    _diag_last_at = millis();
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _diag.record(item.id, r, _request_buffer, length);
    }
    if (_diag.state() == DiagSweep::State::Done) logDiagReadEnd();
}

void USBHostUPS::interruptDiagRead() {
    if (_diag.state() != DiagSweep::State::Reading) return;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _diag.interrupt();
    }
    logDiagReadEnd();
}

void USBHostUPS::logDiagReadEnd() {
    DiagSweep::Status s = _diag.status();
    if (s.state == DiagSweep::State::Interrupted) {
        log("INFO", "[USB] Export read: interrupted after %u of %u reports, %u read, %u empty, %u failed",
            (unsigned)s.done, (unsigned)s.total, (unsigned)s.read, (unsigned)s.empty, (unsigned)s.failed);
    } else {
        log("INFO", "[USB] Export read: %u reports, %u read, %u empty, %u failed",
            (unsigned)s.total, (unsigned)s.read, (unsigned)s.empty, (unsigned)s.failed);
    }
}

DiagSweep::Result USBHostUPS::readReportRaw(uint8_t report_id, uint8_t report_type, uint16_t expected_length, size_t& length) {
    length = expected_length > 0 ? expected_length : 255;
    if (length > sizeof(_request_buffer)) length = sizeof(_request_buffer);

    // Blocking control transfer: no application lock may be held here (ADR 0008)
    esp_err_t err = hid_class_request_get_report(_hid_dev_handle, report_type, report_id, _request_buffer, &length);
    noteControlResult(err, millis());

    if (err == ESP_OK && length > 0) {
        _stat_get_ok++;
        _get_ok_since_claim++;
        return DiagSweep::Result::Read;
    }
    // A report the device leaves empty is what the export is for: no ERROR line and no
    // failure in the traffic summary, only its own count (issue #123)
    length = 0;
    if (err == ESP_OK) {
        _stat_diag_empty++;
        return DiagSweep::Result::Empty;
    }
    _stat_get_failed++;
    logDebug(String("[USB] Export read: report ") + report_id + " failed: " + esp_err_to_name(err));
    return DiagSweep::Result::Failed;
}

uint32_t USBHostUPS::inputReportAgeMs(uint8_t report_id, uint32_t now) const {
    if (!(_input_seen[report_id / 32] & (1u << (report_id % 32)))) return UINT32_MAX;
    // ts comes from the HID task and can be a few ms newer than the poll task's now
    int32_t age = (int32_t)(now - _input_ts[report_id]);
    return age > 0 ? (uint32_t)age : 0;
}

bool USBHostUPS::requestStringDescriptor(uint8_t string_index) {
    if (!_is_ready_to_poll || !_hid_dev_handle || string_index == 0) return false;
    // An index that failed once is not asked again for this device: a firmware that times
    // out on it would otherwise climb the recovery ladder every full poll (review M5)
    if (_failed_strings[string_index / 32] & (1u << (string_index % 32))) return false;
    if (!_link.canPoll(millis())) return false;

    // Blocking control transfers: no application lock held (ADR 0008)
    size_t len = 0;
    esp_err_t err;
    if (_lang_id == 0) {
        err = hid_host_device_get_string_descriptor(_hid_dev_handle, 0, 0, _request_buffer, 255, &len);
        noteControlResult(err, millis());
        if (err == ESP_OK && len >= 4 && _request_buffer[1] == 0x03) {
            _lang_id = (uint16_t)(_request_buffer[2] | (_request_buffer[3] << 8));
        } else if (err == ESP_OK || err == ESP_ERR_INVALID_RESPONSE) {
            _lang_id = 0x0409; // no language list (or a STALL): US English
        } else {
            // No answer: give up this string for the device instead of paying another
            // timeout at every full poll
            _failed_strings[string_index / 32] |= 1u << (string_index % 32);
            log("WARN", "[USB] String descriptor %u not available: no language ID (%s)",
                (unsigned)string_index, esp_err_to_name(err));
            return false;
        }
    }

    err = hid_host_device_get_string_descriptor(_hid_dev_handle, string_index, _lang_id, _request_buffer, 255, &len);
    noteControlResult(err, millis());
    if (err != ESP_OK || len < 2) {
        _failed_strings[string_index / 32] |= 1u << (string_index % 32);
        log("WARN", "[USB] String descriptor %u not available: %s", (unsigned)string_index,
            err == ESP_OK ? "empty" : esp_err_to_name(err));
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_driver) {
        DriverDispatch::parseString(_driver, this, string_index, _request_buffer, len, _ups_data);
        if (string_index == _iProduct) _device_product = _ups_data.get("ups.model");
        _driver->formatDeviceStrings(_ups_data);
        applyDefaultDelays();
    }
    return true;
}

IUSBHostUPS::UPSDataLock USBHostUPS::getUPSData() const {
    return IUSBHostUPS::UPSDataLock(_ups_data, this);
}

String USBHostUPS::getUPSStatusString() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    String status = UPSData::computeUPSStatusString(_ups_data);
    if (status.isEmpty()) {
        return "UNKNOWN";
    }
    return status;
}

void USBHostUPS::setLogCallback(LogCallback cb) {
    _log_cb = cb;
}

bool USBHostUPS::setBeeper(bool enable) {
    // Called by NUT or the web UI: wait for the current poll step, never overlap it
    if (!_op_mutex || xSemaphoreTake(_op_mutex, pdMS_TO_TICKS(OP_WAIT_MS)) != pdTRUE) return false;
    bool ok = setBeeperLocked(enable);
    xSemaphoreGive(_op_mutex);
    return ok;
}

bool USBHostUPS::setBeeperField(const HIDUsageDef& field, bool enable) {
    // Same wait and lock order as setBeeper() (ADR 0008)
    if (!_op_mutex || xSemaphoreTake(_op_mutex, pdMS_TO_TICKS(OP_WAIT_MS)) != pdTRUE) return false;
    bool ok = setBeeperLocked(enable, &field);
    xSemaphoreGive(_op_mutex);
    return ok;
}

const NutTable::HidInfo* USBHostUPS::commandTable(size_t& n) const {
    // Same lock as beeperMuteSupported(): _driver is deleted under _mutex. The table is static.
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    n = 0;
    return DriverDispatch::commandTable(_driver, n);
}

bool USBHostUPS::beeperFieldRead() const {
    // Same lock as commandTable(): _driver is deleted under _mutex
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return DriverDispatch::beeperFieldRead(_driver, this);
}

bool USBHostUPS::setBeeperLocked(bool enable, const HIDUsageDef* field) {
    if (!_is_ready_to_poll || !_hid_dev_handle) return false;
    // Devices that ignore the beeper commands must not be written to at all
    if (!DriverDispatch::beeperControllable(_driver)) return false;
    if (!_link.canPoll(millis())) return false;

    HIDUsageDef def;
    uint16_t expected_length = 0;
    bool shared_report = true;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        // Feature/Output item of the active beeper path, as beeper.mute writes it, or of the
        // command row's field (US-074)
        const HIDUsageDef* found = nullptr;
        if (field) {
            // The item cmdField() picked first (same report type and ID), then any Feature/Output
            // item of the path: a path declared as both FEATURE and OUTPUT is written where the
            // command row found it (US-109)
            for (int pass = 0; pass < 2 && !found; pass++) {
                for (const auto& u : _hid_parser.getUsages()) {
                    if (strcmp(u.path, field->path) != 0 || memcmp(u.coll_index, field->coll_index, sizeof(u.coll_index)) != 0) continue;
                    if (u.report_type != 0x03 && u.report_type != 0x02) continue;
                    if (pass == 0 && (u.report_type != field->report_type || u.report_id != field->report_id)) continue;
                    found = &u;
                    break;
                }
            }
        } else {
            found = activeBeeper(_hid_parser.getUsages());
        }
        if (!found) {
            return false;
        }
        def = *found;
        uint8_t rep_type = (def.report_type == 2) ? HID_REPORT_TYPE_OUTPUT : HID_REPORT_TYPE_FEATURE;
        expected_length = _hid_parser.getExpectedLength(def.report_id, rep_type);
        shared_report = BeeperLogic::reportHasOtherFields(_hid_parser.getUsages(), def);
    }

    uint8_t rep_type = (def.report_type == 2) ? HID_REPORT_TYPE_OUTPUT : HID_REPORT_TYPE_FEATURE;
    if (expected_length > sizeof(_request_buffer)) expected_length = sizeof(_request_buffer);

    memset(_request_buffer, 0, sizeof(_request_buffer));
    size_t fetched_len = expected_length;

    // STEP 1: Fetch current report to preserve other fields (no application lock held, ADR 0008)
    esp_err_t err = hid_class_request_get_report(_hid_dev_handle, rep_type, def.report_id, _request_buffer, &fetched_len);
    noteControlResult(err, millis());

    bool fetched = (err == ESP_OK && fetched_len > 0);
    if (!BeeperLogic::canWriteBack(shared_report, def, fetched, fetched_len)) {
        // Writing zeros over the other fields could switch the load off (review C1)
        log("WARN", "Beeper not changed: report %u shares fields and could not be read back (%s)",
            (unsigned)def.report_id, fetched ? "short answer" : esp_err_to_name(err));
        return false;
    }
    if (!fetched) {
        if (!_link.canPoll(millis())) return false; // pipe not answering, do not insist
        // Fallback for UPSes that reject GET_REPORT: the report holds only the beeper
        fetched_len = expected_length;
        if (def.report_id != 0) _request_buffer[0] = def.report_id;
    }

    fetched_len = BeeperLogic::manipulateBeeperBuffer(enable, &def, _request_buffer, fetched_len,
                                                      DriverDispatch::hidOf(_driver));
    if (fetched_len == 0) return false;

    // STEP 3: Write back
    err = hid_class_request_set_report(_hid_dev_handle, rep_type, def.report_id, _request_buffer, fetched_len);
    noteControlResult(err, millis());

    // Update local state immediately to prevent UI bouncing
    if (err == ESP_OK) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _ups_data.set("ups.beeper.status", enable ? "enabled" : "disabled");
        // The driver's table row too, or the next report would write the old value back (US-074)
        if (_driver) _driver->noteHostValue("ups.beeper.status", enable ? "enabled" : "disabled");
    }

    return err == ESP_OK;
}

bool USBHostUPS::writeUsage(const HIDUsageDef& def, uint32_t value) {
    // Same serialisation as setBeeper(): wait for the current poll step, never overlap it
    if (!_op_mutex || xSemaphoreTake(_op_mutex, pdMS_TO_TICKS(OP_WAIT_MS)) != pdTRUE) return false;
    bool ok = writeUsageLocked(def, value);
    xSemaphoreGive(_op_mutex);
    return ok;
}

bool USBHostUPS::writeUsageLocked(const HIDUsageDef& def, uint32_t value) {
    if (!_is_ready_to_poll || !_hid_dev_handle) return false;
    if (!_link.canPoll(millis())) return false;

    uint16_t expected_length = 0;
    uint16_t request_length = 0;
    bool shared_report = true;
    {
        // _driver is deleted under _mutex (handleDisconnected())
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        expected_length = _hid_parser.getExpectedLength(def.report_id, HID_REPORT_TYPE_FEATURE);
        // wLength as the poll asks it (APC Back-UPS BX: at least 8 bytes)
        request_length = DriverDispatch::requestLength(_driver, HID_REPORT_TYPE_FEATURE, def.report_id, expected_length);
        shared_report = BeeperLogic::reportHasOtherFields(_hid_parser.getUsages(), def);
    }
    if (expected_length > sizeof(_request_buffer)) expected_length = sizeof(_request_buffer);
    if (request_length > sizeof(_request_buffer)) request_length = sizeof(_request_buffer);

    memset(_request_buffer, 0, sizeof(_request_buffer));
    size_t fetched_len = request_length;

    // Read back the report to preserve its other fields (no application lock held, ADR 0008)
    esp_err_t err = hid_class_request_get_report(_hid_dev_handle, HID_REPORT_TYPE_FEATURE, def.report_id, _request_buffer, &fetched_len);
    noteControlResult(err, millis());

    bool fetched = (err == ESP_OK && fetched_len > 0);
    if (!BeeperLogic::canWriteBack(shared_report, def, fetched, fetched_len)) {
        // Writing zeros over the other fields could switch the load off (review C1)
        log("WARN", "%s not written: report %u shares fields and could not be read back (%s)",
            def.path, (unsigned)def.report_id, fetched ? "short answer" : esp_err_to_name(err));
        return false;
    }
    if (!fetched) {
        if (!_link.canPoll(millis())) return false; // pipe not answering, do not insist
        // The report holds only this field: write it from scratch
        fetched_len = expected_length;
        if (def.report_id != 0) _request_buffer[0] = def.report_id;
    }

    fetched_len = BeeperLogic::writeField(def, value, _request_buffer, fetched_len);
    if (fetched_len == 0) return false;

    err = hid_class_request_set_report(_hid_dev_handle, HID_REPORT_TYPE_FEATURE, def.report_id, _request_buffer, fetched_len);
    noteControlResult(err, millis());
    if (err != ESP_OK) {
        log("WARN", "%s = %u not written: %s", def.path, (unsigned)value, esp_err_to_name(err));
    }
    return err == ESP_OK;
}

CommandResult USBHostUPS::executeCommand(const char* name, const char* param) {
    CommandResult r = IUSBHostUPS::executeCommand(name, param);
    if (r == CommandResult::OK) {
        // data_has_changed of instcmd() (usbhid-ups.c:1157): the semi-static reports next.
        // An atomic flag: no wait for the poll step (_driver is deleted under _mutex)
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_driver) _driver->noteDataChanged();
    }
    // "load.off.delay 30": the parameter is part of what was asked
    String cmd = name ? name : "(null)";
    if (param && *param) {
        cmd += " ";
        cmd += param;
    }
    const UPSCommandInfo* info = UpsCommands::find(name);
    const bool destructive = info && info->destructive;
    switch (r) {
    case CommandResult::OK:
        // Commands that cut power to the load stay visible at WARN even when they succeed
        if (name && strcmp(name, "shutdown.default") == 0) {
            log("WARN", "Command %s: OK via %s", cmd.c_str(), _last_default_shutdown.c_str());
        } else {
            log(destructive ? "WARN" : "INFO", "Command %s: OK", cmd.c_str());
        }
        break;
    case CommandResult::FAILED:           log("WARN", "Command %s: failed", cmd.c_str()); break;
    case CommandResult::NOT_SUPPORTED:    log("WARN", "Command %s: not supported", cmd.c_str()); break;
    case CommandResult::NOT_CONNECTED:    log("WARN", "Command %s: UPS not connected", cmd.c_str()); break;
    case CommandResult::INVALID_ARGUMENT: log("WARN", "Command %s: invalid argument", cmd.c_str()); break;
    }
    return r;
}

bool USBHostUPS::isConnected() const {
    return _is_ready_to_poll;
}

const IUpsControl& USBHostUPS::activeControl() const {
    // A static object, still valid once the lock is released and the poll task deletes
    // _driver (handleDisconnected()): the control reads everything else through the port
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _driver ? _driver->control() : HidControl::instance();
}

String USBHostUPS::getActiveBeeperPath() const {
    for (const auto& u : _hid_parser.getUsages()) {
        if (strcmp(u.path, "UPS.PowerSummary.AudibleAlarmControl") == 0 ||
            strcmp(u.path, "UPS.BatterySystem.Battery.AudibleAlarmControl") == 0 ||
            strcmp(u.path, "UPS.AudibleAlarmControl") == 0) {
            return u.path;
        }
    }
    return "";
}

bool USBHostUPS::supportsBeeperToggle() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // A device that ignores the beeper commands (e.g. EcoFlow) still reports
    // ups.beeper.status, but must not advertise the toggle. Checked under _mutex:
    // handleDisconnected() deletes _driver with the same lock held.
    if (!DriverDispatch::beeperControllable(_driver)) return false;
    return getActiveBeeperPath() != "";
}

bool USBHostUPS::shutdownCommandsSupported() const {
    // Same lock as supportsBeeperToggle(): _driver is deleted under _mutex
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _driver ? _driver->shutdownCommandsSupported() : true;
}

bool USBHostUPS::beeperMuteSupported() const {
    // Same lock as supportsBeeperToggle(): _driver is deleted under _mutex
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return DriverDispatch::beeperMuteSupported(_driver);
}

void USBHostUPS::applyDefaultDelays() {
    // ups.delay.* with NUT semantics: driver defaults set at connection, never read from
    // the device (US-058). Right after formatDeviceStrings(); _mutex is recursive and
    // the callers already hold it.
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    DriverDispatch::applyDefaultDelays(_driver, _ups_data, _hid_parser.getUsages(), &_saved_vars);
}

void USBHostUPS::applySavedDriverValues() {
    // setup() drops the driver-side values of the table (outlet.*.desc): set the saved ones
    // again. ups.delay.* is left to applyDefaultDelays(), which checks them (US-067)
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_driver) return;
    for (const auto& kv : _saved_vars) {
        if (strncmp(kv.first.c_str(), "ups.delay.", 10) == 0) continue;
        _driver->setDriverSideValue(kv.first.c_str(), kv.second.c_str());
    }
}

void USBHostUPS::setDriverVarStore(IDriverVarStore* store) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _var_store = store;
    _saved_vars.clear();
    if (_var_store && !_var_store->load(_saved_vars)) {
        log("WARN", "Saved UPS variables not readable: driver defaults apply");
    }
}

NutTable::SetStatus USBHostUPS::setVariable(const char* name, const char* value) {
    NutTable::SetStatus st = IUSBHostUPS::setVariable(name, value);
    const char* var = name ? name : "(null)";
    const char* val = value ? value : "";
    switch (st) {
    case NutTable::SetStatus::OK:                log("INFO", "Variable %s = \"%s\": OK", var, val); break;
    case NutTable::SetStatus::FAILED:            log("WARN", "Variable %s = \"%s\": failed", var, val); break;
    case NutTable::SetStatus::CONVERSION_FAILED: log("WARN", "Variable %s = \"%s\": invalid value", var, val); break;
    case NutTable::SetStatus::UNKNOWN:           log("WARN", "Variable %s = \"%s\": not supported", var, val); break;
    case NutTable::SetStatus::NOT_CONNECTED:     log("WARN", "Variable %s = \"%s\": UPS not connected", var, val); break;
    }
    return st;
}

NutTable::SetStatus USBHostUPS::planSet(const char* name, const char* value, NutTable::SetPlan& plan) const {
    // The driver decides what to write, without I/O (setvar() of usbhid-ups)
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_driver) return NutTable::SetStatus::NOT_CONNECTED;
    return DriverDispatch::planSet(_driver, _ups_data, _hid_parser.getUsages(), name, value, plan);
}

NutTable::SetStatus USBHostUPS::saveDriverSideValue(const NutTable::SetPlan& plan) {
    // Saved first, outside _mutex (flash write): a value that is not saved changes nothing
    if (!_var_store || !_var_store->save(plan.name, plan.value.c_str())) return NutTable::SetStatus::FAILED;
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    DriverVarStore::upsert(_saved_vars, plan.name, plan.value.c_str());
    // Device gone meanwhile: the saved value applies at the next connection
    if (!_driver || !isConnected()) return NutTable::SetStatus::OK;
    if (strncmp(plan.name, "ups.delay.", 10) != 0) {
        // Kept by the table across binds; set in UPSData too, so LIST VAR shows it now
        _driver->setDriverSideValue(plan.name, plan.value.c_str());
    }
    // ups.delay.*: the commands read it at each run (configuredDelay)
    _ups_data.set(plan.name, plan.value);
    return NutTable::SetStatus::OK;
}

NutTable::SetStatus USBHostUPS::writePlannedValue(const NutTable::SetPlan& plan) {
    // HID: same serialisation as writeUsage(), no _mutex held during the transfers (ADR 0008)
    if (!_op_mutex || xSemaphoreTake(_op_mutex, pdMS_TO_TICKS(OP_WAIT_MS)) != pdTRUE) {
        return NutTable::SetStatus::FAILED;
    }
    bool ok = writeUsageLocked(plan.def, (uint32_t)plan.raw);
    if (ok) {
        {
            // data_has_changed of setvar() (usbhid-ups.c:1239): the semi-static reports next
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            if (_driver) _driver->noteDataChanged();
        }
        // Read the report back and decode it as the poll does: LIST VAR shows what the UPS
        // kept. Upstream waits for the next poll (data_has_changed); a failed read back
        // leaves it to the next full poll, the write itself succeeded
        uint16_t request_length;
        {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            request_length = _hid_parser.getExpectedLength(plan.def.report_id, HID_REPORT_TYPE_FEATURE);
            request_length = DriverDispatch::requestLength(_driver, HID_REPORT_TYPE_FEATURE, plan.def.report_id, request_length);
        }
        if (!requestReport(plan.def.report_id, HID_REPORT_TYPE_FEATURE, request_length)) {
            // Also when static reports are read once (APC Back-UPS BX): the next full poll
            // must not skip the report it still holds the old values of
            {
                std::lock_guard<std::recursive_mutex> lock(_mutex);
                DriverDispatch::invalidateReport(_driver, HID_REPORT_TYPE_FEATURE, plan.def.report_id);
            }
            log("WARN", "Variable %s written, read back failed: next full poll reads it", plan.name);
        }
    }
    xSemaphoreGive(_op_mutex);
    return ok ? NutTable::SetStatus::OK : NutTable::SetStatus::FAILED;
}

NutTable::SetStatus USBHostUPS::resetVariable(const char* name) {
    String value;
    NutTable::SetStatus st = activeControl().resetVariable(*this, name, value);
    const char* var = name ? name : "(null)";
    switch (st) {
    case NutTable::SetStatus::OK:
        // Empty: device gone or changed, only the saved value was erased
        if (value.length() == 0) log("INFO", "Variable %s: saved value erased", var);
        else log("INFO", "Variable %s reset to default \"%s\"", var, value.c_str());
        break;
    case NutTable::SetStatus::UNKNOWN:       log("WARN", "Variable %s: not supported", var); break;
    case NutTable::SetStatus::NOT_CONNECTED: log("WARN", "Variable %s: UPS not connected", var); break;
    default:                                 log("WARN", "Variable %s: reset failed", var); break;
    }
    return st;
}

NutTable::SetStatus USBHostUPS::resetDriverSideValue(const char* name, String& value) {
    // Only a driver-side variable of the catalog; canonical name, as the store keeps it.
    // Copied: the catalog points into the driver, deleted under _mutex
    String canonical;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_driver) return NutTable::SetStatus::NOT_CONNECTED;
        for (const auto& v : getWritableVariables()) {
            if (v.driver_side && NutTable::equalsIgnoreCase(v.name, name)) {
                canonical = v.name;
                break;
            }
        }
    }
    if (canonical.length() == 0) return NutTable::SetStatus::UNKNOWN;

    // Erased first, outside _mutex (flash write): a value still saved changes nothing
    if (!_var_store || !_var_store->erase(canonical.c_str())) return NutTable::SetStatus::FAILED;
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    DriverVarStore::remove(_saved_vars, canonical.c_str());
    // Device gone meanwhile: the driver default applies at the next connection
    if (!_driver || !isConnected()) return NutTable::SetStatus::OK;
    if (strncmp(canonical.c_str(), "ups.delay.", 10) == 0) {
        // Without the saved value applyDefaultDelays() sets the driver default again
        applyDefaultDelays();
        value = _ups_data.get(canonical);
        return NutTable::SetStatus::OK;
    }
    // outlet.*.desc: the dfl of the table, also at the next bind; in UPSData too for LIST VAR
    // No such ABSENT row: another device reconnected between the two locks. The store is
    // clean and this driver does not have the variable, so the reset still succeeded
    if (!_driver->resetDriverSideValue(canonical.c_str(), value)) {
        value = "";
        return NutTable::SetStatus::OK;
    }
    _ups_data.set(canonical, value);
    return NutTable::SetStatus::OK;
}

std::vector<NutTable::RWVariable> USBHostUPS::getWritableVariables() const {
    // _driver is deleted and the usages replaced under _mutex (recursive, as getUPSData())
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    std::vector<NutTable::RWVariable> out;
    if (!_driver || !isConnected()) return out;
    auto lk = getUPSData();
    _driver->writableVariables(lk.get(), out);
    return out;
}

void USBHostUPS::snapshotUSBDiagnostics(UsbDiagSnapshot& s) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    s.firmware_version = FIRMWARE_VERSION;
    s.vid = String(_vid, HEX);
    s.pid = String(_pid, HEX);
    s.manufacturer = _ups_data.get("ups.mfr");
    s.product = _device_product.length() > 0 ? _device_product : _ups_data.get("ups.model");
    s.serial_number = _ups_data.get("ups.serial");
    s.descriptor_hex = _cached_report_descriptor_hex;
    s.quirks = _quirks;
    s.driver = _driver ? _driver->getDriverName() : "None";

    s.link_failures = _link.failures();
    s.link_recoveries = _link.recoveries();
    s.stale = isDataStale();
    s.dropped_input_reports = (uint32_t)_dropped_events;
    s.input_period_ms = _in_wd.isPeriodic() ? _in_wd.periodMs() : 0;
    s.input_recoveries = _in_wd.recoveries();
    s.input_watchdog = DriverDispatch::inputWatchdogActive(_driver, (_quirks & QUIRK_NO_INTERRUPT_PIPE) != 0);
    s.port_cycles = _port_cycles.cyclesSinceBoot();
    s.incomplete_input_reports = _in_reasm.dropped();
    s.ep_in_mps = _ep_in_mps;

    s.export_read = diagReadStatus();

    s.reports.clear();
    s.reports.reserve(_cached_reports.size() + _diag.results().size());
    for (const auto& kv : _cached_reports) {
        UsbDiagReport r;
        r.id = kv.second.report_id;
        r.type = kv.second.report_type;
        r.data = kv.second.data;
        s.reports.push_back(std::move(r));
    }
    // Then the reports of the on-demand read the cache does not hold (US-119), by key. A
    // pending read has no results yet: the old ones are about to be dropped.
    if (!_diag_requested) addOnDemandReports(s.reports, _diag.results());
    s.ups_data = _ups_data.getAll();

    // Q* section (US-136): only with a Q* UPS bound, so the HID export does not change
    s.qx = UsbDiagQx();
    if (_qx_transport) {
        s.qx.present = true;
        s.qx.transport = _qx_transport;
        if (const QxDriver* q = _driver ? _driver->qx() : nullptr) q->fillDiagnostics(s.qx);
    }
    s.qx.uptime_ms = millis();
}

// ---------------------------------------------------------------------------
// Q* driver (US-136): primitives on the bound interface and requests of the other tasks
// ---------------------------------------------------------------------------

namespace {

// The poll task's event queue as readInterruptChunk() reads it
struct QxQueueSource {
    QueueHandle_t q;
    template <typename Ev>
    bool receive(Ev& ev, uint32_t waitMs) {
        return q && xQueueReceive(q, &ev, pdMS_TO_TICKS(waitMs)) == pdTRUE;
    }
    uint32_t now() { return millis(); }
};

} // namespace

int USBHostUPS::QxUsbPort::refuse(uint32_t now) {
    if (!_h._is_ready_to_poll || !_h._hid_dev_handle) {
        _error = "no device";
        return QX_USB_ERR_NO_DEVICE;
    }
    if (!_h._link.canPoll(now)) {
        _error = "link paused";
        return QX_USB_ERR_IO;
    }
    if (_deadline != 0 && (int32_t)(now - _deadline) >= 0) {
        _error = "budget";
        return QX_USB_ERR_TIMEOUT;
    }
    return 0;
}

int USBHostUPS::QxUsbPort::result(esp_err_t err) {
    // Every Q* control transfer feeds the link monitor, as the HID requests (ADR 0013 s. 2)
    _h.noteControlResult(err, millis());
    if (err == ESP_OK) {
        _h._stat_qx_ok++;
        _h._get_ok_since_claim++; // isControlPipeProven(): restart policy, port cycle wait
        _error = "";
        return 0;
    }
    _h._stat_qx_failed++;
    _error = esp_err_to_name(err);
    switch (err) {
    case ESP_ERR_TIMEOUT:          return QX_USB_ERR_TIMEOUT;
    case ESP_ERR_INVALID_RESPONSE: return QX_USB_ERR_PIPE; // STALL
    case ESP_ERR_INVALID_STATE:
    case ESP_ERR_NOT_FOUND:        return QX_USB_ERR_NO_DEVICE;
    default:                       return QX_USB_ERR_IO;
    }
}

int USBHostUPS::QxUsbPort::setReport(uint8_t reportType, uint8_t reportId, const uint8_t* data, size_t len) {
    if (int r = refuse(millis())) return r;
    if (len > sizeof(_h._request_buffer)) {
        _error = "report too long";
        return QX_USB_ERR_IO;
    }
    // The API takes a non-const buffer
    memcpy(_h._request_buffer, data, len);
    // Blocking control transfer: no application lock held (ADR 0008)
    esp_err_t err = hid_class_request_set_report(_h._hid_dev_handle, reportType, reportId, _h._request_buffer, len);
    if (int r = result(err)) return r;
    return (int)len;
}

int USBHostUPS::QxUsbPort::getStringDescriptor(uint8_t index, uint16_t langid, uint8_t* buf, size_t buflen) {
    if (int r = refuse(millis())) return r;
    // No _failed_strings, no _lang_id: the index is the command (krauler family)
    size_t got = 0;
    size_t max = buflen < 255 ? buflen : 255;
    esp_err_t err = hid_host_device_get_string_descriptor(_h._hid_dev_handle, index, langid, buf, max, &got);
    if (int r = result(err)) return r;
    return (int)got;
}

int USBHostUPS::QxUsbPort::readInterrupt(uint8_t* buf, size_t buflen, uint32_t timeoutMs) {
    const uint32_t now = millis();
    if (!_h._is_ready_to_poll || !_h._hid_dev_handle) {
        _error = "no device";
        return QX_USB_ERR_NO_DEVICE;
    }
    if (_deadline != 0) {
        if ((int32_t)(now - _deadline) >= 0) {
            _error = "budget";
            return QX_USB_ERR_TIMEOUT;
        }
        const uint32_t left = _deadline - now;
        if (left < timeoutMs) timeoutMs = left;
    }
    QxQueueSource src{_h._event_queue};
    size_t got = 0;
    uint32_t foreign = 0; // INPUT of another interface: not ours, dropped
    switch (readInterruptChunk<HidEventTraits>(src, _h._event_backlog, _h._hid_dev_handle, buf, buflen, got,
                                               timeoutMs, foreign)) {
    case ChunkRead::Chunk:
        _error = "";
        return (int)got;
    case ChunkRead::Timeout:
        _error = "timeout";
        return QX_USB_ERR_TIMEOUT;
    case ChunkRead::Gone:
        _error = "disconnected";
        return QX_USB_ERR_NO_DEVICE;
    case ChunkRead::Error:
        break;
    }
    _error = "transfer error";
    return QX_USB_ERR_IO;
}

void USBHostUPS::QxUsbPort::discardInput() {
    if (!_h._hid_dev_handle) return;
    QxQueueSource src{_h._event_queue};
    uint32_t foreign = 0;
    discardInputChunks<HidEventTraits>(src, _h._event_backlog, _h._hid_dev_handle, foreign);
}

void USBHostUPS::answerQxRequest(CommandResult cmd, NutTable::SetStatus set, uint32_t gen) {
    _qx_box.answer(cmd, set, gen);
}

bool USBHostUPS::serviceQxRequest(bool can_run) {
    return _qx_box.service(can_run, _qx_req_port);
}

// Takes s within ms, feeding the Task WDT of the caller every second: a Q* request waits up to
// two step bounds (25 s with the 12 s ceiling), and shutdown.default sends up to six of them
// from the loopTask, whose TWDT is 30 s (src/core/main.cpp). Every wait stays bounded, so the
// WDT still catches a real hang. esp_task_wdt_reset() does nothing for a task not subscribed.
static bool takeFeedingWdt(SemaphoreHandle_t s, uint32_t ms) {
    const uint32_t SLICE_MS = 1000;
    for (;;) {
        const uint32_t slice = ms < SLICE_MS ? ms : SLICE_MS;
        if (xSemaphoreTake(s, pdMS_TO_TICKS(slice)) == pdTRUE) return true;
        esp_task_wdt_reset();
        ms -= slice;
        if (ms == 0) return false;
    }
}

bool USBHostUPS::postQxRequest(QxRequest& req) {
    return _qx_box.post(req, _qx_req_port);
}

bool USBHostUPS::QxRequestPort::takeCaller(uint32_t ms) {
    return takeFeedingWdt(_h._qx_req_mutex, ms);
}

bool USBHostUPS::QxRequestPort::waitDone(uint32_t ms) {
    return takeFeedingWdt(_h._qx_req_done, ms);
}

bool USBHostUPS::QxRequestPort::hasDriver() const {
    return _h._driver && _h._driver->qx();
}

uint32_t USBHostUPS::QxRequestPort::waitMs() const {
    // Under _mutex, with a driver (QxRequestBox::post()): the step running when the request
    // arrives, then the request itself; each step may end one control transfer timeout after
    // its budget (a transfer started just before the deadline), so a late answer is not taken
    // for an abandoned request
    return (qxStepBoundMs(_h._driver->qx()->transportBudgetMs()) + QX_CTRL_TIMEOUT_MS) * 2 + 1000;
}

// In the poll task, without _mutex (ADR 0008): _driver is deleted only by this task, so it
// stays valid for the whole command; the driver takes the lock itself to publish
CommandResult USBHostUPS::QxRequestPort::runCommand(const char* name, const char* param) {
    QxDriver* q = _h._driver ? _h._driver->qx() : nullptr;
    if (!q) return CommandResult::NOT_CONNECTED;
    return q->runCommand(_h._ups_data, name, param);
}

NutTable::SetStatus USBHostUPS::QxRequestPort::runSetVariable(const char* name, const char* value) {
    QxDriver* q = _h._driver ? _h._driver->qx() : nullptr;
    if (!q) return NutTable::SetStatus::NOT_CONNECTED;
    return q->runSetVariable(_h._ups_data, name, value);
}

std::vector<const UPSCommandInfo*> USBHostUPS::driverCommands() const {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const QxDriver* q = _driver ? _driver->qx() : nullptr;
    return q ? q->commands() : std::vector<const UPSCommandInfo*>();
}

CommandResult USBHostUPS::runDriverCommand(const char* name, const char* param) {
    QxRequest req;
    if (!name || strlen(name) >= sizeof(req.name)) return CommandResult::NOT_SUPPORTED;
    if (param && strlen(param) >= sizeof(req.value)) return CommandResult::INVALID_ARGUMENT;
    req.setvar = false;
    snprintf(req.name, sizeof(req.name), "%s", name);
    snprintf(req.value, sizeof(req.value), "%s", param ? param : "");
    if (!postQxRequest(req)) return CommandResult::FAILED;
    return req.cmd;
}

NutTable::SetStatus USBHostUPS::runDriverSetVariable(const char* name, const char* value) {
    QxRequest req;
    if (!name || strlen(name) >= sizeof(req.name)) return NutTable::SetStatus::UNKNOWN;
    if (!value || strlen(value) >= sizeof(req.value)) return NutTable::SetStatus::CONVERSION_FAILED;
    req.setvar = true;
    snprintf(req.name, sizeof(req.name), "%s", name);
    snprintf(req.value, sizeof(req.value), "%s", value);
    if (!postQxRequest(req)) return NutTable::SetStatus::FAILED;
    return req.set;
}

void USBHostUPS::usb_host_lib_task(void *arg) {
    USBHostUPS *self = static_cast<USBHostUPS*>(arg);
    while (self->_usb_task_run) {
        uint32_t event_flags;
        esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                // If there are no clients, we can do usb_host_uninstall() or just delay.
                // But hid_host is a client, so it's fine.
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            // expected timeout
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

void USBHostUPS::logDebug(const String& msg) const {
#ifdef USBUPS_DEBUG_LOG
    if (_log_cb) _log_cb("DEBUG", msg.c_str());
#else
    (void)msg; // keeps the 50-line web log for events that matter (review S1)
#endif
}

void USBHostUPS::populateStringsFromDeviceInfo(const hid_host_dev_info_t& dev_info, uint32_t quirks, UPSData& ups_data) {
    const bool invert = (quirks & QUIRK_INVERT_STRINGS) != 0;
    char buf[HID_STR_DESC_MAX_LENGTH + 1];

    if (DeviceStrings::toAscii(dev_info.iManufacturer, HID_STR_DESC_MAX_LENGTH, invert, buf, sizeof(buf)) > 0) {
        ups_data.set("ups.mfr", buf);
    }
    if (DeviceStrings::toAscii(dev_info.iProduct, HID_STR_DESC_MAX_LENGTH, invert, buf, sizeof(buf)) > 0) {
        ups_data.set("ups.model", buf);
    }
    if (DeviceStrings::toAscii(dev_info.iSerialNumber, HID_STR_DESC_MAX_LENGTH, invert, buf, sizeof(buf)) > 0 &&
        strcmp(buf, "Blank") != 0) {
        ups_data.set("ups.serial", buf);
    }
}

