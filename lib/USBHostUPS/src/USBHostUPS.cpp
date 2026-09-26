#include "BeeperLogic.h"
#include "DeviceStrings.h"
#include "USBHostUPS.h"
#include "IUPSDriver.h"
#include "DriverRegistry.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include "esp_task_wdt.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

USBHostUPS::USBHostUPS() :
    _usb_task_handle(NULL), _usb_task_run(false),
    _poll_task_handle(NULL), _poll_task_run(false), _op_mutex(NULL),
    _event_queue(NULL), _self_close_handle(NULL), _dropped_events(0), _reported_dropped_events(0),
    _hid_dev_handle(NULL),
    _vid(0), _pid(0),
    _initialized(false), _is_ready_to_poll(false), _device_seen(false),
    _ep_in_mps(0), _uses_report_ids(false), _lang_id(0), _failed_strings{},
    _stat_since(0), _stat_input(0), _stat_in_packets(0), _stat_get_ok(0), _stat_get_failed(0),
    _in_restart_pending(false), _in_start_attempts(0), _in_restart_at(0), _in_recoveries(0),
    _restart_requested(false), _restart_reason(""),
    _driver(nullptr), _log_cb(nullptr), _quirks(0)
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

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
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

    while (self->_poll_task_run) {
        if (wdt) esp_task_wdt_reset();
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

    if (_restart_requested) return;

    uint32_t now = millis();
    if (_in_restart_pending) {
        retryInterfaceStart(now);
        return;
    }

    if (!_is_ready_to_poll || !_driver) return;

    switch (_link.tick(now)) {
    case LinkMonitor::Action::RECOVER:
        recoverInterface("control pipe not answering", now);
        return;
    case LinkMonitor::Action::RESTART:
        requestRestart("control pipe not answering after recovery");
        return;
    default:
        break;
    }

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

    logStats(now);

    // Drivers skip their poll steps while isPollingPaused() reports a backoff
    _driver->loop(this, _ups_data, now);
}

void USBHostUPS::logStats(uint32_t now) {
    if ((now - _stat_since) < STATS_PERIOD_MS) return;
    if (_stat_input || _stat_in_packets || _stat_get_ok || _stat_get_failed) {
        log("INFO", "[USB] Last %u s: %u INPUT reports (%u packets), %u GET_REPORT ok, %u failed",
            (unsigned)((now - _stat_since) / 1000), (unsigned)_stat_input, (unsigned)_stat_in_packets,
            (unsigned)_stat_get_ok, (unsigned)_stat_get_failed);
    }
    _stat_since = now;
    _stat_input = _stat_in_packets = _stat_get_ok = _stat_get_failed = 0;
}

void USBHostUPS::processEvents() {
    if (!_event_queue) return;

    HidEvent ev;
    for (UBaseType_t i = 0; i < EVENT_QUEUE_LEN && xQueueReceive(_event_queue, &ev, 0) == pdTRUE; i++) {
        switch (ev.type) {
        case HidEvent::CONNECTED:
            log("INFO", "HID Device Connected event");
            claimInterface(ev.handle);
            break;
        case HidEvent::OPEN_FAILED:
            log("ERROR", "hid_host_device_open failed");
            break;
        case HidEvent::INPUT_REPORT:
            processInputReport(ev);
            break;
        case HidEvent::DISCONNECTED:
            handleDisconnected(ev.handle);
            break;
        case HidEvent::TRANSFER_ERROR:
            if (ev.handle == _hid_dev_handle && _is_ready_to_poll && !_in_restart_pending && !_restart_requested) {
                if (++_in_recoveries > MAX_IN_RECOVERIES) {
                    requestRestart("INPUT transfer keeps failing");
                } else {
                    recoverInterface("INPUT transfer error", millis(), true);
                }
            }
            break;
        }
    }

    uint32_t dropped = _dropped_events;
    if (dropped != _reported_dropped_events) {
        log("WARN", "HID event queue full: %u INPUT reports dropped so far", (unsigned)dropped);
        _reported_dropped_events = dropped;
    }
}

