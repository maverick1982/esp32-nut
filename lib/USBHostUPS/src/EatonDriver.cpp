#include "EatonDriver.h"
#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "HIDUsages.h"

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
    _slow_poll_counter = 14;
    _active_beeper = "";
}

void EatonDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
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
                const auto& usages = host->getUsages();
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    if (u.report_type == 2) continue; // Skip OUTPUT reports
                    if (u.report_id == 254 || u.report_id == 255) continue; // CRITICAL QUIRK (NUT): Skip reports 254/255 for Eaton devices to prevent USB freeze/stall
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

void EatonDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    struct Mapping {
        const char* path;
        void (*apply)(EatonDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.ACPresent", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.acPresent = true; d.acPresent = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Discharging", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.discharging = true; d.discharging = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Charging", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.charging = true; d.charging = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.belowRemainingCapacityLimit = true; d.belowRemainingCapacityLimit = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.NeedReplacement", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.needReplacement = true; d.needReplacement = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Overload", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.overload = true; d.overload = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.ShutdownImminent", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.shutdownImminent = true; d.shutdownImminent = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.CommunicationLost", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.communicationLost = true; d.communicationLost = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.Good", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.good = true; d.good = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.InternalFailure", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.internalFailure = true; d.internalFailure = v != 0; } } },
        
        { "UPS.OutletSystem.Outlet.PresentStatus.SwitchOn/Off", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.outlet1Switch = true; d.outlet1Switch = v != 0; } { d.has.outlet2Switch = true; d.outlet2Switch = v != 0; } } },
        
        { "UPS.PowerSummary.PercentLoad", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) {
            { d.has.load = true; d.load = (uint8_t)v; }
            if (d.has.configActivePower) { d.has.realPower = true; d.realPower = (uint16_t)(((uint32_t)d.configActivePower * (uint32_t)v) / 100); }
            else if (d.has.configApparentPower) { d.has.realPower = true; d.realPower = (uint16_t)(((uint32_t)d.configApparentPower * 60 * (uint32_t)v) / 10000); }
        }},
        { "UPS.Battery.Temperature", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) {
            { d.has.batteryTemperature = true; d.batteryTemperature = (v > 200.0) ? (v - 273.15) : v; }
        }},
        { "UPS.PowerConverter.Input.Voltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.inputVoltage = true; d.inputVoltage = v; } } },
        { "UPS.PowerConverter.Output.Voltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.outputVoltage = true; d.outputVoltage = v; } } },
        
        { "UPS.PowerSummary.Voltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) { d.has.batteryVoltage = true; { d.has.batteryVoltage = true; d.batteryVoltage = v; } } } },

        { "UPS.BatterySystem.Voltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.batteryVoltage = true; d.batteryVoltage = v; } } },
        { "UPS.PowerSummary.RemainingCapacity", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.remainingCapacity = true; d.remainingCapacity = v > 100 ? 100 : (uint8_t)v; } } },
        { "UPS.PowerSummary.RemainingCapacityLimit", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.remainingCapacityLimit = true; d.remainingCapacityLimit = (uint8_t)v; } } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.runTimeToEmpty = true; d.runTimeToEmpty = (uint32_t)v; } } },
        
        { "UPS.BatterySystem.Battery.DesignCapacity", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.designCapacity = true; d.designCapacity = (uint8_t)(v / 3600.0); } } },
        
        { "UPS.Flow.ConfigApparentPower", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configApparentPower = true; d.configApparentPower = (uint16_t)v; } } },
        { "UPS.Flow.ConfigActivePower", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configActivePower = true; d.configActivePower = (uint16_t)v; } } },
        { "UPS.Flow.ConfigFrequency", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configFrequency = true; d.configFrequency = (uint8_t)v; } { d.has.outputFrequencyNominal = true; d.outputFrequencyNominal = (uint16_t)v; } } },
        { "UPS.Flow.ConfigVoltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configVoltage = true; d.configVoltage = (uint16_t)v; } { d.has.outputVoltageNominal = true; d.outputVoltageNominal = (uint16_t)v; } } },
        { "UPS.PowerConverter.Output.ConfigVoltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.outputVoltageNominal = true; d.outputVoltageNominal = (uint16_t)v; } } },
        
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.highVoltageTransfer = true; d.highVoltageTransfer = (uint16_t)v; } } },
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.lowVoltageTransfer = true; d.lowVoltageTransfer = (uint16_t)v; } } },
        
        { "UPS.PowerSummary.AudibleAlarmControl", [](EatonDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) { d.has.beeperEnabled = true; d.beeperEnabled = (v != 0); }
            else if (v == 1) { d.has.beeperEnabled = true; d.beeperEnabled = false; } 
            else if (v == 2 || v == 3) { d.has.beeperEnabled = true; d.beeperEnabled = true; } 
        } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](EatonDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) { d.has.beeperEnabled = true; d.beeperEnabled = (v != 0); }
            else if (v == 1) { d.has.beeperEnabled = true; d.beeperEnabled = false; } 
            else if (v == 2 || v == 3) { d.has.beeperEnabled = true; d.beeperEnabled = true; } 
        } },
        { "UPS.AudibleAlarmControl", [](EatonDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (def && def->bit_size == 1) { d.has.beeperEnabled = true; d.beeperEnabled = (v != 0); }
            else if (v == 1) { d.has.beeperEnabled = true; d.beeperEnabled = false; } 
            else if (v == 2 || v == 3) { d.has.beeperEnabled = true; d.beeperEnabled = true; } 
        } },
        
        { "UPS.PowerSummary.DelayBeforeShutdown", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.delayShutdown = true; d.delayShutdown = (int32_t)v; } { d.has.timerShutdown = true; d.timerShutdown = (int32_t)v; } } },
        { "UPS.PowerSummary.DelayBeforeStartup", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.delayStart = true; d.delayStart = (int32_t)v; } { d.has.timerStart = true; d.timerStart = (int32_t)v; } } },
        
        { "UPS.PowerConverter.ConverterType", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            int type = (int)v;
            if (type == 1) { d.has.upsType = true; d.upsType = "offline / line interactive"; }
            else if (type == 2) { d.has.upsType = true; d.upsType = "online"; }
            else if (type == 3) { d.has.upsType = true; d.upsType = "online - unitary/parallel"; }
            else if (type == 4) { d.has.upsType = true; d.upsType = "online - parallel with hot standy"; }
            else if (type == 5) { d.has.upsType = true; d.upsType = "online - hot standby redundancy"; }
        } },
        
        { "UPS.PowerSummary.iDeviceChemistry", [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) { drv->_chemStrIdx = (uint8_t)v; } }
    };

    if (_active_beeper == "") {
        _active_beeper = host->getActiveBeeperPath();
        // If it's still empty, we don't have a beeper
        if (_active_beeper == "") _active_beeper = "none";
    }

    for (const auto& u : host->getUsages()) {
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

void EatonDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) { // Safety check
            str += (char)data[i];
        }
    }
    if (index == host->_iManufacturer) { ups_data.has.manufacturer = true; ups_data.manufacturer = str; }
    else if (index == host->_iProduct) { ups_data.has.product = true; ups_data.product = str; }
    else if (host->_iSerialNumber > 0 && index == host->_iSerialNumber) { ups_data.has.serialNumber = true; ups_data.serialNumber = str; }
    else if (_chemStrIdx > 0 && index == _chemStrIdx) { ups_data.has.batteryType = true; ups_data.batteryType = str; }
}

