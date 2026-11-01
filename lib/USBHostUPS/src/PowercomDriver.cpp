/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/powercom-hid.c:
 *   Copyright (C) 2003 - 2009 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2020 - 2024 Jim Klimov <jimklimov+nut@gmail.com>
 */

/**
 * @brief Powercom Driver Implementation
 *
 * Every value, status bit and instant command comes from NutTable::powercomHid2Nut(), the
 * rows of powercom_hid2nut in nut_repo/drivers/powercom-hid.c (US-077, ADR 0011). What is
 * left here are the hooks that are not table rows:
 * - poll policy (issue #36): 800 ms between requests, the 0x0A keep-alive, the reports of
 *   the bound rows (static ones once, the BOOL ones in the quick poll) and the legacy
 *   report 0xA4, whose text battery.voltage serves (powercom_hack_voltage);
 * - beeper encoding: the values of the beeper.enable / beeper.disable rows, 1 and 0
 *   (powercom-hid.c:659-660), although powercom_beeper_info reads 2 as disabled;
 * - powercom_format_mfr / powercom_format_model / powercom_format_serial
 *   (formatDeviceStrings): the USB strings, "PowerCOM" when there is no manufacturer
 *   string, no ups.model / ups.serial without theirs; the string descriptors are not
 *   requested again by the poll;
 * - the delays of US-066. The SPD-750U offers the beeper commands as in NUT (US-109).
 * Out of scope: powercom_claim (interrupt_only for PID 0x0001, PID 0x0002 rejected) and
 * fix_report_desc.
 */

#include "PowercomDriver.h"
#include "IHidTransport.h"
#include "HIDParser.h"
#include <cctype>

PowercomDriver::PowercomDriver() : _status_report_len(0) {}

void PowercomDriver::setup() {
    GenericDriver::setup();
    _status_report_len = 0;
}

// powercom_format_mfr (powercom-hid.c:739): hd->Vendor, "PowerCOM" without it. ups.model and
// ups.serial are hd->Product and hd->Serial (powercom_format_model/_serial): the USB strings
// as they are, and no variable when the device has none (usbhid-ups.c:2179-2195)
void PowercomDriver::formatDeviceStrings(UPSData& data) {
    if (!data.hasKey("ups.mfr")) data.set("ups.mfr", "PowerCOM");
}

// The row has a usage on the attached device, in any report type
static bool isBound(const std::vector<HIDUsageDef>& usages, const NutTable::HidInfo& row) {
    for (const auto& u : usages) {
        if (NutTable::pathMatches(u, row.hidpath)) return true;
    }
    return false;
}