void USBHostUPS::claimInterface(hid_host_device_handle_t handle) {
    size_t desc_len = 0;
    uint8_t *desc = hid_host_get_report_descriptor(handle, &desc_len);

    bool is_ups = false;
    HIDParser temp_parser;
    if (desc) {
        temp_parser.parseReportDescriptor(desc, desc_len);
        for (const auto& u : temp_parser.getUsages()) {
            if ((u.usage & 0xFFFF0000) == HID_PAGE_UPS || (u.usage & 0xFFFF0000) == HID_PAGE_BATTERY) {
                is_ups = true;
                break;
            }
        }
    }

    if (!is_ups) {
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
    _hid_parser = temp_parser;
    _cached_reports.clear();

    _cached_report_descriptor_hex = "";
    for (size_t i = 0; i < desc_len; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", desc[i]);
        _cached_report_descriptor_hex += hex;
    }

    hid_host_dev_info_t dev_info;
    if (hid_host_get_device_info(handle, &dev_info) == ESP_OK) {
        _vid = dev_info.VID;
        _pid = dev_info.PID;

        if (_driver) { delete _driver; _driver = nullptr; }
        _driver = DriverRegistry::create(_vid, _pid);

        _quirks = 0;
        for (int q = 0; UPS_QUIRKS[q].vid != 0; q++) {
            if (UPS_QUIRKS[q].vid == _vid && (UPS_QUIRKS[q].pid == 0xFFFF || UPS_QUIRKS[q].pid == _pid)) {
                _quirks |= UPS_QUIRKS[q].flags;
            }
        }
        _driver->setup();
        populateStringsFromDeviceInfo(dev_info, _quirks, _ups_data);
    }

    _link.reset(millis());
    _link.setCarriesData(!(_quirks & QUIRK_NO_GET_REPORT));
    _in_wd.reset(millis());
    _in_reasm.reset();
    _ep_in_mps = hid_host_device_get_ep_in_mps(handle);
    _uses_report_ids = _hid_parser.usesReportIds();
    // String indices, so the drivers can fetch a string the USB Host library missed (review M5)
    _iManufacturer = _iProduct = _iSerialNumber = 0;
    hid_host_device_get_string_indices(handle, &_iManufacturer, &_iProduct, &_iSerialNumber);
    _lang_id = 0;
    memset(_failed_strings, 0, sizeof(_failed_strings));
    _in_restart_pending = false;
    _in_recoveries = 0;
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
    _device_seen = true;

    log("INFO", "UPS interface claimed and ready (IN endpoint MPS %u).", (unsigned)_ep_in_mps);
}

void USBHostUPS::processInputReport(const HidEvent& ev) {
    if (ev.handle != _hid_dev_handle || !_driver) return;

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

    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (length > 0) {
        uint16_t key = (1 << 8) | r_id; // type 1 = INPUT
        auto& cached = _cached_reports[key];
        cached.report_id = r_id;
        cached.report_type = 1;
        cached.data.assign(data, data + length);
    }
    _driver->decodeReport(this, r_id, 1, data, length, _ups_data);
}

void USBHostUPS::handleDisconnected(hid_host_device_handle_t handle) {
    // The first hid_host_device_close() (ours, or the HID driver's on physical removal)
    // raised this event; this second one releases the interface.
    hid_host_device_close(handle);
    if (_hid_dev_handle != handle) return;

    log("INFO", "HID Device Disconnected");
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _hid_dev_handle = NULL;
    _is_ready_to_poll = false;
    _in_restart_pending = false;
    _ups_data = UPSData();
    if (_driver) { delete _driver; _driver = nullptr; }
    _hid_parser = HIDParser();
    _cached_reports.clear();
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
    // A STALL or an oversized answer still proves the device is talking to us
    if (err == ESP_OK || err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_INVALID_SIZE) {
        if (_link.failures() > 0) log("INFO", "[USB] Control pipe answering again");
        _link.onAlive(now);
    } else {
        _link.onLinkFailure(now);
    }
}

void USBHostUPS::recoverInterface(const char* why, uint32_t now, bool clear_in_halt) {
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
    if (_driver) _driver->setup(); // restart the poll cycle from scratch
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

bool USBHostUPS::isPollingPaused() const {
    return !_link.canPoll(millis());
}

bool USBHostUPS::isDataStale() const {
    uint32_t now = millis();
    if (!_is_ready_to_poll) return LinkMonitor::isStaleWithoutDevice(_device_seen, now, NO_DEVICE_BOOT_GRACE_MS);
    // Recovery exhausted: nothing refreshes the values while the restart waits (review A7)
    if (_restart_requested) return true;
    return _link.isStale(now) || _in_wd.isStale(now);
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
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        uint16_t key = (report_type << 8) | report_id;
        auto& cached = _cached_reports[key];
        cached.report_id = report_id;
        cached.report_type = report_type;
        cached.data.assign(_request_buffer, _request_buffer + length);

        if (_driver) {
            _driver->decodeReport(this, report_id, report_type, _request_buffer, length, _ups_data);
        }
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
    if (_driver) _driver->parseStringDescriptor(this, string_index, _request_buffer, len, _ups_data);
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

bool USBHostUPS::setBeeperLocked(bool enable) {
    if (!_is_ready_to_poll || !_hid_dev_handle) return false;
    if (!_link.canPoll(millis())) return false;

    HIDUsageDef def;
    uint16_t expected_length = 0;
    bool shared_report = true;
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        String active_path = getActiveBeeperPath();
        if (active_path == "") return false;

        const HIDUsageDef* found = nullptr;
        for (const auto& u : _hid_parser.getUsages()) {
            if (strcmp(u.path, active_path.c_str()) == 0) {
                found = &u;
                break;
            }
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

    fetched_len = BeeperLogic::manipulateBeeperBuffer(enable, &def, _request_buffer, fetched_len, _driver);
    if (fetched_len == 0) return false;

    // STEP 3: Write back
    err = hid_class_request_set_report(_hid_dev_handle, rep_type, def.report_id, _request_buffer, fetched_len);
    noteControlResult(err, millis());

    // Update local state immediately to prevent UI bouncing
    if (err == ESP_OK) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _ups_data.set("ups.beeper.status", enable ? "enabled" : "disabled");
    }

    return err == ESP_OK;
}

bool USBHostUPS::isConnected() const {
    return _is_ready_to_poll;
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
    return getActiveBeeperPath() != "";
}

String USBHostUPS::dumpUSBDiagnostics() {
    JsonDocument doc;

    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        doc["firmware_version"] = FIRMWARE_VERSION;
        doc["vid"] = String(_vid, HEX);
        doc["pid"] = String(_pid, HEX);
        doc["manufacturer"] = _ups_data.get("ups.mfr");
        doc["product"] = _ups_data.get("ups.model");
        doc["serial_number"] = _ups_data.get("ups.serial");

        // Format hex string as a custom 10-items-per-line JSON Array
        String arrStr = "[\n";
        if (_cached_report_descriptor_hex.length() > 0) {
            for (size_t i = 0; i < _cached_report_descriptor_hex.length(); i += 2) {
                if (i > 0 && (i / 2) % 10 == 0) arrStr += ",\n    ";
                else if (i > 0) arrStr += ", ";
                else arrStr += "    ";

                String byteStr = _cached_report_descriptor_hex.substring(i, i+2);
                byteStr.toUpperCase();
                arrStr += "\"0x" + byteStr + "\"";
            }
        }
        arrStr += "\n  ]";
        doc["report_descriptor_hex"] = serialized(arrStr);

        doc["quirks"] = _quirks;
        doc["driver"] = _driver ? _driver->getDriverName() : "None";

        JsonObject link = doc["link"].to<JsonObject>();
        link["failures"] = _link.failures();
        link["recoveries"] = _link.recoveries();
        link["stale"] = isDataStale();
        link["dropped_input_reports"] = (uint32_t)_dropped_events;
        link["input_period_ms"] = _in_wd.isPeriodic() ? _in_wd.periodMs() : 0;
        link["input_recoveries"] = _in_wd.recoveries();
        link["incomplete_input_reports"] = _in_reasm.dropped();
        link["ep_in_mps"] = _ep_in_mps;

        JsonArray scenarios = doc["scenarios"].to<JsonArray>();
        JsonObject scenario = scenarios.add<JsonObject>();
        scenario["description"] = "Live ESP32 dump";

        JsonArray reports = scenario["reports"].to<JsonArray>();
        for (const auto& kv : _cached_reports) {
            JsonObject report = reports.add<JsonObject>();
            report["id"] = kv.second.report_id;
            report["type"] = kv.second.report_type;

            String dataStr = "[";
            for (size_t i = 0; i < kv.second.data.size(); ++i) {
                if (i > 0) dataStr += ", ";
                dataStr += String(kv.second.data[i]);
            }
            dataStr += "]";
            report["data"] = serialized(dataStr);
        }

        JsonObject expectedData = scenario["expected_ups_data"].to<JsonObject>();
        for (const auto& param : _ups_data.getAll()) {
            expectedData[param.key] = param.value;
        }
    }

    String output;
    serializeJsonPretty(doc, output);
    return output;
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

