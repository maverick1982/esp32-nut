#include "GenericDriver.h"
#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "HIDUsages.h"
#include "Quirks.h"
#include <algorithm>
#include <string.h>
#include <cmath>

/**
 * @brief Generic HID UPS Driver Implementation
 *
 * ADR 0003 COMPLIANCE:
 * This driver faithfully mirrors the official NUT behavior for generic USB PDC devices.
 * - Reference: nut_repo/drivers/usbhid-ups.c and nut_repo/drivers/libhid.c
 * - HID Usages: Strict adherence to NUT's usbhid-ups mappings for UPS.PowerSummary, UPS.BatterySystem, etc.
 * - Polling: quick poll of the status reports every 2 s, full poll every 30 s (usbhid-ups
 *   pollfreq / pollinterval, ADR 0006).
 */

GenericDriver::GenericDriver() :
    _batteryDateStringIndex(0),
    _lists_built(false),
    _input_twin{},
    _static_report{},
    _decoded_report{},
    _queue_pos(0),
    _step_now(false),
    _cycle_full(false),
    _strings_rechecked(false),
    _first_full_done(false),
    _last_quick(0),
    _last_full(0),
    _last_step(0) {
}

void GenericDriver::setup() {
    _active_beeper = "";
    _generic_map.invalidate();
    _lists_built = false;
    _quick.clear();
    _full.clear();
    _queue.clear();
    _queue_pos = 0;
    _step_now = false;
    _cycle_full = false;
    _strings_rechecked = false;
    _first_full_done = false;
    _cycle_strings.clear();
    memset(_input_twin, 0, sizeof(_input_twin));
    memset(_static_report, 0, sizeof(_static_report));
    memset(_decoded_report, 0, sizeof(_decoded_report));
    _last_quick = 0;
    _last_full = 0;
    _last_step = 0;
}

bool GenericDriver::isStatusUsage(const HIDUsageDef& u) {
    switch (u.usage) {
    case 0x00840035: // PercentLoad
    case 0x00840065: // Overload
    case 0x00840069: // ShutdownImminent
    case 0x00840073: // CommunicationLost
    case 0x00850042: // BelowRemainingCapacityLimit
    case 0x00850044: // Charging
    case 0x00850045: // Discharging
    case 0x0085004b: // NeedReplacement
    case 0x00850066: // RemainingCapacity
    case 0x00850068: // RunTimeToEmpty
    case 0x008500d0: // ACPresent
        return true;
    default:
        return strstr(u.path, ".PresentStatus.") != nullptr;
    }
}

bool GenericDriver::isDynamicUsage(const HIDUsageDef& u) {
    if (isStatusUsage(u)) return true;
    uint32_t page = u.usage & 0xFFFF0000;
    uint16_t id = u.usage & 0xFFFF;
    if (page == 0x00840000) {
        if (id >= 0x30 && id <= 0x37) return true; // Voltage ... Humidity
        if (id >= 0x55 && id <= 0x58) return true; // DelayBeforeReboot/Startup/Shutdown, Test
        if (id == 0x5a) return true;                // AudibleAlarmControl
        if (id >= 0x60 && id <= 0x7f) return true; // status flags
    } else if (page == 0x00850000) {
        if (id >= 0x40 && id <= 0x4f) return true; // status flags
        if (id >= 0x66 && id <= 0x6b) return true; // RemainingCapacity ... CycleCount
        if (id >= 0xd0 && id <= 0xdf) return true; // ACPresent, BatteryPresent, ...
    }
    // Vendor usages known by name: APCDelayBefore*, APCStatusFlag, APCLineFailCause
    return strstr(u.path, "Delay") != nullptr || strstr(u.path, "Status") != nullptr ||
           strstr(u.path, "LineFailCause") != nullptr;
}

