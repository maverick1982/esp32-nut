#include "BeeperLogic.h"
#include "USBHostUPS.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "PowercomDriver.h"
#include "EatonDriver.h"
#include "CyberPowerDriver.h"
#include "OpenUPSDriver.h"
#include <ArduinoJson.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

USBHostUPS::USBHostUPS() :
    _hid_dev_handle(NULL), _dev_handle(NULL),
    _vid(0), _pid(0),
    _initialized(false), _is_ready_to_poll(false),
    _is_fetching(false), _control_pending(false),
    _driver(nullptr), _log_cb(nullptr), _quirks(0), _usb_task_handle(NULL), _usb_task_run(false)
{
}

USBHostUPS::~USBHostUPS() {
    end();
}

bool USBHostUPS::begin() {
    if (_initialized) return true;

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        if (_log_cb) _log_cb("ERROR", "usb_host_install failed");
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
        .stack_size = 4096,
        .core_id = tskNO_AFFINITY,
        .callback = USBHostUPS::hid_host_driver_event_cb,
        .callback_arg = this
    };

    err = hid_host_install(&hid_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        if (_log_cb) _log_cb("ERROR", "hid_host_install failed");
        return false;
    }

    _initialized = true;
    return true;
}

void USBHostUPS::end() {
    if (!_initialized) return;

    if (_hid_dev_handle) {
        hid_host_device_close(_hid_dev_handle);
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

void USBHostUPS::hid_host_driver_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    USBHostUPS* ups = static_cast<USBHostUPS*>(arg);
    ups->handle_driver_event(hid_device_handle, event);
}

void USBHostUPS::handle_driver_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event) {
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        if (_log_cb) _log_cb("INFO", "HID Device Connected event");

        const hid_host_device_config_t dev_config = {
            .callback = USBHostUPS::hid_host_interface_event_cb,
            .callback_arg = this
        };

        esp_err_t err = hid_host_device_open(hid_device_handle, &dev_config);
        if (err != ESP_OK) {
            if (_log_cb) _log_cb("ERROR", "hid_host_device_open failed");
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _pending_interfaces.push_back(hid_device_handle);
    }
}

void USBHostUPS::hid_host_interface_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    USBHostUPS* ups = static_cast<USBHostUPS*>(arg);
    ups->handle_interface_event(hid_device_handle, event);
}

void USBHostUPS::handle_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event) {
    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (!_driver) return;
        
        size_t length = 0;
        uint8_t data[256];
        if (hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &length) == ESP_OK) {
            uint8_t r_id = (length > 0) ? data[0] : 0;
            char dbg[128];
            snprintf(dbg, sizeof(dbg), "INPUT_REPORT: id=%d, len=%d", r_id, length);
            if (_log_cb) _log_cb("INFO", dbg);
            _driver->decodeReport(this, r_id, 1, data, length, _ups_data);
        }
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        if (_log_cb) _log_cb("INFO", "HID Device Disconnected");
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        hid_host_device_close(hid_device_handle);
        if (_hid_dev_handle == hid_device_handle) {
            _hid_dev_handle = NULL;
            _is_ready_to_poll = false;
            _ups_data = UPSData();
            if (_driver) { delete _driver; _driver = nullptr; }
            _hid_parser = HIDParser();
        }
    }
}

void USBHostUPS::loop() {
    if (!_pending_interfaces.empty()) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_pending_interfaces.empty()) return;
        
        hid_host_device_handle_t handle = _pending_interfaces.front();
        _pending_interfaces.erase(_pending_interfaces.begin());

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

        if (is_ups) {
            if (_hid_dev_handle != NULL) {
                // If we already have a bound UPS interface, keep it or replace it?
                // For now, close the new one if we already have one, OR replace?
                // Let's replace just in case.
                hid_host_device_close(_hid_dev_handle);
            }
            
            _hid_dev_handle = handle;
            _hid_parser = temp_parser;
            
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
                populateStringsFromDeviceInfo(dev_info, _ups_data);
            }
            
            hid_host_device_start(_hid_dev_handle);
            _is_ready_to_poll = true;
            
            if (_log_cb) _log_cb("INFO", "UPS interface claimed and ready.");
        } else {
            // Not a UPS! Close it!
            if (_log_cb) _log_cb("INFO", "Ignoring non-UPS interface.");
            hid_host_device_close(handle);
        }
        return;
    }

    if (!_is_ready_to_poll) return;

    if (_driver) {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _driver->loop(this, _ups_data, millis());
    }
}

