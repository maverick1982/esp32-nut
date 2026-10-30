/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/ecoflow-hid.c:
 *   Copyright (C) 2003 - 2012 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2013 Charles Lepple <clepple+nut@gmail.com>
 *                 2024 Yevgeniy Kuksenko <2882631+ykuksenko@users.noreply.github.com>
 *                 2025 M. Greyson Christoforo <grey@christoforo.net>
 */

/**
 * @brief EcoFlow Driver Implementation
 *
 * Every value and status bit comes from NutTable::ecoflowHid2Nut(), the rows of
 * ecoflow_hid2nut in nut_repo/drivers/ecoflow-hid.c (US-078, ADR 0011), vendor ID 0x3746.
 * What is left here are the hooks that are not table rows:
 * - no beeper control and no load/shutdown commands: upstream leaves them commented out
 *   (US-054, US-058);
 * - ecoflow_format_mfr (formatDeviceStrings): "EcoFlow" when the device has no manufacturer
 *   string, or an empty one.
 * Standard HID usages without an ecoflow-hid row keep the genericHid2Nut() rows
 * (ADR 0011 rule 2), for example ups.realpower.nominal from UPS.Flow.ConfigActivePower.
 */

#include "EcoFlowDriver.h"

// The genericHid2Nut() rows (US-079; standard mappings of GenericDriver before) whose keys come from
// ecoflowHid2Nut(), copied character by character from that table (US-078)
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of ecoflow_hid2nut set the internal ups.status.* keys
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
    // battery.voltage
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    // battery.charge, battery.charge.low, battery.runtime
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // ups.mfr.date
    "UPS.PowerSummary.ManufacturerDate", "UPS.ManufacturerDate",
    // battery.capacity, and battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.DesignCapacity", "UPS.BatterySystem.Battery.FullChargeCapacity",
    // ups.power.nominal
    "UPS.Flow.ConfigApparentPower",
    // battery.voltage.nominal
    "UPS.PowerSummary.ConfigVoltage", "UPS.Battery.ConfigVoltage",
    // ups.beeper.status
    "UPS.PowerSummary.AudibleAlarmControl", "UPS.BatterySystem.Battery.AudibleAlarmControl", "UPS.AudibleAlarmControl",
    // ups.timer.shutdown: only UPS.OutletSystem.Outlet.DelayBeforeShutdown (ecoflow-hid.c:127);
    // the PowerSummary one is an unmapped row upstream ("does not seem to work")
    "UPS.PowerSummary.DelayBeforeShutdown",
    // Kept (ADR 0011 rule 2, standard names ecoflow-hid does not read): ups.realpower.nominal
    // from UPS.Flow.ConfigActivePower, battery.mfr.date, the temperatures, the PowerConverter,
    // Input and Output measures
};

const char* const* EcoFlowDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

// ecoflow_format_mfr() returns hd->Vendor, or "EcoFlow" without one. A manufacturer string
// that decodes to empty counts as missing. Called after the strings of the device info and
// after every string descriptor, so a real vendor string still replaces the fallback.
void EcoFlowDriver::formatDeviceStrings(UPSData& data) {
    if (data.get("ups.mfr").length() == 0) data.set("ups.mfr", "EcoFlow");
}
