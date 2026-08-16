#include "APCDriver.h"
#include "USBHostUPS.h"
#include "HIDUsages.h"
#include <string.h>

APCDriver::APCDriver() : _poll_step(0), _last_step_time(0), _last_fast_poll(0), _slow_poll_counter(0), _last_poll(0) {}


void APCDriver::setup() {
    _last_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _last_fast_poll = 0;
    _slow_poll_counter = 0;
}

void APCDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
            
            _slow_poll_counter++;
            if (_slow_poll_counter >= 15) {
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (now - _last_step_time >= 50 || _poll_step == 1) {
            _last_step_time = now;
            
            const auto& usages = host->_hid_parser.getUsages();
            std::vector<uint8_t> rids;
            for (const auto& u : usages) {
                bool found = false;
                for (uint8_t id : rids) {
                    if (id == u.report_id) { found = true; break; }
                }
                if (!found && u.report_id != 0) rids.push_back(u.report_id);
            }
            
            if (_poll_step - 1 < rids.size()) {
                host->requestReport(rids[_poll_step - 1], 0x03, 8); // Polling dynamically
            } else {
                _poll_step = 0; // Done
                return;
            }
            _poll_step++;
        }
    }
}

void APCDriver::decodeReport(USBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
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
        
        { HID_USAGE_UPS_LOAD, [](UPSData& d, double v) {
            d.load = (uint8_t)v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint8_t)v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint8_t)v) / 10000);
        }},
        { HID_USAGE_UPS_INVOLTAGE, [](UPSData& d, double v) { d.inputVoltage = v; } },
        { HID_USAGE_UPS_OUTVOLTAGE, [](UPSData& d, double v) { d.outputVoltage = v; } },
        
        { HID_USAGE_BAT_VOLTAGE, [](UPSData& d, double v) { d.batteryVoltage = v; } },
        { HID_USAGE_BAT_REMCAPACITY, [](UPSData& d, double v) { d.remainingCapacity = (uint8_t)v; } },
        { HID_USAGE_BAT_RUNTIMETOEMPTY, [](UPSData& d, double v) { d.runTimeToEmpty = (uint32_t)v; } },
    };

    for (const auto& m : mappings) {
        const HIDUsageDef* def = host->getUsageDef(m.usage);
        if (def && def->report_id == report_id) {
            double val = HIDParser::extractUsage(def, report_id, data, length);
            m.apply(ups_data, val);
        }
    }
}

void APCDriver::parseStringDescriptor(USBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) {
            str += (char)data[i];
        }
    }
    if (index == 1) ups_data.manufacturer = str;
    else if (index == 2) ups_data.product = str;
    else if (index == 3) ups_data.serialNumber = str;
}

