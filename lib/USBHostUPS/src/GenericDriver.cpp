#include "GenericDriver.h"
#include "USBHostUPS.h"

GenericDriver::GenericDriver() : 
    _last_poll(0),
    _last_fast_poll(0),
    _last_step_time(0),
    _poll_step(0),
    _slow_poll_counter(0) {
}

void GenericDriver::setup() {
    _last_poll = 0;
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _slow_poll_counter = 14;
    _active_beeper = "";
    Serial.println("[GenericDriver] Setup started.");
}

void GenericDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (data.upsType != "Generic") {
        data.upsType = "Generic";
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
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    uint16_t pair = (u.report_type << 8) | u.report_id;
                    bool found = false;
                    for (uint16_t id : rids) {
                        if (id == pair) { found = true; break; }
                    }
                    if (!found) rids.push_back(pair);
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

void GenericDriver::decodeReport(USBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    struct Mapping {
        String path;
        void (*apply)(GenericDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.ACPresent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.acPresent = v != 0; } },
        { "UPS.PowerSummary.ACPresent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.acPresent = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Discharging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.discharging = v != 0; } },
        { "UPS.PowerSummary.Discharging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.discharging = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Charging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.charging = v != 0; } },
        { "UPS.PowerSummary.Charging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.charging = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.belowRemainingCapacityLimit = v != 0; } },
        { "UPS.PowerSummary.BelowRemainingCapacityLimit", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.belowRemainingCapacityLimit = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.NeedReplacement", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.needReplacement = v != 0; } },
        { "UPS.PowerSummary.NeedReplacement", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.needReplacement = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Overload", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.overload = v != 0; } },
        { "UPS.PowerSummary.Overload", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.overload = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.ShutdownImminent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.shutdownImminent = v != 0; } },
        { "UPS.PowerSummary.ShutdownImminent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.shutdownImminent = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.CommunicationLost", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.communicationLost = v != 0; } },
        { "UPS.PowerSummary.CommunicationLost", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.communicationLost = v != 0; } },
        
        { "UPS.PowerConverter.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.inputVoltage = v; } },
        { "UPS.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.inputVoltage = v; } },
        { "UPS.Flow.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.inputVoltage = v; } },
        
        { "UPS.PowerConverter.Output.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.outputVoltage = v; } },
        { "UPS.Output.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.outputVoltage = v; } },
        
        { "UPS.PowerSummary.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.batteryVoltage = v; } },
        { "UPS.BatterySystem.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.batteryVoltage = v; } },
        { "UPS.Battery.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.batteryVoltage = v; } },
        
        { "UPS.PowerSummary.RemainingCapacity", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.remainingCapacity = v > 100 ? 100 : (uint8_t)v; } },
        { "UPS.PowerSummary.RemainingCapacityLimit", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.remainingCapacityLimit = (uint8_t)v; } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.runTimeToEmpty = (uint32_t)v; } },
        { "UPS.Battery.RunTimeToEmpty", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.runTimeToEmpty = (uint32_t)v; } },
        
        { "UPS.PowerSummary.PercentLoad", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.load = (uint8_t)v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint32_t)v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint32_t)v) / 10000);
        }},
        { "UPS.Output.PercentLoad", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.load = (uint8_t)v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint32_t)v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint32_t)v) / 10000);
        }},
        
        { "UPS.BatterySystem.Battery.DesignCapacity", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.designCapacity = (uint8_t)(v / 3600.0); } },
        { "UPS.BatterySystem.Battery.FullChargeCapacity", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.fullChargeCapacity = (uint8_t)(v / 3600.0); } },
        
        { "UPS.Flow.ConfigActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configActivePower = (uint16_t)v; } },
        { "UPS.PowerConverter.ConfigActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configActivePower = (uint16_t)v; } },
        { "UPS.Output.ConfigActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configActivePower = (uint16_t)v; } },
        
        { "UPS.Flow.ConfigApparentPower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configApparentPower = (uint16_t)v; } },
        
        { "UPS.PowerSummary.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configVoltage = (uint16_t)v; } },
        { "UPS.Flow.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configVoltage = (uint16_t)v; } },
        { "UPS.Input.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.configVoltage = (uint16_t)v; } },
        
        { "UPS.PowerConverter.Output.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.outputVoltageNominal = (uint16_t)v; } },
        { "UPS.Output.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.outputVoltageNominal = (uint16_t)v; } },
        
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.highVoltageTransfer = (uint16_t)v; } },
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.lowVoltageTransfer = (uint16_t)v; } },
        
        { "UPS.PowerSummary.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) d.beeperEnabled = (v != 0);
            else if (v == 1) d.beeperEnabled = false; 
            else if (v == 2 || v == 3) d.beeperEnabled = true; 
        } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) d.beeperEnabled = (v != 0);
            else if (v == 1) d.beeperEnabled = false; 
            else if (v == 2 || v == 3) d.beeperEnabled = true; 
        } },
        { "UPS.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) d.beeperEnabled = (v != 0);
            else if (v == 1) d.beeperEnabled = false; 
            else if (v == 2 || v == 3) d.beeperEnabled = true; 
        } },
        
        { "UPS.PowerSummary.DelayBeforeShutdown", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.delayShutdown = (int32_t)v; d.timerShutdown = (int32_t)v; } },
        
        { "UPS.Output.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.lowVoltageTransfer = (uint16_t)v; } },
        { "UPS.Input.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.lowVoltageTransfer = (uint16_t)v; } },
        { "UPS.Output.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.highVoltageTransfer = (uint16_t)v; } },
        { "UPS.Input.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.highVoltageTransfer = (uint16_t)v; } }
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

void GenericDriver::parseStringDescriptor(USBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
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

