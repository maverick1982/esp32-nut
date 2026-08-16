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
    _slow_poll_counter = 0;
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
        const char* path;
        void (*apply)(UPSData&, double);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.ACPresent", [](UPSData& d, double v) { d.acPresent = v != 0; } },
        { "UPS.PowerSummary.ACPresent", [](UPSData& d, double v) { d.acPresent = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Discharging", [](UPSData& d, double v) { d.discharging = v != 0; } },
        { "UPS.PowerSummary.Discharging", [](UPSData& d, double v) { d.discharging = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Charging", [](UPSData& d, double v) { d.charging = v != 0; } },
        { "UPS.PowerSummary.Charging", [](UPSData& d, double v) { d.charging = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", [](UPSData& d, double v) { d.belowRemainingCapacityLimit = v != 0; } },
        { "UPS.PowerSummary.BelowRemainingCapacityLimit", [](UPSData& d, double v) { d.belowRemainingCapacityLimit = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.NeedReplacement", [](UPSData& d, double v) { d.needReplacement = v != 0; } },
        { "UPS.PowerSummary.NeedReplacement", [](UPSData& d, double v) { d.needReplacement = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Overload", [](UPSData& d, double v) { d.overload = v != 0; } },
        { "UPS.PowerSummary.Overload", [](UPSData& d, double v) { d.overload = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.ShutdownImminent", [](UPSData& d, double v) { d.shutdownImminent = v != 0; } },
        { "UPS.PowerSummary.ShutdownImminent", [](UPSData& d, double v) { d.shutdownImminent = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.CommunicationLost", [](UPSData& d, double v) { d.communicationLost = v != 0; } },
        { "UPS.PowerSummary.CommunicationLost", [](UPSData& d, double v) { d.communicationLost = v != 0; } },
        
        { "UPS.PowerConverter.Input.Voltage", [](UPSData& d, double v) { d.inputVoltage = v; } },
        { "UPS.Input.Voltage", [](UPSData& d, double v) { d.inputVoltage = v; } },
        { "UPS.Flow.Voltage", [](UPSData& d, double v) { d.inputVoltage = v; } },
        
        { "UPS.PowerConverter.Output.Voltage", [](UPSData& d, double v) { d.outputVoltage = v; } },
        { "UPS.Output.Voltage", [](UPSData& d, double v) { d.outputVoltage = v; } },
        
        { "UPS.PowerSummary.Voltage", [](UPSData& d, double v) { d.batteryVoltage = v; } },
        { "UPS.BatterySystem.Voltage", [](UPSData& d, double v) { d.batteryVoltage = v; } },
        { "UPS.Battery.Voltage", [](UPSData& d, double v) { d.batteryVoltage = v; } },
        
        { "UPS.PowerSummary.RemainingCapacity", [](UPSData& d, double v) { d.remainingCapacity = v > 100 ? 100 : (uint8_t)v; } },
        { "UPS.PowerSummary.RemainingCapacityLimit", [](UPSData& d, double v) { d.remainingCapacityLimit = (uint8_t)v; } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](UPSData& d, double v) { d.runTimeToEmpty = (uint32_t)v; } },
        { "UPS.Battery.RunTimeToEmpty", [](UPSData& d, double v) { d.runTimeToEmpty = (uint32_t)v; } },
        
        { "UPS.PowerSummary.PercentLoad", [](UPSData& d, double v) {
            d.load = (uint8_t)v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint32_t)v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint32_t)v) / 10000);
        }},
        { "UPS.Output.PercentLoad", [](UPSData& d, double v) {
            d.load = (uint8_t)v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint32_t)v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint32_t)v) / 10000);
        }},
        
        { "UPS.BatterySystem.Battery.DesignCapacity", [](UPSData& d, double v) { d.designCapacity = (uint8_t)(v / 3600.0); } },
        { "UPS.BatterySystem.Battery.FullChargeCapacity", [](UPSData& d, double v) { d.fullChargeCapacity = (uint8_t)(v / 3600.0); } },
        
        { "UPS.Flow.ConfigActivePower", [](UPSData& d, double v) { d.configActivePower = (uint16_t)v; } },
        { "UPS.PowerConverter.ConfigActivePower", [](UPSData& d, double v) { d.configActivePower = (uint16_t)v; } },
        { "UPS.Output.ConfigActivePower", [](UPSData& d, double v) { d.configActivePower = (uint16_t)v; } },
        
        { "UPS.Flow.ConfigApparentPower", [](UPSData& d, double v) { d.configApparentPower = (uint16_t)v; } },
        
        { "UPS.PowerSummary.ConfigVoltage", [](UPSData& d, double v) { d.configVoltage = (uint16_t)v; } },
        { "UPS.Flow.ConfigVoltage", [](UPSData& d, double v) { d.configVoltage = (uint16_t)v; } },
        { "UPS.Input.ConfigVoltage", [](UPSData& d, double v) { d.configVoltage = (uint16_t)v; } },
        
        { "UPS.PowerConverter.Output.ConfigVoltage", [](UPSData& d, double v) { d.outputVoltageNominal = (uint16_t)v; } },
        { "UPS.Output.ConfigVoltage", [](UPSData& d, double v) { d.outputVoltageNominal = (uint16_t)v; } },
        
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](UPSData& d, double v) { d.lowVoltageTransfer = (uint16_t)v; } },
        { "UPS.Output.LowVoltageTransfer", [](UPSData& d, double v) { d.lowVoltageTransfer = (uint16_t)v; } },
        { "UPS.Input.LowVoltageTransfer", [](UPSData& d, double v) { d.lowVoltageTransfer = (uint16_t)v; } },
        
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](UPSData& d, double v) { d.highVoltageTransfer = (uint16_t)v; } },
        { "UPS.Output.HighVoltageTransfer", [](UPSData& d, double v) { d.highVoltageTransfer = (uint16_t)v; } },
        { "UPS.Input.HighVoltageTransfer", [](UPSData& d, double v) { d.highVoltageTransfer = (uint16_t)v; } }
    };

    for (const auto& u : host->_hid_parser.getUsages()) {
        if (u.report_id != report_id || u.report_type != report_type) continue;
        for (const auto& m : mappings) {
            if (u.path == m.path) {
                double val = HIDParser::extractUsage(&u, report_id, data, length);
                m.apply(ups_data, val);
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