void GenericDriver::buildPollLists(IUSBHostUPS* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const {
    quick.clear();
    full.clear();
    const auto& usages = host->getUsages();

    // Reports in order of first appearance. Report ID 0 is kept (review M4): a device
    // without report IDs has only that one.
    struct Report { uint8_t type; uint8_t id; bool status; };
    std::vector<Report> reports;
    for (const auto& u : usages) {
        if (u.report_type != 1 && u.report_type != 3) continue; // OUTPUT reports are not read
        if (!acceptPollReport(u.report_type, u.report_id)) continue;
        bool found = false;
        for (auto& r : reports) {
            if (r.type == u.report_type && r.id == u.report_id) {
                r.status = r.status || isStatusUsage(u);
                found = true;
                break;
            }
        }
        if (!found) reports.push_back(Report{u.report_type, u.report_id, isStatusUsage(u)});
    }

    for (const auto& r : reports) {
        if (r.type == 1) {
            if (!pollInputReports()) continue;
            // The FEATURE report with the same ID carries the same values
            bool has_feature = false;
            for (const auto& f : reports) {
                if (f.type == 3 && f.id == r.id) { has_feature = true; break; }
            }
            if (has_feature) continue;
        }
        full.push_back(PollItem{r.type, r.id, 0});
        if (r.status) quick.push_back(PollItem{r.type, r.id, 0});
    }
}

// A FEATURE report is an INPUT twin when every one of its usages is also in the INPUT
// report with the same ID: the INPUT report then already refreshes all its values.
// libhid.c keeps one buffer per report ID for both (issue #60, APC Back-UPS BX).
void GenericDriver::findInputTwins(IUSBHostUPS* host) {
    memset(_input_twin, 0, sizeof(_input_twin));
    uint32_t mismatch[8] = {};
    const auto& usages = host->getUsages();
    for (const auto& f : usages) {
        if (f.report_type != 3) continue;
        bool in_input = false;
        for (const auto& i : usages) {
            if (i.report_type == 1 && i.report_id == f.report_id && strcmp(i.path, f.path) == 0) {
                in_input = true;
                break;
            }
        }
        uint32_t bit = 1u << (f.report_id % 32);
        if (in_input) _input_twin[f.report_id / 32] |= bit;
        else mismatch[f.report_id / 32] |= bit;
    }
    for (int w = 0; w < 8; w++) _input_twin[w] &= ~mismatch[w];
}

// A report is static when none of its usages is dynamic
void GenericDriver::findStaticReports(IUSBHostUPS* host) {
    memset(_static_report, 0, sizeof(_static_report));
    uint32_t dynamic[2][8] = {};
    for (const auto& u : host->getUsages()) {
        if (u.report_type != 1 && u.report_type != 3) continue;
        uint32_t bit = 1u << (u.report_id % 32);
        uint8_t slot = typeSlot(u.report_type);
        if (isDynamicUsage(u)) dynamic[slot][u.report_id / 32] |= bit;
        else _static_report[slot][u.report_id / 32] |= bit;
    }
    for (int t = 0; t < 2; t++) {
        for (int w = 0; w < 8; w++) _static_report[t][w] &= ~dynamic[t][w];
    }
}

bool GenericDriver::isStaticAndKnown(const PollItem& item) const {
    if (!pollStaticReportsOnce()) return false;
    if (item.report_type != 1 && item.report_type != 3) return false;
    uint8_t slot = typeSlot(item.report_type);
    uint32_t bit = 1u << (item.id % 32);
    return (_static_report[slot][item.id / 32] & bit) && (_decoded_report[slot][item.id / 32] & bit);
}

bool GenericDriver::isFreshFromInput(IUSBHostUPS* host, const PollItem& item, uint32_t now) const {
    if (item.report_type == 3) {
        if (!(_input_twin[item.id / 32] & (1u << (item.id % 32)))) return false;
    } else if (item.report_type != 1) {
        return false;
    }
    return host->inputReportAgeMs(item.id, now) < maxReportAgeMs();
}

void GenericDriver::collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const {
    if (!data.hasKey("ups.mfr") && host->_iManufacturer > 0) out.push_back(host->_iManufacturer);
    if (!data.hasKey("ups.model") && host->_iProduct > 0) out.push_back(host->_iProduct);
    if (!data.hasKey("ups.serial") && host->_iSerialNumber > 0) out.push_back(host->_iSerialNumber);
    if (!data.hasKey("battery.mfr.date") && _batteryDateStringIndex > 0) out.push_back(_batteryDateStringIndex);
}

// Queues the missing strings not requested yet in this cycle
void GenericDriver::appendNewStrings(IUSBHostUPS* host, const UPSData& data) {
    std::vector<uint8_t> strings;
    collectStringRequests(host, data, strings);
    for (uint8_t idx : strings) {
        if (std::find(_cycle_strings.begin(), _cycle_strings.end(), idx) != _cycle_strings.end()) continue;
        _cycle_strings.push_back(idx);
        _queue.push_back(PollItem{STRING_ITEM, idx, 0});
    }
}

void GenericDriver::startCycle(IUSBHostUPS* host, const UPSData& data, bool full, uint32_t now) {
    _queue.clear();
    _queue_pos = 0;
    _cycle_full = full;
    _strings_rechecked = false;
    _cycle_strings.clear();
    if (full) appendNewStrings(host, data);
    // Devices that reject GET_REPORT only send INPUT reports on the interrupt endpoint
    if (!(host->getQuirks() & QUIRK_NO_GET_REPORT)) {
        const auto& reports = full ? _full : _quick;
        _queue.insert(_queue.end(), reports.begin(), reports.end());
    }

    uint32_t stamp = now != 0 ? now : 1;
    _last_quick = stamp; // a full poll refreshes the status reports too
    if (full) _last_full = stamp;
    _step_now = true; // first request right away
}

void GenericDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    host->lock();
    if (data.get("ups.type") != upsTypeName()) {
        data.set("ups.type", upsTypeName());
    }
    onLoop(host, data);
    host->unlock();

    if (!_lists_built) {
        buildPollLists(host, _quick, _full);
        findInputTwins(host);
        findStaticReports(host);
        _lists_built = true;
    }

    // Some string indices are only known once the reports are decoded (iDeviceChemistry,
    // battery date): ask for them at the end of the same full poll, not 30 s later
    if (_queue_pos >= _queue.size() && _cycle_full && !_strings_rechecked) {
        _strings_rechecked = true;
        appendNewStrings(host, data);
    }
    if (_queue_pos >= _queue.size() && _cycle_full && _strings_rechecked) _first_full_done = true;

    if (_queue_pos >= _queue.size()) {
        bool full_due= _last_full == 0 || (now - _last_full) >= fullPollMs();
        bool quick_due = quickPollMs() > 0 && (now - _last_quick) >= quickPollMs();
        if (!full_due && !quick_due) return;
        startCycle(host, data, full_due, now);
    }

    if (_queue_pos >= _queue.size()) return;
    if (host->isPollingPaused()) return; // the cycle resumes where it stopped
    if (!_step_now && (now - _last_step) < stepSpacingMs()) return;

    // Skipped reports cost no request, so they take no step either
    while (_queue_pos < _queue.size() &&
           (isFreshFromInput(host, _queue[_queue_pos], now) || isStaticAndKnown(_queue[_queue_pos]))) {
        _queue_pos++;
    }
    if (_queue_pos >= _queue.size()) return;

    const PollItem item = _queue[_queue_pos++];
    _last_step = now;
    _step_now = false;

    if (item.report_type == STRING_ITEM) {
        host->requestStringDescriptor(item.id);
    } else {
        uint16_t len = item.length ? item.length : host->getHIDParser()->getExpectedLength(item.id, item.report_type);
        host->requestReport(item.id, item.report_type, requestLength(item.report_type, item.id, len));
    }
}