// Quick poll: every 2 s the status keep-alive (report 0x0A, 8 bytes, issue #36) and the
// reports of the bound BOOL rows. The engine applies the BOOL rows again in table order after
// each report, so a stale UPS.PresentStatus report (rows 557-568, after the PowerSummary ones)
// would hide an OB from report 0x0A until its next read: it is read as often (US-077), and
// right after report 0x0A changes, also in the middle of a full poll (decodeReport).
// Full poll: the same, then the reports of the other bound rows in table order, then the
// legacy report 0xA4 (8 bytes, powercom_hack_voltage). A row whose variable an earlier bound
// row already provides is never read (upstream drops it at INIT); static reports are read
// once (pollStaticReportsOnce). Input voltage, output voltage and load keep the 8-byte
// requests of issue #36.
void PowercomDriver::buildPollLists(IHidTransport* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const {
    const auto& usages = host->getUsages();
    quick = { PollItem{3, 0x0A, 8} };
    appendTableReports(usages, quick, [](const NutTable::HidInfo& row, size_t) {
        return strcmp(row.info_type, "BOOL") == 0;
    });

    full = quick;
    size_t n = 0;
    const NutTable::HidInfo* table = hid2nut(n);
    auto value_row = [](const NutTable::HidInfo& row) {
        return !NutTable::isInternalRow(row) && !NutTable::isCommandRow(row) &&
               !(row.hidflags & NutTable::HU_FLAG_ABSENT);
    };
    appendTableReports(usages, full, [&](const NutTable::HidInfo& row, size_t i) {
        if (!value_row(row)) return false;
        for (size_t j = 0; j < i; j++) {
            if (value_row(table[j]) && strcmp(table[j].info_type, row.info_type) == 0 && isBound(usages, table[j]))
                return false;
        }
        return true;
    });
    for (auto& item : full) {
        if (item.id == 0x1D || item.id == 0x1F || item.id == 0x21) item.length = 8;
    }
    // Unmapped in NUT too: read as FEATURE of exactly 8 bytes (powercom-hid.c:457)
    full.push_back(PollItem{3, 0xA4, 8});
}

// Static when every value row on the usage is HU_FLAG_STATIC (a usage without rows is never
// read by the table, so it does not make its report dynamic)
bool PowercomDriver::isStaticUsage(const HIDUsageDef& u) const {
    size_t n = 0;
    const NutTable::HidInfo* table = hid2nut(n);
    for (size_t i = 0; i < n; i++) {
        if (NutTable::isCommandRow(table[i]) || !NutTable::pathMatches(u, table[i].hidpath)) continue;
        if (!(table[i].hidflags & NutTable::HU_FLAG_STATIC)) return false;
    }
    return true;
}

void PowercomDriver::decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

#ifdef USBUPS_DEBUG_LOG
    // Raw dump of every INPUT report: only in debug builds, it allocates on each report
    if (report_type == 1) { // Interrupt IN report
        String hex = "";
        for (size_t i = 0; i < length && i < 16; i++) {
            char b[4];
            sprintf(b, "%02X ", data[i]);
            hex += b;
        }
        host->logDebug("[PowercomDriver] INT IN (len " + String((int)length) + "): " + hex);
    }
#endif

    if (report_id == 0xA4 && report_type == 3) {
        // powercom_hack_voltage (powercom-hid.c:432-493): " 13.7 2" -> "13.7", kept as the
        // last valid reading only when it has a dot; Conv::PowercomHackVoltage serves it
        char msg[9];
        size_t j = 0;
        size_t start_idx = (data[0] == 0xA4) ? 1 : 0; // the report ID, when the host keeps it
        for (size_t i = start_idx; i < length && i < 8; i++) msg[j++] = (char)data[i];
        msg[j] = '\0';
        char* start = msg;
        while (*start && !std::isdigit((unsigned char)*start) && *start != '.') start++;
        char* end = start;
        while (*end && (std::isdigit((unsigned char)*end) || *end == '.')) end++;
        *end = '\0';
#ifdef USBUPS_DEBUG_LOG
        host->logDebug("[PowercomDriver] 0xA4 text: '" + String(msg) + "'");
#endif
        if (*start && strchr(start, '.')) {
            snprintf(_nut_ctx.powercom_battery_voltage, sizeof(_nut_ctx.powercom_battery_voltage), "%s", start);
        }
        return;
    }

    // A new status in report 0x0A (INPUT, or the keep-alive) would stay hidden by the older
    // UPS.PresentStatus report (rows 557-568 come later in the table and the BOOL rows are
    // applied again in table order) until that report is read: during a full poll that can
    // be 15 requests away. Read the other status reports next, as the quick poll would (US-077)
    if (report_id == 0x0A) {
        size_t n = length < sizeof(_status_report) ? length : sizeof(_status_report);
        // The first value is no change: every status report is in the first poll anyway
        bool changed = _status_report_len != 0 && (n != _status_report_len || memcmp(_status_report, data, n) != 0);
        memcpy(_status_report, data, n);
        _status_report_len = (uint8_t)n;
        if (changed) {
            std::vector<PollItem> status;
            appendTableReports(host->getUsages(), status, [](const NutTable::HidInfo& row, size_t) {
                return strcmp(row.info_type, "BOOL") == 0;
            });
            for (auto it = status.rbegin(); it != status.rend(); ++it) {
                if (it->id != 0x0A) pollSoon(*it);
            }
        }
    }

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);
}

