#include "GenericDriver.h"
#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "HIDUsages.h"
#include "Quirks.h"

/**
 * @brief Generic HID UPS Driver Implementation
 * 
 * ADR 0003 COMPLIANCE:
 * This driver faithfully mirrors the official NUT behavior for generic USB PDC devices.
 * - Reference: nut_repo/drivers/usbhid-ups.c and nut_repo/drivers/libhid.c
 * - HID Usages: Strict adherence to NUT's usbhid-ups mappings for UPS.PowerSummary, UPS.BatterySystem, etc.
 */

GenericDriver::GenericDriver() : 
    _last_poll(0),
    _last_fast_poll(0),
    _last_full_poll(0),
    _last_step_time(0),
    _poll_step(0),
    _slow_poll_counter(0),
    _is_full_walk(false),
    _batteryDateStringIndex(0) {
}

void GenericDriver::setup() {
    _last_poll = 0;
    _last_fast_poll = 0;
    _last_full_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _slow_poll_counter = 14;
    _is_full_walk = false;
    _batteryDateStringIndex = 0;
    _active_beeper = "";
}

bool GenericDriver::isStaticUsage(const String& path) {
    if (path.indexOf("ConfigVoltage") >= 0) return true;
    if (path.indexOf("ConfigActivePower") >= 0) return true;
    if (path.indexOf("ConfigApparentPower") >= 0) return true;
    if (path.indexOf("ConfigFrequency") >= 0) return true;
    if (path.indexOf("HighVoltageTransfer") >= 0) return true;
    if (path.indexOf("LowVoltageTransfer") >= 0) return true;
    if (path.indexOf("RemainingCapacityLimit") >= 0) return true;
    if (path.indexOf("DesignCapacity") >= 0) return true;
    if (path.indexOf("ManufacturerDate") >= 0) return true;
    return false;
}

void GenericDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (data.get("ups.type") != getDriverName()) {
        data.set("ups.type", getDriverName());
    }

    if (_poll_step == 0) {
        bool initial_run = (_last_full_poll == 0);
        bool full_poll_due = (now - _last_full_poll >= getFullPollIntervalMs());
        bool fast_poll_due = (now - _last_fast_poll >= getFastPollIntervalMs() || _last_fast_poll == 0);

        if (initial_run || full_poll_due) {
            _is_full_walk = true;
            _poll_step = 1;
            _last_fast_poll = now != 0 ? now : 1;
        } else if (fast_poll_due) {
            _is_full_walk = false;
            _last_fast_poll = now != 0 ? now : 1;
            // Quick update: feature reports are skipped (no traffic on EP0)
            return;
        } else {
            return;
        }
    }

    if (_poll_step > 0) {
        if (host->isControlPending()) return;

        if (now - _last_step_time >= getPollPacingMs()) { // Strict pacing for all requests
            _last_step_time = now;
            
            if (_poll_step == 1) {
                if (!data.hasKey("ups.mfr") && host->_iManufacturer > 0) host->requestStringDescriptor(host->_iManufacturer);
            } else if (_poll_step == 2) {
                if (!data.hasKey("ups.model") && host->_iProduct > 0) host->requestStringDescriptor(host->_iProduct);
            } else if (_poll_step == 3) {
                if (!data.hasKey("ups.serial") && host->_iSerialNumber > 0) host->requestStringDescriptor(host->_iSerialNumber);
            } else if (_poll_step == 4) {
                if (!data.hasKey("battery.mfr.date") && _batteryDateStringIndex > 0) host->requestStringDescriptor(_batteryDateStringIndex);
            } else {
                const auto& usages = host->getUsages();
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    if (u.report_type == 2) continue; // Skip OUTPUT reports
                    if (!shouldPollUsage(u.path)) continue;
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

                // If this is a recurring full walk (not initial walk), exclude reports that only contain static usages
                if (_last_full_poll > 0) {
                    for (auto it = rids.begin(); it != rids.end(); ) {
                        uint8_t r_id = *it & 0xFF;
                        uint8_t r_type = *it >> 8;
                        bool has_dynamic = false;
                        for (const auto& u : usages) {
                            if (u.report_id == r_id && u.report_type == r_type) {
                                if (!isStaticUsage(u.path)) {
                                    has_dynamic = true;
                                    break;
                                }
                            }
                        }
                        if (!has_dynamic) {
                            it = rids.erase(it);
                            continue;
                        }
                        ++it;
                    }
                }
                
                int index = _poll_step - 5;
                if (index >= 0 && index < (int)rids.size()) {
                    uint8_t r_type = rids[index] >> 8;
                    uint8_t r_id = rids[index] & 0xFF;
                    
                    if (host->getQuirks() & QUIRK_NO_GET_REPORT) {
                        _poll_step = 0; // Skip polling entirely for devices without GET_REPORT support
                        return;
                    }
                    
                    uint16_t expected_length = 64;
                    if (host->getQuirks() & QUIRK_MAX_REPORT_SIZE_1) {
                        expected_length = 1;
                    } else {
                        uint16_t max_bit_bound = 0;
                        for (const auto& u : host->getUsages()) {
                            if (u.report_id == r_id && u.report_type == r_type) {
                                if (u.bit_offset + u.bit_size > max_bit_bound) {
                                    max_bit_bound = u.bit_offset + u.bit_size;
                                }
                            }
                        }
                        if (max_bit_bound > 0) {
                            expected_length = (max_bit_bound + 7) / 8;
                            if (r_id != 0) expected_length += 1;
                        }
                    }
                    host->requestReport(r_id, r_type, expected_length);
                } else {
                    _poll_step = 0; // Done
                    _last_full_poll = now != 0 ? now : 1;
                    _last_fast_poll = now != 0 ? now : 1;
                    _is_full_walk = false;
                    return;
                }
            }
            _poll_step++;
        }
    }
}

void GenericDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    struct Mapping {
        String path;
        void (*apply)(GenericDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.ACPresent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.ac_present", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.ACPresent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.ac_present", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.Discharging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.discharging", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.Discharging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.discharging", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.Charging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.charging", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.Charging", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.charging", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.battery_low", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.BelowRemainingCapacityLimit", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.battery_low", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.NeedReplacement", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.replace_battery", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.NeedReplacement", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.replace_battery", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.Overload", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.overload", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.Overload", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.overload", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.ShutdownImminent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.shutdown_imminent", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.ShutdownImminent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.shutdown_imminent", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.CommunicationLost", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.comm_lost", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.CommunicationLost", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.comm_lost", v != 0 ? "1" : "0"); } },
        
        { "UPS.PowerConverter.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("input.voltage") && d.isOnline()) return;
            d.set("input.voltage", String(v, 1)); 
        } },
        { "UPS.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("input.voltage") && d.isOnline()) return;
            d.set("input.voltage", String(v, 1)); 
        } },
        { "UPS.Flow.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("input.voltage") && d.isOnline()) return;
            d.set("input.voltage", String(v, 1)); 
        } },
        
        { "UPS.PowerConverter.Input.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("input.frequency") && d.isOnline()) return;
            d.set("input.frequency", String(v, 1)); 
        } },
        { "UPS.Input.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("input.frequency") && d.isOnline()) return;
            d.set("input.frequency", String(v, 1)); 
        } },
        { "UPS.Flow.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("input.frequency") && d.isOnline()) return;
            d.set("input.frequency", String(v, 1)); 
        } },
        
        { "UPS.PowerConverter.Output.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("output.voltage") && d.isOnline()) return;
            d.set("output.voltage", String(v, 1)); 
        } },
        { "UPS.Output.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("output.voltage") && d.isOnline()) return;
            d.set("output.voltage", String(v, 1)); 
        } },
        
        { "UPS.PowerConverter.Output.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("output.frequency") && d.isOnline()) return;
            d.set("output.frequency", String(v, 1)); 
        } },
        { "UPS.Output.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            if (v <= 0.0 && d.hasKey("output.frequency") && d.isOnline()) return;
            d.set("output.frequency", String(v, 1)); 
        } },
        
        { "UPS.PowerConverter.Output.ActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.realpower", String((int)v)); } },
        { "UPS.Output.ActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.realpower", String((int)v)); } },
        
        { "UPS.PowerConverter.Output.ApparentPower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.power", String((int)v)); } },
        { "UPS.Output.ApparentPower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.power", String((int)v)); } },
        
        { "UPS.PowerSummary.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage", String(v, 2)); } },
        { "UPS.BatterySystem.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage", String(v, 2)); } },
        { "UPS.Battery.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage", String(v, 2)); } },
        
        { "UPS.PowerSummary.RemainingCapacity", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.charge", String((int)(v > 100 ? 100 : v))); } },
        { "UPS.PowerSummary.RemainingCapacityLimit", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.charge.low", String((int)v)); } },
        { "UPS.PowerSummary.RunTimeToEmpty", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.runtime", String((int)v)); } },
        { "UPS.Battery.RunTimeToEmpty", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.runtime", String((int)v)); } },
        
        { "UPS.PowerSummary.PercentLoad", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("ups.load", String((int)v));
            d.updateRealPower();
        }},
        { "UPS.PowerSummary.ManufacturerDate", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            if (v <= 0) return;
            long date = (long)v;
            long year = 1980 + (date >> 9);
            long month = (date >> 5) & 0x0F;
            long day = date & 0x1F;
            char buf[20];
            snprintf(buf, sizeof(buf), "%04ld/%02ld/%02ld", year, month, day);
            d.set("ups.mfr.date", String(buf));
        }},
        { "UPS.ManufacturerDate", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            if (v <= 0) return;
            long date = (long)v;
            long year = 1980 + (date >> 9);
            long month = (date >> 5) & 0x0F;
            long day = date & 0x1F;
            char buf[20];
            snprintf(buf, sizeof(buf), "%04ld/%02ld/%02ld", year, month, day);
            d.set("ups.mfr.date", String(buf));
        }},
        { "UPS.Battery.ManufacturerDate", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            if (v <= 0) return;
            long date = (long)v;
            long year = 1980 + (date >> 9);
            long month = (date >> 5) & 0x0F;
            long day = date & 0x1F;
            char buf[20];
            snprintf(buf, sizeof(buf), "%04ld/%02ld/%02ld", year, month, day);
            d.set("battery.mfr.date", String(buf));
        }},
        { "UPS.BatterySystem.Battery.Date", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) {
            if (def && v > 0) drv->_batteryDateStringIndex = (uint8_t)v;
        }},
        { "UPS.Battery.Date", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) {
            if (def && v > 0) drv->_batteryDateStringIndex = (uint8_t)v;
        }},
        { "UPS.PowerSummary.Temperature", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            double temp = (v > 200.0) ? (v - 273.15) : v;
            if (temp <= 0.0 && d.hasKey("ups.temperature") && d.isOnline()) return;
            d.set("ups.temperature", String(temp, 1));
        }},
        { "UPS.Battery.Temperature", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            double temp = (v > 200.0) ? (v - 273.15) : v;
            if (temp <= 0.0 && d.hasKey("battery.temperature") && d.isOnline()) return;
            d.set("battery.temperature", String(temp, 1));
        }},
        
        { "UPS.PowerConverter.Output.PercentLoad", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            d.set("ups.load", String((int)v)); 
            d.updateRealPower(); 
        }},
        { "UPS.Output.PercentLoad", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            d.set("ups.load", String((int)v)); 
            d.updateRealPower(); 
        }},
        
        { "UPS.BatterySystem.Battery.DesignCapacity", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.capacity", String((int)(v / 3600.0))); } },
        { "UPS.BatterySystem.Battery.FullChargeCapacity", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.capacity.full", String((int)(v / 3600.0))); } },
        
        { "UPS.Flow.ConfigActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.realpower.nominal", String((int)v)); d.updateRealPower(); } },
        { "UPS.PowerConverter.ConfigActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.realpower.nominal", String((int)v)); d.updateRealPower(); } },
        { "UPS.Output.ConfigActivePower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.realpower.nominal", String((int)v)); d.updateRealPower(); } },
        
        { "UPS.Flow.ConfigApparentPower", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.power.nominal", String((int)v)); d.updateRealPower(); } },
        
        { "UPS.PowerSummary.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) d.set("input.voltage.nominal", String((int)v)); } },
        { "UPS.Flow.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) d.set("input.voltage.nominal", String((int)v)); } },
        { "UPS.Input.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) d.set("input.voltage.nominal", String((int)v)); } },
        
        { "UPS.PowerConverter.Output.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) d.set("output.voltage.nominal", String((int)v)); } },
        { "UPS.Output.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) d.set("output.voltage.nominal", String((int)v)); } },
        
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.high", String((int)v)); } },
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.low", String((int)v)); } },
        
        { "UPS.PowerSummary.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (v == 0 && d.hasKey("ups.beeper.status")) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if (v == 1) { d.set("ups.beeper.status", "disabled"); } 
            else if (v == 2 || v == 3) { d.set("ups.beeper.status", "enabled"); } 
        } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (v == 0 && d.hasKey("ups.beeper.status")) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if (v == 1) { d.set("ups.beeper.status", "disabled"); } 
            else if (v == 2 || v == 3) { d.set("ups.beeper.status", "enabled"); } 
        } },
        { "UPS.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && def->path != drv->_active_beeper) return;
            if (v == 0 && d.hasKey("ups.beeper.status")) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if (v == 1) { d.set("ups.beeper.status", "disabled"); } 
            else if (v == 2 || v == 3) { d.set("ups.beeper.status", "enabled"); } 
        } },
        
        { "UPS.PowerSummary.DelayBeforeShutdown", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.delay.shutdown", String((int)v)); d.set("ups.timer.shutdown", String((int)v)); } },
        
        { "UPS.Output.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.low", String((int)v)); } },
        { "UPS.Input.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.low", String((int)v)); } },
        { "UPS.Output.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.high", String((int)v)); } },
        { "UPS.Input.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.high", String((int)v)); } }
    };

    if (_active_beeper == "") {
        _active_beeper = host->getActiveBeeperPath();
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

void GenericDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
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
    if (index == host->_iManufacturer) { ups_data.set("ups.mfr", str); }
    else if (index == host->_iProduct) { ups_data.set("ups.model", str); }
    else if (host->_iSerialNumber > 0 && index == host->_iSerialNumber) { ups_data.set("ups.serial", str); }
    else if (_batteryDateStringIndex > 0 && index == _batteryDateStringIndex) { ups_data.set("battery.mfr.date", str); }
}