// ups.test.result text for a Test value, test_read_info in usbhid-ups.c; nullptr otherwise
static const char* testResultText(int v) {
    switch (v) {
    case 1: return "Done and passed";
    case 2: return "Done and warning";
    case 3: return "Done and error";
    case 4: return "Aborted";
    case 5: return "In progress";
    case 6: return "No test initiated";
    case 7: return "Test scheduled";
    default: return nullptr;
    }
}

void GenericDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;
    if (report_type == 1 || report_type == 3) {
        _decoded_report[typeSlot(report_type)][report_id / 32] |= 1u << (report_id % 32);
    }

    typedef UsageMapIndex<GenericDriver>::Mapping Mapping;
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
        // Standard PresentStatus flags rendered by computeUPSStatusString().
        // BatteryPresent is inverted, as upstream nobattery_info.
        { "UPS.PowerSummary.PresentStatus.BatteryPresent", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.no_battery", v == 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.remaining_time_limit_expired", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.FullyCharged", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.fully_charged", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.FullyDischarged", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.depleted", v != 0 ? "1" : "0"); } },
        
        { "UPS.PowerConverter.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.voltage", String(v, 1)); } },
        { "UPS.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.voltage", String(v, 1)); } },
        { "UPS.Flow.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.voltage", String(v, 1)); } },
        
        { "UPS.PowerConverter.Input.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.frequency", String(v, 1)); } },
        { "UPS.Input.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.frequency", String(v, 1)); } },
        { "UPS.Flow.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.frequency", String(v, 1)); } },
        
        { "UPS.PowerConverter.Output.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("output.voltage", String(v, 1)); } },
        { "UPS.Output.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("output.voltage", String(v, 1)); } },
        
        { "UPS.PowerConverter.Output.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("output.frequency", String(v, 1)); } },
        { "UPS.Output.Frequency", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("output.frequency", String(v, 1)); } },
        
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
            d.set("ups.temperature", String((v > 200.0) ? (v - 273.15) : v, 1));
        }},
        { "UPS.Battery.Temperature", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("battery.temperature", String((v > 200.0) ? (v - 273.15) : v, 1));
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
        
        // Tensione nominale della batteria, non della rete: così in tutti i sottodriver NUT
        // (apc-hid, cps-hid, mge-hid, ...). Issue 48: su un APC scriveva 12 in input.voltage.nominal.
        // Arrotondata come il "%.0f" di mge-hid/cps-hid/powercom-hid (lrint: pari sui .5, come printf),
        // non troncata (issue 67); i driver il cui sottodriver NUT usa "%.1f" la riscrivono con un decimale.
        { "UPS.PowerSummary.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage.nominal", String(lrint(v))); } },
        { "UPS.Battery.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage.nominal", String(lrint(v))); } },
        { "UPS.Flow.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.voltage.nominal", String((int)v)); } },
        { "UPS.Input.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.voltage.nominal", String((int)v)); } },
        
        { "UPS.PowerConverter.Output.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("output.voltage.nominal", String((int)v)); } },
        { "UPS.Output.ConfigVoltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("output.voltage.nominal", String((int)v)); } },
        
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.high", String((int)v)); } },
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.transfer.low", String((int)v)); } },
        
        { "UPS.PowerSummary.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && strcmp(def->path, drv->_active_beeper.c_str()) != 0) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if (v == 1) { d.set("ups.beeper.status", "disabled"); } 
            else if (v == 2) { d.set("ups.beeper.status", "enabled"); }
            else if (v == 3) { d.set("ups.beeper.status", "muted"); } // beeper_info in usbhid-ups
        } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && strcmp(def->path, drv->_active_beeper.c_str()) != 0) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if (v == 1) { d.set("ups.beeper.status", "disabled"); } 
            else if (v == 2) { d.set("ups.beeper.status", "enabled"); }
            else if (v == 3) { d.set("ups.beeper.status", "muted"); } // beeper_info in usbhid-ups
        } },
        { "UPS.AudibleAlarmControl", [](GenericDriver* drv, UPSData& d, double v, const HIDUsageDef* def) { 
            if (def && strcmp(def->path, drv->_active_beeper.c_str()) != 0) return;
            if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); }
            else if (v == 1) { d.set("ups.beeper.status", "disabled"); } 
            else if (v == 2) { d.set("ups.beeper.status", "enabled"); }
            else if (v == 3) { d.set("ups.beeper.status", "muted"); } // beeper_info in usbhid-ups
        } },
        
        { "UPS.BatterySystem.Battery.Test", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (const char* t = testResultText((int)v)) d.set("ups.test.result", t); } },
        { "UPS.Battery.Test", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (const char* t = testResultText((int)v)) d.set("ups.test.result", t); } },
        { "UPS.Output.Test", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { if (const char* t = testResultText((int)v)) d.set("ups.test.result", t); } },
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

    _generic_map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
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

