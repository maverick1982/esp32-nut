/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/cps-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2020 - 2025 Jim Klimov <jimklimov+nut@gmail.com>
 *                 2024 Alejandro González <me@alegon.dev>
 */

#include "CyberPowerDriver.h"
#include "IHidTransport.h"

/**
 * @brief CyberPower Driver Implementation
 *
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for CyberPower HID devices.
 * - Reference: nut_repo/drivers/cps-hid.c
 * - Every variable, the status and the instant commands come from cpsHid2Nut() (US-075,
 *   ADR 0011): the standard mappings of the same keys are superseded below, and the driver
 *   keeps only hooks (poll quirks, delays, the PID 0x0501 scale hook, vendor layouts).
 * - String Inversion: cps-hid handles UTF-16 inversion (handled generically via QUIRK_INVERT_STRINGS).
 * - cps_fix_report_desc is not needed: extractUsage() does not clip values to the
 *   Logical Maximum, so the wrong LogMax of the voltages and ConfigActivePower is harmless.
 */

// Traffic on EP0 is what makes these firmwares stall: status values come from the
// INPUT reports the UPS sends every few seconds, the rest is read every 30 s.
bool CyberPowerDriver::acceptPollReport(uint8_t report_type, uint8_t report_id) const {
    // Vendor-defined reports (>= 130) and reports 4 and 6 hang some firmwares
    return !(report_id >= FIRST_VENDOR_REPORT || report_id == 4 || report_id == 6);
}

bool CyberPowerDriver::hasVendorReports(IHidTransport* host) {
    for (const auto& u : host->getUsages()) {
        if (u.report_id >= FIRST_VENDOR_REPORT) return true;
    }
    return false;
}

// The standard mappings of GenericDriver::decodeReport() whose keys come from cpsHid2Nut(),
// copied character by character from that table (US-075)
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of cps_hid2nut set the internal ups.status.* keys
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
    // ups.realpower, ups.power
    "UPS.PowerConverter.Output.ActivePower", "UPS.Output.ActivePower",
    "UPS.PowerConverter.Output.ApparentPower", "UPS.Output.ApparentPower",
    // battery.voltage
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    // battery.charge, battery.charge.low, battery.runtime
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // ups.load
    "UPS.PowerSummary.PercentLoad", "UPS.PowerConverter.Output.PercentLoad", "UPS.Output.PercentLoad",
    // battery.mfr.date: the ManufacturerDate and iOEMInformation rows
    "UPS.Battery.ManufacturerDate", "UPS.BatterySystem.Battery.Date", "UPS.Battery.Date",
    // ups.temperature
    "UPS.PowerSummary.Temperature",
    // battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.FullChargeCapacity",
    // ups.realpower.nominal, ups.power.nominal
    "UPS.Flow.ConfigActivePower", "UPS.PowerConverter.ConfigActivePower", "UPS.Output.ConfigActivePower",
    "UPS.Flow.ConfigApparentPower",
    // battery.voltage.nominal, input.voltage.nominal, output.voltage.nominal
    "UPS.PowerSummary.ConfigVoltage", "UPS.Battery.ConfigVoltage",
    "UPS.Flow.ConfigVoltage", "UPS.Input.ConfigVoltage",
    "UPS.PowerConverter.Output.ConfigVoltage", "UPS.Output.ConfigVoltage",
    // input.transfer.high, input.transfer.low
    "UPS.PowerConverter.Output.HighVoltageTransfer", "UPS.PowerConverter.Output.LowVoltageTransfer",
    "UPS.Output.LowVoltageTransfer", "UPS.Input.LowVoltageTransfer",
    "UPS.Output.HighVoltageTransfer", "UPS.Input.HighVoltageTransfer",
    // ups.beeper.status
    "UPS.PowerSummary.AudibleAlarmControl", "UPS.BatterySystem.Battery.AudibleAlarmControl", "UPS.AudibleAlarmControl",
    // ups.test.result
    "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test",
    // ups.timer.shutdown
    "UPS.PowerSummary.DelayBeforeShutdown",
    // Kept (ADR 0011 rule 2, standard names cps-hid does not read): ups.mfr.date from
    // ManufacturerDate, battery.capacity from BatterySystem.Battery.DesignCapacity,
    // battery.temperature from Battery.Temperature
};

const char* const* CyberPowerDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

void CyberPowerDriver::decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (!host) return;
    // cps_usb_device_table: cps_battery_scale() is the hook of PID 0x0501 only (battery
    // voltage and frequency scales, read by the cpsHid2Nut() conversions)
    _nut_ctx.cps.might_need_scale = host->getPID() == 0x0501;
    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);
}
