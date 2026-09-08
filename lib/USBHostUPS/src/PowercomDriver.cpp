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
    _last_0xa4_poll(0),
    _mfr_retries(0),
    _prod_retries(0),
    _serial_retries(0) {
}

void PowercomDriver::setup() {
    GenericDriver::setup();
    _last_0xa4_poll = 0;
    _mfr_retries = 0;
    _prod_retries = 0;
    _serial_retries = 0;
    Serial.println("[PowercomDriver] Setup completed (NUT 2.0s Quick-Poll & 30s Full-Poll mode).");
}

void PowercomDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;
    _current_pid = host->getPID();

    if (data.get("ups.type") != "Powercom") {
        data.set("ups.type", "Powercom");
    }
    if (!data.hasKey("ups.mfr")) {
        data.set("ups.mfr", "POWERCOM Co.,LTD");
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

    // Delegating dynamic PDC walk to GenericDriver (identical to NUT hid_ups_walk)
    GenericDriver::loop(host, data, now);

    // Legacy Powercom (0xA4 report) fallback polling
    // Older Powercom models don't use standard PDC reports and only expose an 0xA4 report.
    // We poll this once every getFullPollIntervalMs() (or on first run).
    if (_poll_step == 0 && (now - _last_0xa4_poll >= getFullPollIntervalMs() || _last_0xa4_poll == 0)) {
        if (!host->isControlPending() && (now - _last_step_time >= getPollPacingMs())) {
            _last_0xa4_poll = now != 0 ? now : 1;
            _last_step_time = now; // Sync with pacing grid
            host->requestReport(0xA4, 3, 8);
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
    // Powercom SPD-750U hardware descriptor defines telemetry as Feature reports (Type 3).
    // Since it streams them over Interrupt IN (Type 1), we override the type to let GenericDriver match them.
    uint8_t effective_type = report_type;
    if (_current_pid == 0x0004 && report_type == 1 && report_id != 0) {
        effective_type = 3;
    }

    GenericDriver::decodeReport(host, report_id, effective_type, data, length, ups_data);

    // NUT completely ignores standard voltage fields for Powercom because they are often broken/garbage
    if (saved_has_voltage) ups_data.set("battery.voltage", saved_voltage);
    else ups_data.remove("battery.voltage");
    if (saved_has_beeper) ups_data.set("ups.beeper.status", saved_beeper);
    else ups_data.remove("ups.beeper.status");

    if (report_id == 0xA4 && effective_type == 3) {
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
        { "UPS.PowerSummary.AudibleAlarmControl", [](PowercomDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && drv->_active_beeper.length() > 0 && def->path != drv->_active_beeper) return;
            if (v == 0 && d.hasKey("ups.beeper.status")) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if ((int)v == 1) { d.set("ups.beeper.status", "enabled"); } // Powercom NUT: 1 = enabled
            else if ((int)v == 2) { d.set("ups.beeper.status", "disabled"); } // Powercom NUT: 2 = disabled
            else { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
        } },
        { "UPS.AudibleAlarmControl", [](PowercomDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && drv->_active_beeper.length() > 0 && def->path != drv->_active_beeper) return;
            if (v == 0 && d.hasKey("ups.beeper.status")) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if ((int)v == 1) { d.set("ups.beeper.status", "enabled"); } 
            else if ((int)v == 2) { d.set("ups.beeper.status", "disabled"); } 
            else { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
        } }
    };

    for (const auto& u : host->getUsages()) {
        if (u.report_id != report_id || u.report_type != effective_type) continue;
        double val = HIDParser::extractUsage(&u, report_id, data, length);
        
        for (const auto& m : mappings) {
            if (u.path == String(m.path)) {
                m.apply(this, ups_data, val, &u);
                break;
            }
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

uint32_t PowercomDriver::getPollPacingMs() { return (_current_pid == 0x0004) ? 800 : 0; }
uint32_t PowercomDriver::getFastPollIntervalMs() { return (_current_pid == 0x0004) ? 5000 : 2000; }
bool PowercomDriver::shouldPollUsage(const String& path) {
    if (_current_pid != 0x0004) return true;
    return false; // For SPD-750U, rely 100% on Interrupt IN and 0xA4 legacy string. Get_Report crashes the UPS.
}


