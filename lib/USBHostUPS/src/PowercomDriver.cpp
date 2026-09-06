/**
 * @brief Powercom Driver Implementation
 * 
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for Powercom HID devices.
 * - Reference: nut_repo/drivers/powercom-hid.c
 * - Report 0xA4 / 0x0A Quirks: Uses powercom_poll_0xa4 and powercom_poll_0x0a structures.
 * - Standard Field Overrides: Reverts PDC mapped voltage/beeper fields as Powercom sends garbage values.
 */

#include "PowercomDriver.h"
#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "Quirks.h"
#include <cctype>

PowercomDriver::PowercomDriver() : 
    _last_fast_poll(0),
    _last_step_time(0),
    _last_0xa4_poll(0),
    _poll_step(0),
    _slow_poll_counter(0),
    _mfr_retries(0),
    _prod_retries(0),
    _serial_retries(0) {
}

void PowercomDriver::setup() {
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _last_0xa4_poll = 0;
    _slow_poll_counter = 14;
    _mfr_retries = 0;
    _prod_retries = 0;
    _serial_retries = 0;
    Serial.println("[PowercomDriver] Setup completed (NUT 2.0s Quick-Poll & 30s Full-Poll mode).");
}

void PowercomDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (data.get("ups.type") != "Powercom") {
        data.set("ups.type", "Powercom");
    }
    if (!data.hasKey("ups.mfr")) {
        data.set("ups.mfr", "Powercom");
    }

    if (!data.hasKey("ups.model")) {
        uint16_t pid = host->getPID();
        switch (pid) {
            case 0x00a2: data.set("ups.model", "IMPERIAL Series"); break;
            case 0x00a3: data.set("ups.model", "Smart King Pro"); break;
            case 0x00a4: data.set("ups.model", "WOW Series"); break;
            case 0x00a5: data.set("ups.model", "Vanguard Series"); break;
            case 0x00a6: data.set("ups.model", "Black Knight Pro"); break;
            case 0x0004: data.set("ups.model", "SPD / Vanguard / BNT"); break;
            case 0x0001: data.set("ups.model", "Powercom UPS"); break;
            default:     data.set("ups.model", "Powercom HID UPS"); break;
        }
    }

    // Quick-Poll: Every 2.0s trigger status keep-alive (Report 0x0A)
    // Full-Poll: Every 30.0s query voltages and load (Reports 0x1D, 0x21, 0x1F)
    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
            _last_fast_poll = (now != 0) ? now : 1;
            _poll_step = 1;
            _last_step_time = now;

            _slow_poll_counter++;
            if (_slow_poll_counter >= 15) { // 30s / 2s = 15
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (host->isControlPending()) return; // Non-overlapping guard

        if (now - _last_step_time >= 50 || _poll_step == 1) {
            _last_step_time = now;

            if (_poll_step == 1) {
                // Step 1: Quick-Poll (Report 0x0A / ACPresent)
                host->requestReport(0x0A, 3, 8);
            } else if (_slow_poll_counter == 0) {
                // Steps 2..4 only during the 30s cycle
                if (_poll_step == 2) {
                    host->requestReport(0x1D, 3, 8); // input.voltage
                } else if (_poll_step == 3) {
                    host->requestReport(0x21, 3, 8); // output.voltage
                } else if (_poll_step == 4) {
                    host->requestReport(0x1F, 3, 8); // ups.load
                } else {
                    _poll_step = 0;
                    return;
                }
            } else {
                _poll_step = 0;
                return;
            }
            _poll_step++;
        }
    }
}

void PowercomDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    if (report_type == 1) { // Interrupt IN report
        String hex = "";
        for (size_t i = 0; i < length && i < 16; i++) {
            char b[4];
            sprintf(b, "%02X ", data[i]);
            hex += b;
        }
        Serial.printf("[PowercomDriver] INT IN (len %d, id 0x%02X): %s\n", (int)length, report_id, hex.c_str());
    }

    // Save fields before GenericDriver so Powercom custom mappings can handle them
    String saved_voltage = ups_data.get("battery.voltage");
    bool saved_has_voltage = ups_data.hasKey("battery.voltage");
    String saved_beeper = ups_data.get("ups.beeper.status");
    bool saved_has_beeper = ups_data.hasKey("ups.beeper.status");

    // Run GenericDriver first for default PDC mappings
    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    // NUT completely ignores standard voltage fields for Powercom because they are often broken/garbage
    if (saved_has_voltage) ups_data.set("battery.voltage", saved_voltage);
    else ups_data.remove("battery.voltage");
    if (saved_has_beeper) ups_data.set("ups.beeper.status", saved_beeper);
    else ups_data.remove("ups.beeper.status");

    if (report_id == 0xA4 && report_type == 3) {
        String msg = "";
        // Check if report ID is prepended at data[0] or if payload starts directly at data[0]
        size_t start_idx = (data[0] == 0xA4) ? 1 : 0;
        for (size_t i = start_idx; i < length && i < 8; i++) {
            msg += (char)data[i];
        }
        Serial.printf("[PowercomDriver] 0xA4 raw bytes: %d, text: '%s'\n", (int)length, msg.c_str());

        int start = -1;
        for (int i = 0; i < msg.length(); i++) {
            if (std::isdigit((unsigned char)msg[i]) || msg[i] == '.') {
                start = i;
                break;
            }
        }
        if (start >= 0) {
            String valStr = "";
            for (int i = start; i < msg.length(); i++) {
                if (std::isdigit((unsigned char)msg[i]) || msg[i] == '.') {
                    valStr += msg[i];
                } else {
                    break;
                }
            }
            if (valStr.length() > 0 && valStr.indexOf('.') != -1) {
                ups_data.set("battery.voltage", String(valStr.toFloat(), 2));
            }
        }
        return;
    }

    struct Mapping {
        const char* path;
        void (*apply)(PowercomDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.RemainingCapacity", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.charge", String((int)(v > 100 ? 100 : v))); } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.runtime", String((int)v)); } },
        { "UPS.Battery.RunTimeToEmpty", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.runtime", String((int)v)); } },
        { "UPS.PowerSummary.AudibleAlarmControl", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if ((int)v == 1) { d.set("ups.beeper.status", "enabled"); } // Powercom NUT: 1 = enabled
            else if ((int)v == 2) { d.set("ups.beeper.status", "disabled"); } // Powercom NUT: 2 = disabled
            else { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
        } },
        { "UPS.AudibleAlarmControl", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if ((int)v == 1) { d.set("ups.beeper.status", "enabled"); } 
            else if ((int)v == 2) { d.set("ups.beeper.status", "disabled"); } 
            else { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
        } }
    };

    for (const auto& u : host->getUsages()) {
        if (u.report_id != report_id || u.report_type != report_type) continue;
        double val = HIDParser::extractUsage(&u, report_id, data, length);
        
        for (const auto& m : mappings) {
            if (strcmp(u.path.c_str(), m.path) == 0) {
                m.apply(this, ups_data, val, &u);
                break; // handled by string mappings
            }
        }
        
        // Match by usage instead of full path because Powercom uses non-standard Usage Page 0x0002
        if (u.usage == 0x00020030) { // Voltage
            if (u.path.indexOf("0x0002001A") >= 0) { ups_data.set("input.voltage", String(val * 4.0f, 1)); } // Input
            else if (u.path.indexOf("0x0002001C") >= 0) { ups_data.set("output.voltage", String(val * 4.0f, 1)); } // Output
        }
        else if (u.usage == 0x00020035) { // PercentLoad
            if (u.path.indexOf("0x0002001C") >= 0) { ups_data.set("ups.load", String((int)val)); }
        }
        else if (u.usage == 0x00020081) { // InternalChargeController (Status bits 1)
            uint32_t bitmask = (uint32_t)val;
            ups_data.set("ups.status.internal_failure", (bitmask & 0x01) != 0 ? "1" : "0");
            ups_data.set("ups.status.replace_battery", (bitmask & 0x02) != 0 ? "1" : "0");
            ups_data.set("ups.status.shutdown_imminent", (bitmask & 0x10) != 0 ? "1" : "0");
        }
        else if (u.usage == 0x00020082) { // PrimaryBatterySupport (Status bits 2)
            uint32_t bitmask = (uint32_t)val;
            ups_data.set("ups.status.ac_present", (bitmask & 0x01) == 0 ? "1" : "0"); // 1 = line fail
            ups_data.set("ups.status.discharging", (bitmask & 0x01) != 0 ? "1" : "0"); 
            ups_data.set("ups.status.battery_low", (bitmask & 0x02) != 0 ? "1" : "0");
            ups_data.set("ups.status.overload", (bitmask & 0x20) != 0 ? "1" : "0");
        }
        else if (u.usage == 0x00020032) { // Frequency
            // Could map to input or output depending on collection
        }
        else if (u.usage == 0x00020057) { // DelayBeforeShutdown
            uint16_t i = (uint16_t)val;
            int32_t delay = 60 * (i >> 8) + (i & 0x00FF);
            ups_data.set("ups.delay.shutdown", String(delay));
            ups_data.set("ups.timer.shutdown", String(delay));
        }
        else if (u.usage == 0x00020083) { // DesignCapacity
            ups_data.set("battery.capacity", String((int)(val / 3600.0)));
        }
    }
}

void PowercomDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    
    bool invert = false;
    if (host && (host->getQuirks() & QUIRK_INVERT_STRINGS)) {
        invert = true;
    }
    
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) {
            char c = (char)data[i];
            if (invert) c = ~c;
            str += c;
        }
    }
    if (index == host->_iManufacturer) ups_data.set("ups.mfr", str);
    else if (index == host->_iProduct) ups_data.set("ups.model", str);
    else if (host->_iSerialNumber > 0 && index == host->_iSerialNumber) ups_data.set("ups.serial", str);
}

String PowercomDriver::fetchVoltageHack(IUSBHostUPS* host) {
    return "";
}

uint8_t PowercomDriver::encodeBeeperValue(bool enable, uint16_t bit_size) const {
    if (bit_size == 1) return enable ? 1 : 0;
    return enable ? 1 : 2; // Powercom protocol: 1 = enable, 2 = disable
}