// The genericHid2Nut() rows whose keys powercomHid2Nut() owns (US-077,
// ADR 0011), copied from there
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of powercom_hid2nut set the internal ups.status.* keys
    "UPS.PowerSummary.PresentStatus.ACPresent", "UPS.PowerSummary.ACPresent",
    "UPS.PowerSummary.PresentStatus.Discharging", "UPS.PowerSummary.Discharging",
    "UPS.PowerSummary.PresentStatus.Charging", "UPS.PowerSummary.Charging",
    "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", "UPS.PowerSummary.BelowRemainingCapacityLimit",
    "UPS.PowerSummary.PresentStatus.NeedReplacement", "UPS.PowerSummary.NeedReplacement",
    "UPS.PowerSummary.PresentStatus.Overload", "UPS.PowerSummary.Overload",
    "UPS.PowerSummary.PresentStatus.ShutdownImminent", "UPS.PowerSummary.ShutdownImminent",
    "UPS.PowerSummary.PresentStatus.CommunicationLost", "UPS.PowerSummary.CommunicationLost",
    "UPS.PowerSummary.PresentStatus.BatteryPresent", "UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired",
    "UPS.PowerSummary.PresentStatus.FullyCharged", "UPS.PowerSummary.PresentStatus.FullyDischarged",
    // input.voltage, input.frequency, output.voltage, output.frequency
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Input.Frequency", "UPS.Input.Frequency", "UPS.Flow.Frequency",
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    "UPS.PowerConverter.Output.Frequency", "UPS.Output.Frequency",
    // battery.voltage: on PowerCOM these duplicate output.voltage (powercom-hid.c:574-581)
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    // battery.charge, battery.charge.low, battery.runtime
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // ups.load
    "UPS.PowerSummary.PercentLoad", "UPS.PowerConverter.Output.PercentLoad", "UPS.Output.PercentLoad",
    // ups.mfr.date, battery.mfr.date
    "UPS.PowerSummary.ManufacturerDate", "UPS.ManufacturerDate",
    "UPS.Battery.ManufacturerDate", "UPS.BatterySystem.Battery.Date", "UPS.Battery.Date",
    // battery.temperature
    "UPS.Battery.Temperature",
    // battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.FullChargeCapacity",
    // battery.voltage.nominal: the output nominal voltage on PowerCOM (powercom-hid.c:574-581)
    "UPS.PowerSummary.ConfigVoltage", "UPS.Battery.ConfigVoltage",
    // input.voltage.nominal, output.voltage.nominal
    "UPS.Flow.ConfigVoltage", "UPS.Input.ConfigVoltage",
    "UPS.PowerConverter.Output.ConfigVoltage", "UPS.Output.ConfigVoltage",
    // ups.beeper.status
    "UPS.PowerSummary.AudibleAlarmControl", "UPS.BatterySystem.Battery.AudibleAlarmControl", "UPS.AudibleAlarmControl",
    // ups.test.result
    "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test",
    // ups.timer.shutdown
    "UPS.PowerSummary.DelayBeforeShutdown",
    // Kept (ADR 0011 rule 2, standard names powercom-hid does not read): ups.realpower and
    // ups.power, ups.realpower.nominal and ups.power.nominal, ups.temperature
    // (PowerSummary.Temperature), battery.capacity, input.transfer.*, and the ups.realpower
    // estimate. They appear only when their report is polled: on the SPD-750U none is.
};

const char* const* PowercomDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

// The values of the beeper.enable / beeper.disable rows (powercom-hid.c:659-660)
uint8_t PowercomDriver::encodeBeeperValue(bool enable, uint16_t bit_size) const {
    (void)bit_size;
    return enable ? 1 : 0;
}