bool USBHostUPS::requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length) {
    if (!_is_ready_to_poll || !_hid_dev_handle) return false;
    
    uint8_t data[256];
    size_t length = expected_length > 0 ? expected_length : 255;
    
    esp_err_t err = hid_class_request_get_report(_hid_dev_handle, report_type, report_id, data, &length);
    if (err == ESP_OK && length > 0) {

        
        if (_driver) {
            _driver->decodeReport(this, report_id, report_type, data, length, _ups_data);
        }
        return true;
    } else {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), "requestReport FAILED: type=%d, id=%d, err=0x%x", report_type, report_id, err);
        if (_log_cb) _log_cb("ERROR", dbg);
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
    if (!_is_ready_to_poll) return false;
    
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    String active_path = getActiveBeeperPath();
    if (active_path == "") return false;
    
    const HIDUsageDef* def = nullptr;
    for (const auto& u : _hid_parser.getUsages()) {
        if (u.path == active_path) {
            def = &u;
            break;
        }
    }
    
    if (!def) {
        return false;
    }
    
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));
    size_t fetched_len = sizeof(buffer);
    
    uint8_t rep_type = (def->report_type == 2) ? HID_REPORT_TYPE_OUTPUT : HID_REPORT_TYPE_FEATURE;
    
    // STEP 1: Fetch current report to preserve other fields
    esp_err_t err = hid_class_request_get_report(_hid_dev_handle, rep_type, def->report_id, buffer, &fetched_len);
    
    if (err != ESP_OK || fetched_len == 0) {
        // Fallback for UPSes that reject GET_REPORT on features
        fetched_len = (def->report_id != 0 ? 1 : 0) + (def->bit_offset / 8) + 1;
        if (def->report_id != 0) buffer[0] = def->report_id;
    }
    
    fetched_len = BeeperLogic::manipulateBeeperBuffer(enable, def, buffer, fetched_len, _driver);
    if (fetched_len == 0) return false;
    
    // STEP 3: Write back
    err = hid_class_request_set_report(_hid_dev_handle, rep_type, def->report_id, buffer, fetched_len);
    
    // Update local state immediately to prevent UI bouncing
    if (err == ESP_OK) {
        _ups_data.set("ups.beeper.status", enable ? "enabled" : "disabled");
    }
    
    return err == ESP_OK;
}

bool USBHostUPS::isConnected() const {
    return _is_ready_to_poll;
}

String USBHostUPS::getActiveBeeperPath() const {
    for (const auto& u : _hid_parser.getUsages()) {
        if (u.path == "UPS.PowerSummary.AudibleAlarmControl" || 
            u.path == "UPS.BatterySystem.Battery.AudibleAlarmControl" || 
            u.path == "UPS.AudibleAlarmControl") {
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

void USBHostUPS::populateStringsFromDeviceInfo(const hid_host_dev_info_t& dev_info, UPSData& ups_data) {
    char buf[128];
    wcstombs(buf, dev_info.iManufacturer, sizeof(buf));
    if (String(buf).length() > 0) ups_data.set("ups.mfr", String(buf));
    
    wcstombs(buf, dev_info.iProduct, sizeof(buf));
    if (String(buf).length() > 0) ups_data.set("ups.model", String(buf));
    
    wcstombs(buf, dev_info.iSerialNumber, sizeof(buf));
    if (String(buf).length() > 0 && String(buf) != "Blank") ups_data.set("ups.serial", String(buf));
}


