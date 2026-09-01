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
    _slow_poll_counter(0) {
}

void PowercomDriver::setup() {
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _last_0xa4_poll = 0;
    _slow_poll_counter = 14;
    Serial.println("[PowercomDriver] Setup started.");
}

void PowercomDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (data.upsType != "Powercom") {
        data.upsType = "Powercom";
    }

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
            
            _slow_poll_counter++;
            if (_slow_poll_counter >= 15) { // 30s / 2s = 15
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (host->isControlPending()) return;

        if (now - _last_step_time >= 50 || _poll_step == 1) {
            _last_step_time = now;
            
            if (_poll_step == 1) {
                if (_slow_poll_counter == 0 && data.manufacturer == "") if (host->_iManufacturer > 0) host->requestStringDescriptor(host->_iManufacturer);
            } else if (_poll_step == 2) {
                if (_slow_poll_counter == 0 && data.product == "") if (host->_iProduct > 0) host->requestStringDescriptor(host->_iProduct);
            } else if (_poll_step == 3) {
                if (_slow_poll_counter == 0 && data.serialNumber == "") if (host->_iSerialNumber > 0) host->requestStringDescriptor(host->_iSerialNumber);
            } else {
                const auto& usages = host->getUsages();
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    if (u.report_type == 2) continue; // Skip OUTPUT reports
                    uint16_t pair = (u.report_type << 8) | u.report_id;
                    bool found = false;
                    for (uint16_t id : rids) {
                        if (id == pair) { found = true; break; }
                    }
                    if (!found) rids.push_back(pair);
                }
                for (auto it = rids.begin(); it != rids.end(); ) {
                    if ((*it >> 8) == 1) { // If Input report
                        uint8_t id = *it & 0xFF;
                        bool has_feature = false;
                        for (uint16_t pair : rids) {
                            if ((pair >> 8) == 3 && (pair & 0xFF) == id) { has_feature = true; break; }
                        }
                        if (has_feature) {
                            it = rids.erase(it);
                            continue;
                        }
                    }
                    ++it;
                }
                
                int index = _poll_step - 4;
                if (index >= 0 && index < rids.size()) {
                    uint8_t r_type = rids[index] >> 8;
                    uint8_t r_id = rids[index] & 0xFF;
                    if (r_id >= 0xA0 && r_id <= 0xAF) {
                        host->requestReport(r_id, r_type, 8);
                    } else {
                        host->requestReport(r_id, r_type, 64);
                    }
                } else if (index == rids.size()) {
                    // Feature Report 0xA4 for Powercom battery voltage hack
                    host->requestReport(0xA4, 3, 8);
                } else {
                    _poll_step = 0;
                    return;
                }
            }
            _poll_step++;
        }
    }
}

void PowercomDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    // Save fields before GenericDriver so Powercom custom mappings can handle them
    float saved_voltage = ups_data.batteryVoltage;
    bool saved_has_voltage = ups_data.has.batteryVoltage;
    bool saved_beeper = ups_data.beeperEnabled;
    bool saved_has_beeper = ups_data.has.beeperEnabled;

    // Run GenericDriver first for default PDC mappings
    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    // NUT completely ignores standard voltage fields for Powercom because they are often broken/garbage
    ups_data.batteryVoltage = saved_voltage;
    ups_data.has.batteryVoltage = saved_has_voltage;
    ups_data.beeperEnabled = saved_beeper;
    ups_data.has.beeperEnabled = saved_has_beeper;

    if (report_id == 0xA4 && report_type == 3) {
        String msg = "";
        for (size_t i = 1; i < length && i < 8; i++) {
            msg += (char)data[i];
        }
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
                ups_data.has.batteryVoltage = true;
                ups_data.batteryVoltage = valStr.toFloat();
            }
        }
        return;
    }

    struct Mapping {
        const char* path;
        void (*apply)(PowercomDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.RemainingCapacity", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.remainingCapacity = true; d.remainingCapacity = v > 100 ? 100 : (uint8_t)v; } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.runTimeToEmpty = true; d.runTimeToEmpty = (uint32_t)v; } },
        { "UPS.Battery.RunTimeToEmpty", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.runTimeToEmpty = true; d.runTimeToEmpty = (uint32_t)v; } },
        { "UPS.PowerSummary.AudibleAlarmControl", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef* def) { 
            d.has.beeperEnabled = true;
            if (def && def->bit_size == 1) { d.beeperEnabled = (v != 0); }
            else if ((int)v == 1) { d.beeperEnabled = true; } // Powercom NUT: 1 = enabled
            else if ((int)v == 2) { d.beeperEnabled = false; } // Powercom NUT: 2 = disabled
            else { d.beeperEnabled = (v != 0); }
        } },
        { "UPS.AudibleAlarmControl", [](PowercomDriver*, UPSData& d, double v, const HIDUsageDef* def) { 
            d.has.beeperEnabled = true;
            if (def && def->bit_size == 1) { d.beeperEnabled = (v != 0); }
            else if ((int)v == 1) { d.beeperEnabled = true; } 
            else if ((int)v == 2) { d.beeperEnabled = false; } 
            else { d.beeperEnabled = (v != 0); }
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
            if (u.path.indexOf("0x0002001A") >= 0) { ups_data.has.inputVoltage = true; ups_data.inputVoltage = val * 4.0; } // Input
            else if (u.path.indexOf("0x0002001C") >= 0) { ups_data.has.outputVoltage = true; ups_data.outputVoltage = val * 4.0; } // Output
        }
        else if (u.usage == 0x00020035) { // PercentLoad
            if (u.path.indexOf("0x0002001C") >= 0) { ups_data.has.load = true; ups_data.load = (uint8_t)val; }
        }
        else if (u.usage == 0x00020081) { // InternalChargeController (Status bits 1)
            uint32_t bitmask = (uint32_t)val;
            ups_data.internalFailure = (bitmask & 0x01) != 0;
            ups_data.needReplacement = (bitmask & 0x02) != 0;
            ups_data.shutdownImminent = (bitmask & 0x10) != 0;
        }
        else if (u.usage == 0x00020082) { // PrimaryBatterySupport (Status bits 2)
            uint32_t bitmask = (uint32_t)val;
            ups_data.acPresent = (bitmask & 0x01) == 0; // 1 = line fail
            ups_data.discharging = (bitmask & 0x01) != 0; 
            ups_data.belowRemainingCapacityLimit = (bitmask & 0x02) != 0;
            ups_data.overload = (bitmask & 0x20) != 0;
        }
        else if (u.usage == 0x00020032) { // Frequency
            // Could map to input or output depending on collection
        }
        else if (u.usage == 0x00020057) { // DelayBeforeShutdown
            uint16_t i = (uint16_t)val;
            ups_data.has.delayShutdown = true;
            ups_data.delayShutdown = 60 * (i >> 8) + (i & 0x00FF);
            ups_data.has.timerShutdown = true;
            ups_data.timerShutdown = ups_data.delayShutdown;
        }
        else if (u.usage == 0x00020083) { // DesignCapacity
            ups_data.has.designCapacity = true;
            ups_data.designCapacity = (uint8_t)(val / 3600.0);
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
    if (index == host->_iManufacturer) ups_data.manufacturer = str;
    else if (index == host->_iProduct) ups_data.product = str;
    else if (host->_iSerialNumber > 0 && index == host->_iSerialNumber) ups_data.serialNumber = str;
}

String PowercomDriver::fetchVoltageHack(IUSBHostUPS* host) {
    return "";
}

uint8_t PowercomDriver::encodeBeeperValue(bool enable, uint16_t bit_size) const {
    if (bit_size == 1) return enable ? 1 : 0;
    return enable ? 1 : 2; // Powercom protocol: 1 = enable, 2 = disable
}
