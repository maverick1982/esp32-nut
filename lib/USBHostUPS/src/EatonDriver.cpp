#include "EatonDriver.h"
#include "USBHostUPS.h"

EatonDriver::EatonDriver() :
    _last_poll(0),
    _chemStrIdx(0),
    _poll_step(0),
    _last_step_time(0),
    _last_fast_poll(0),
    _slow_poll_counter(0) {
}

void EatonDriver::setup() {
    _last_poll = 0;
    _chemStrIdx = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _last_fast_poll = 0;
    _slow_poll_counter = 0;
}

void EatonDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
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
        if (now - _last_step_time >= 50 || _poll_step == 1) { // Execute first step immediately
            _last_step_time = now;
            
            if (_poll_step == 1) {
                if (_slow_poll_counter == 0 && data.manufacturer == "") host->requestStringDescriptor(1);
            } else if (_poll_step == 2) {
                if (_slow_poll_counter == 0 && data.product == "") host->requestStringDescriptor(2);
            } else if (_poll_step == 3) {
                if (_slow_poll_counter == 0 && data.serialNumber == "") host->requestStringDescriptor(4);
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

void EatonDriver::decodeReport(USBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    struct Mapping {
        const char* path;
        void (*apply)(EatonDriver*, UPSData&, double);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.ACPresent", [](EatonDriver*, UPSData& d, double v) { d.acPresent = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Discharging", [](EatonDriver*, UPSData& d, double v) { d.discharging = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Charging", [](EatonDriver*, UPSData& d, double v) { d.charging = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", [](EatonDriver*, UPSData& d, double v) { d.belowRemainingCapacityLimit = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.NeedReplacement", [](EatonDriver*, UPSData& d, double v) { d.needReplacement = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Overload", [](EatonDriver*, UPSData& d, double v) { d.overload = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.ShutdownImminent", [](EatonDriver*, UPSData& d, double v) { d.shutdownImminent = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.CommunicationLost", [](EatonDriver*, UPSData& d, double v) { d.communicationLost = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.Good", [](EatonDriver*, UPSData& d, double v) { d.good = v != 0; } },
        { "UPS.PowerSummary.PresentStatus.InternalFailure", [](EatonDriver*, UPSData& d, double v) { d.internalFailure = v != 0; } },
        
        { "UPS.OutletSystem.Outlet.PresentStatus.SwitchOn/Off", [](EatonDriver*, UPSData& d, double v) { d.outlet1Switch = v != 0; d.outlet2Switch = v != 0; } },
        
        { "UPS.PowerSummary.PercentLoad", [](EatonDriver*, UPSData& d, double v) {
            d.load = (uint8_t)v;
            if (d.configActivePower > 0) d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint32_t)v) / 100);
            else if (d.configApparentPower > 0) d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint32_t)v) / 10000);
        }},
        { "UPS.PowerConverter.Input.Voltage", [](EatonDriver*, UPSData& d, double v) { d.inputVoltage = v; } },
        { "UPS.PowerConverter.Output.Voltage", [](EatonDriver*, UPSData& d, double v) { d.outputVoltage = v; } },
        
        { "UPS.PowerSummary.Voltage", [](EatonDriver*, UPSData& d, double v) { d.batteryVoltage = v; } },
        { "UPS.BatterySystem.Voltage", [](EatonDriver*, UPSData& d, double v) { d.batteryVoltage = v; } },
        { "UPS.PowerSummary.RemainingCapacity", [](EatonDriver*, UPSData& d, double v) { d.remainingCapacity = v > 100 ? 100 : (uint8_t)v; } },
        { "UPS.PowerSummary.RemainingCapacityLimit", [](EatonDriver*, UPSData& d, double v) { d.remainingCapacityLimit = (uint8_t)v; } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](EatonDriver*, UPSData& d, double v) { d.runTimeToEmpty = (uint32_t)v; } },
        
        { "UPS.BatterySystem.Battery.DesignCapacity", [](EatonDriver*, UPSData& d, double v) { d.designCapacity = (uint8_t)(v / 3600.0); } },
        
        { "UPS.Flow.ConfigApparentPower", [](EatonDriver*, UPSData& d, double v) { d.configApparentPower = (uint16_t)v; } },
        { "UPS.Flow.ConfigActivePower", [](EatonDriver*, UPSData& d, double v) { d.configActivePower = (uint16_t)v; } },
        { "UPS.Flow.ConfigFrequency", [](EatonDriver*, UPSData& d, double v) { d.configFrequency = (uint8_t)v; d.outputFrequencyNominal = (uint16_t)v; } },
        { "UPS.Flow.ConfigVoltage", [](EatonDriver*, UPSData& d, double v) { d.configVoltage = (uint16_t)v; d.outputVoltageNominal = (uint16_t)v; } },
        
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](EatonDriver*, UPSData& d, double v) { d.highVoltageTransfer = (uint16_t)v; } },
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](EatonDriver*, UPSData& d, double v) { d.lowVoltageTransfer = (uint16_t)v; } },
        
        { "UPS.PowerSummary.AudibleAlarmControl", [](EatonDriver*, UPSData& d, double v) { d.beeperEnabled = (v != 1); } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](EatonDriver*, UPSData& d, double v) { d.beeperEnabled = (v != 1); } },
        { "UPS.AudibleAlarmControl", [](EatonDriver*, UPSData& d, double v) { d.beeperEnabled = (v != 1); } },
        
        { "UPS.PowerSummary.DelayBeforeShutdown", [](EatonDriver*, UPSData& d, double v) { d.delayShutdown = (int32_t)v; d.timerShutdown = (int32_t)v; } },
        { "UPS.PowerSummary.DelayBeforeStartup", [](EatonDriver*, UPSData& d, double v) { d.delayStart = (int32_t)v; d.timerStart = (int32_t)v; } },
        
        { "UPS.PowerConverter.ConverterType", [](EatonDriver*, UPSData& d, double v) { 
            int type = (int)v;
            if (type == 1) d.upsType = "offline / line interactive";
            else if (type == 2) d.upsType = "online";
            else if (type == 3) d.upsType = "online - unitary/parallel";
            else if (type == 4) d.upsType = "online - parallel with hot standy";
            else if (type == 5) d.upsType = "online - hot standby redundancy";
        } },
        
        { "UPS.PowerSummary.iDeviceChemistry", [](EatonDriver* drv, UPSData&, double v) { drv->_chemStrIdx = (uint8_t)v; } }
    };

    for (const auto& u : host->_hid_parser.getUsages()) {
        if (u.report_id != report_id || u.report_type != report_type) continue;
        for (const auto& m : mappings) {
            if (u.path == m.path) {
                double val = HIDParser::extractUsage(&u, report_id, data, length);
                m.apply(this, ups_data, val);
                break;
            }
        }
    }
}

void EatonDriver::parseStringDescriptor(USBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) { // Safety check
            str += (char)data[i];
        }
    }
    if (index == 1) ups_data.manufacturer = str;
    else if (index == 2) ups_data.product = str;
    else if (index == 4) ups_data.serialNumber = str;
    else if (_chemStrIdx > 0 && index == _chemStrIdx) ups_data.batteryType = str;
}

