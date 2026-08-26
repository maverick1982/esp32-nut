#include "CyberPowerDriver.h"
#include "USBHostUPS.h"

CyberPowerDriver::CyberPowerDriver() : 
    _last_poll(0), 
    _last_fast_poll(0), 
    _last_step_time(0), 
    _poll_step(0),
    _slow_poll_counter(0),
    _active_beeper("") {
}

void CyberPowerDriver::setup() {
    _last_poll = 0;
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _slow_poll_counter = 14;
    _active_beeper = "";
    Serial.println("[CyberPowerDriver] Setup started.");
}

void CyberPowerDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 30000 || _last_fast_poll == 0) {
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
            
            _slow_poll_counter++;
            if (_slow_poll_counter >= 2) {
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (host->isControlPending()) return;

        if (now - _last_step_time >= 50 || _poll_step == 1) { // Execute first step immediately
            _last_step_time = now;
            
            if (_poll_step == 1) {
                if (_slow_poll_counter == 0 && data.manufacturer == "") if (host->_iManufacturer > 0) host->requestStringDescriptor(host->_iManufacturer);
            } else if (_poll_step == 2) {
                if (_slow_poll_counter == 0 && data.product == "") if (host->_iProduct > 0) host->requestStringDescriptor(host->_iProduct);
            } else if (_poll_step == 3) {
                if (_slow_poll_counter == 0 && data.serialNumber == "") if (host->_iSerialNumber > 0) host->requestStringDescriptor(host->_iSerialNumber);
            } else {
                const auto& usages = host->_hid_parser.getUsages();
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    if (u.report_type == 2) continue; // Skip OUTPUT reports
                    uint16_t pair = (u.report_type << 8) | u.report_id;
                    bool found = false;
                    for (uint16_t id : rids) {
                        if (id == pair) { found = true; break; }
                    }
                    if (!found && u.report_id != 0) rids.push_back(pair);
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
                    host->requestReport(r_id, r_type, 64);
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
        String path;
        void (*apply)(CyberPowerDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.ACPresent", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.acPresent = true; d.acPresent = v != 0; } } },
        { "UPS.PowerSummary.ACPresent", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.acPresent = true; d.acPresent = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Discharging", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.discharging = true; d.discharging = v != 0; } } },
        { "UPS.PowerSummary.Discharging", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.discharging = true; d.discharging = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Charging", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.charging = true; d.charging = v != 0; } } },
        { "UPS.PowerSummary.Charging", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.charging = true; d.charging = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.belowRemainingCapacityLimit = true; d.belowRemainingCapacityLimit = v != 0; } } },
        { "UPS.PowerSummary.BelowRemainingCapacityLimit", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.belowRemainingCapacityLimit = true; d.belowRemainingCapacityLimit = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.NeedReplacement", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.needReplacement = true; d.needReplacement = v != 0; } } },
        { "UPS.PowerSummary.NeedReplacement", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.needReplacement = true; d.needReplacement = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Overload", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.overload = true; d.overload = v != 0; } } },
        { "UPS.PowerSummary.Overload", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.overload = true; d.overload = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.ShutdownImminent", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.shutdownImminent = true; d.shutdownImminent = v != 0; } } },
        { "UPS.PowerSummary.ShutdownImminent", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.shutdownImminent = true; d.shutdownImminent = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.CommunicationLost", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.communicationLost = true; d.communicationLost = v != 0; } } },
        { "UPS.PowerSummary.CommunicationLost", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.communicationLost = true; d.communicationLost = v != 0; } } },
        
        { "UPS.PowerSummary.PercentLoad", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) {
            { d.has.load = true; d.load = (uint8_t)v; }
            if (d.has.configActivePower) { d.has.realPower = true; d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint8_t)v) / 100); }
            else if (d.has.configApparentPower) { d.has.realPower = true; d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint8_t)v) / 10000); }
        }},
        { "UPS.Battery.Temperature", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) {
            { d.has.batteryTemperature = true; d.batteryTemperature = (v > 200.0) ? (v - 273.15) : v; }
        }},
        { "UPS.Output.PercentLoad", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) {
            { d.has.load = true; d.load = (uint8_t)v; }
            if (d.has.configActivePower) { d.has.realPower = true; d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint8_t)v) / 100); }
            else if (d.has.configApparentPower) { d.has.realPower = true; d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint8_t)v) / 10000); }
        }},
        
        { "UPS.Input.Voltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.inputVoltage = true; d.inputVoltage = v; } } },
        { "UPS.Output.Voltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.outputVoltage = true; d.outputVoltage = v; } } },
        
        { "UPS.PowerSummary.Voltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.batteryVoltage = true; d.batteryVoltage = v; } } },
        
        { "UPS.PowerSummary.RemainingCapacity", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.remainingCapacity = true; d.remainingCapacity = v > 100 ? 100 : v; } } },
        { "UPS.PowerSummary.RemainingCapacityLimit", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.remainingCapacityLimit = true; d.remainingCapacityLimit = (uint8_t)v; } } },
        
        { "UPS.PowerSummary.RunTimeToEmpty", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.runTimeToEmpty = true; d.runTimeToEmpty = (uint32_t)v; } } },
        
        { "UPS.PowerConverter.ConfigActivePower", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configActivePower = true; d.configActivePower = (uint16_t)v; } } },
        { "UPS.Output.ConfigActivePower", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configActivePower = true; d.configActivePower = (uint16_t)v; } } },
        
        { "UPS.PowerSummary.ConfigVoltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { /* mapped to battery.voltage.nominal in cps-hid */ } },
        { "UPS.Input.ConfigVoltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configVoltage = true; d.configVoltage = (uint16_t)v; } } },
        { "UPS.Output.ConfigVoltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.outputVoltageNominal = true; d.outputVoltageNominal = (uint16_t)v; } } },
        
        { "UPS.Output.LowVoltageTransfer", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.lowVoltageTransfer = true; d.lowVoltageTransfer = (uint16_t)v; } } },
        { "UPS.Input.LowVoltageTransfer", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.lowVoltageTransfer = true; d.lowVoltageTransfer = (uint16_t)v; } } },
        
        { "UPS.Output.HighVoltageTransfer", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.highVoltageTransfer = true; d.highVoltageTransfer = (uint16_t)v; } } },
        { "UPS.Input.HighVoltageTransfer", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.highVoltageTransfer = true; d.highVoltageTransfer = (uint16_t)v; } } },
        
        { "UPS.PowerSummary.AudibleAlarmControl", [](CyberPowerDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) { d.has.beeperEnabled = true; d.beeperEnabled = (v != 0); }
            else if (v == 1) { d.has.beeperEnabled = true; d.beeperEnabled = false; } 
            else if (v == 2 || v == 3) { d.has.beeperEnabled = true; d.beeperEnabled = true; } 
        } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](CyberPowerDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) { d.has.beeperEnabled = true; d.beeperEnabled = (v != 0); }
            else if (v == 1) { d.has.beeperEnabled = true; d.beeperEnabled = false; } 
            else if (v == 2 || v == 3) { d.has.beeperEnabled = true; d.beeperEnabled = true; } 
        } },
        { "UPS.AudibleAlarmControl", [](CyberPowerDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) { d.has.beeperEnabled = true; d.beeperEnabled = (v != 0); }
            else if (v == 1) { d.has.beeperEnabled = true; d.beeperEnabled = false; } 
            else if (v == 2 || v == 3) { d.has.beeperEnabled = true; d.beeperEnabled = true; } 
        } }
    };

    if (_active_beeper == "") {
        _active_beeper = host->getActiveBeeperPath();
        if (_active_beeper == "") _active_beeper = "none";
    }

    for (const auto& u : host->_hid_parser.getUsages()) {
        if (u.report_id != report_id || u.report_type != report_type) continue;
        for (const auto& m : mappings) {
            if (u.path == m.path) {
                double val = HIDParser::extractUsage(&u, report_id, data, length);
                m.apply(this, ups_data, val, &u);
                break;
            }
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
    
    // Auto-detect inverted strings by checking the high byte of the first UTF-16 character
    if (str_len >= 4 && length >= 4) {
        if (data[3] == 0xFF) {
            invert = true;
        } else if (data[3] == 0x00) {
            invert = false;
        }
    }
    
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) {
            char c = (char)data[i];
            if (invert) c = ~c;
            str += c;
        }
    }
    if (index == host->_iManufacturer) { ups_data.has.manufacturer = true; ups_data.manufacturer = str; }
    else if (index == host->_iProduct) { ups_data.has.product = true; ups_data.product = str; }
    else if (host->_iSerialNumber > 0 && index == host->_iSerialNumber) { ups_data.has.serialNumber = true; ups_data.serialNumber = str; }
}

