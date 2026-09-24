#include "BeeperLogic.h"
#include "DeviceStrings.h"
#include "USBHostUPS.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "PowercomDriver.h"
#include "EatonDriver.h"
#include "CyberPowerDriver.h"
#include "OpenUPSDriver.h"
#include <ArduinoJson.h>
#include <stdarg.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

USBHostUPS::USBHostUPS() :
    _usb_task_handle(NULL), _usb_task_run(false),
    _event_queue(NULL), _self_close_handle(NULL), _dropped_events(0), _reported_dropped_events(0),
    _hid_dev_handle(NULL),
    _vid(0), _pid(0),
    _initialized(false), _is_ready_to_poll(false), _device_seen(false),
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

    _initialized = true;
    return true;
}

void USBHostUPS::end() {
    if (!_initialized) return;

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
// loopTask context
// ---------------------------------------------------------------------------

void USBHostUPS::loop() {
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

    // Drivers skip their poll steps while isControlPending() reports a backoff
    _driver->loop(this, _ups_data, now);
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
                    recoverInterface("INPUT transfer error", millis());
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
        if (_vid == 0x051D) { _driver = new APCDriver(); }
        else if (_vid == 0x0764) { _driver = new CyberPowerDriver(); }
        else if (_vid == 0x0463) { _driver = new EatonDriver(); }
        else if (_vid == 0x0d9f) { _driver = new PowercomDriver(); }
        else if (_vid == 0x04D8 && (_pid == 0xD004 || _pid == 0xD005)) { _driver = new OpenUPSDriver(); }
        else { _driver = new GenericDriver(); }

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
    _in_restart_pending = false;
    _in_recoveries = 0;
    hid_host_device_start(_hid_dev_handle);
    _is_ready_to_poll = true;
    _device_seen = true;

    log("INFO", "UPS interface claimed and ready.");
}

void USBHostUPS::processInputReport(const HidEvent& ev) {
    if (ev.handle != _hid_dev_handle || !_driver) return;

    uint8_t r_id = (ev.length > 0) ? ev.data[0] : 0;
    log("INFO", "INPUT_REPORT: id=%d, len=%d", r_id, ev.length);
    _in_recoveries = 0;

#ifdef USBUPS_DEBUG_SLOW_INPUT_MS
    // Issue #47 reproduction aid: slow INPUT processing. Before ADR 0008 this ran inside
    // the HID task callback and turned every overlapping GET_REPORT into a timeout.
    delay(USBUPS_DEBUG_SLOW_INPUT_MS);
#endif

    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (ev.length > 0) {
        uint16_t key = (1 << 8) | r_id; // type 1 = INPUT
        auto& cached = _cached_reports[key];
        cached.report_id = r_id;
        cached.report_type = 1;
        cached.data.assign(ev.data, ev.data + ev.length);
    }
    _driver->decodeReport(this, r_id, 1, ev.data, ev.length, _ups_data);
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

void USBHostUPS::recoverInterface(const char* why, uint32_t now) {
    log("WARN", "[USB] Restarting HID interface: %s", why);
    esp_err_t err = hid_host_device_stop(_hid_dev_handle); // halt + flush + clear of the IN endpoint
    if (err != ESP_OK) log("WARN", "[USB] hid_host_device_stop failed: %s", esp_err_to_name(err));
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

bool USBHostUPS::isControlPending() const {
    return !_link.canPoll(millis());
}

bool USBHostUPS::isDataStale() const {
    uint32_t now = millis();
    if (!_is_ready_to_poll) return LinkMonitor::isStaleWithoutDevice(_device_seen, now, NO_DEVICE_BOOT_GRACE_MS);
    return _link.isStale(now);
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
    return false;
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
    if (_log_cb) _log_cb("DEBUG", msg.c_str());
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

