#include "CyberPowerDriver.h"
#include "USBHostUPS.h"

CyberPowerDriver::CyberPowerDriver() : 
    _last_poll(0), 
    _last_fast_poll(0), 
    _last_step_time(0), 
    _poll_step(0),
    _slow_poll_counter(0) {
}

void CyberPowerDriver::setup() {
    _last_poll = 0;
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _slow_poll_counter = 0;
    Serial.println("[CyberPowerDriver] Setup started.");
}

void CyberPowerDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

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
        if (now - _last_step_time >= 50 || _poll_step == 1) {
            _last_step_time = now;
            
            if (_poll_step == 1) {
                if (_slow_poll_counter == 0 && data.manufacturer == "") host->requestStringDescriptor(1);
            } else if (_poll_step == 2) {
                if (_slow_poll_counter == 0 && data.product == "") host->requestStringDescriptor(2);
            } else if (_poll_step == 3) {
                if (_slow_poll_counter == 0 && data.serialNumber == "") host->requestStringDescriptor(3);
            } else {
                const auto& usages = host->_hid_parser.getUsages();
                std::vector<uint8_t> rids;
                for (const auto& u : usages) {
                    bool found = false;
                    for (uint8_t id : rids) {
                        if (id == u.report_id) { found = true; break; }
                    }
                    if (!found) rids.push_back(u.report_id);
                }
                
                int index = _poll_step - 4;
                if (index >= 0 && index < rids.size()) {
                    host->requestReport(rids[index], 0x03, 16);
                } else {
                    _poll_step = 0;
                    return;
                }
            }
            _poll_step++;
        }
    }
}

void CyberPowerDriver::decodeReport(USBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    struct Mapping {
        uint32_t usage;
        void (*apply)(UPSData&, double);
    };

    static const Mapping mappings[] = {
        { HID_USAGE_UPS_ACPRAESENT, [](UPSData& d, double v) { d.acPresent = v != 0; } },
        { HID_USAGE_UPS_DISCHARGING, [](UPSData& d, double v) { d.discharging = v != 0; } },
        { HID_USAGE_UPS_CHARGING, [](UPSData& d, double v) { d.charging = v != 0; } },
        { HID_USAGE_UPS_BELOWREMCAP, [](UPSData& d, double v) { d.belowRemainingCapacityLimit = v != 0; } },
        { HID_USAGE_UPS_NEEDREPLACE, [](UPSData& d, double v) { d.needReplacement = v != 0; } },
        { HID_USAGE_UPS_OVERLOAD, [](UPSData& d, double v) { d.overload = v != 0; } },
        { HID_USAGE_UPS_SHUTDOWNIMMINENT, [](UPSData& d, double v) { d.shutdownImminent = v != 0; } },
        { HID_USAGE_UPS_COMMLOST, [](UPSData& d, double v) { d.communicationLost = v != 0; } },
        
        { HID_USAGE_UPS_LOAD, [](UPSData& d, double v) {
            d.load = v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * v) / 10000);
        }},
        { HID_USAGE_UPS_INVOLTAGE, [](UPSData& d, double v) { d.inputVoltage = v; } },
        { HID_USAGE_UPS_OUTVOLTAGE, [](UPSData& d, double v) { d.outputVoltage = v; } },
        { HID_USAGE_UPS_CONFIGVOLTAGE, [](UPSData& d, double v) { d.configVoltage = v; } },
        { HID_USAGE_UPS_CONFIGACTPOW, [](UPSData& d, double v) { d.configActivePower = v; } },
        { HID_USAGE_UPS_CONFIGAPPPOW, [](UPSData& d, double v) { d.configApparentPower = v; } },
        
        { HID_USAGE_BAT_VOLTAGE, [](UPSData& d, double v) { d.batteryVoltage = v; } },
        { HID_USAGE_BAT_REMCAPACITY, [](UPSData& d, double v) { d.remainingCapacity = v > 100 ? 100 : v; } },
        { HID_USAGE_BAT_REMCAPLIMIT, [](UPSData& d, double v) { d.remainingCapacityLimit = v; } },
        { HID_USAGE_BAT_RUNTIMETOEMPTY, [](UPSData& d, double v) { d.runTimeToEmpty = v; } },
        { HID_USAGE_BAT_DESIGNCAPACITY, [](UPSData& d, double v) { d.designCapacity = v; } },
        { HID_USAGE_BAT_FULLCHGCAPACITY, [](UPSData& d, double v) { d.fullChargeCapacity = v; } },
    };

    for (const auto& m : mappings) {
        const HIDUsageDef* def = host->getUsageDef(m.usage);
        if (def && def->report_id == report_id) {
            double val = HIDParser::extractUsage(def, report_id, data, length);
            m.apply(ups_data, val);
        }
    }
}

void CyberPowerDriver::parseStringDescriptor(USBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
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
    if (index == 1) ups_data.manufacturer = str;
    else if (index == 2) ups_data.product = str;
    else if (index == 3 || index == 4) ups_data.serialNumber = str;
}

