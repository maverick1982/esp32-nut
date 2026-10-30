/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/liebert-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2007 Charles Lepple <clepple@gmail.com>
 *                 2018 Markus "Links2004" <help.markus+gitk@gmail.com>
 *                 2023 Blaž Zakrajšek <zakrajsek.blaz@gmail.com>
 */

#include "LiebertDriver.h"

/**
 * @brief Liebert / Phoenixtec Driver Implementation
 *
 * Every value, status bit and instant command comes from NutTable::liebertHid2Nut(), the rows of
 * liebert_hid2nut in nut_repo/drivers/liebert-hid.c (US-106, ADR 0011), vendor ID 0x06DA,
 * product 0xFFFF; DriverRegistry leaves the AEG (mge-hid.c) and GoldenMate (idowell-hid.c)
 * devices to the other drivers. Deviations from liebert-hid.c (US-097, kept by US-106):
 * 1. beeper.toggle keeps the behavior of the project (reads ups.beeper.status and writes the
 *    opposite): liebert-hid.c always writes 1, "disabled" in the standard HID encoding.
 * 2. Standard HID usages without a liebert-hid row keep the genericHid2Nut() rows (ADR 0011 rule 2),
 *    for example ups.timer.shutdown and ups.realpower.
 * 3. shutdown.default is offered, as for every driver of the project (US-058).
 * Since US-106 PresentStatus.Good gives OFF and InternalFailure gives "Internal UPS fault!", as
 * usbhid-ups (user decision of 2026-10-30). Hooks that are not table rows: liebert_format_mfr
 * (formatDeviceStrings) and the delays of US-066.
 */

// The genericHid2Nut() rows (US-079; standard mappings of GenericDriver before) whose keys come from
// liebertHid2Nut(), copied character by character from that table (US-106)
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of liebert_hid2nut set these internal ups.status.* keys
    "UPS.PowerSummary.PresentStatus.ACPresent", "UPS.PowerSummary.ACPresent",
    "UPS.PowerSummary.PresentStatus.Discharging", "UPS.PowerSummary.Discharging",
    "UPS.PowerSummary.PresentStatus.Charging", "UPS.PowerSummary.Charging",
    "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", "UPS.PowerSummary.BelowRemainingCapacityLimit",
    "UPS.PowerSummary.PresentStatus.Overload", "UPS.PowerSummary.Overload",
    "UPS.PowerSummary.PresentStatus.ShutdownImminent", "UPS.PowerSummary.ShutdownImminent",
    // battery.voltage, battery.charge, battery.runtime
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    "UPS.PowerSummary.RemainingCapacity",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // ups.load, ups.power.nominal
    "UPS.PowerSummary.PercentLoad", "UPS.PowerConverter.Output.PercentLoad", "UPS.Output.PercentLoad",
    "UPS.Flow.ConfigApparentPower",
    // ups.test.result, ups.beeper.status
    "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test",
    "UPS.PowerSummary.AudibleAlarmControl", "UPS.BatterySystem.Battery.AudibleAlarmControl", "UPS.AudibleAlarmControl",
    // output.voltage, output.voltage.nominal (UPS.Flow.[4].ConfigVoltage, input.voltage.nominal
    // in genericHid2Nut()), output.frequency
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    "UPS.Flow.ConfigVoltage", "UPS.PowerConverter.Output.ConfigVoltage", "UPS.Output.ConfigVoltage",
    "UPS.PowerConverter.Output.Frequency", "UPS.Output.Frequency",
    // input.voltage, input.frequency
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Input.Frequency", "UPS.Input.Frequency", "UPS.Flow.Frequency",
    // input.transfer.*: the vendor usages of rows 115-116; PowerConverter.Output.*VoltageTransfer
    // is output.transfer.* upstream (rows 108-109)
    "UPS.PowerConverter.Output.HighVoltageTransfer", "UPS.PowerConverter.Output.LowVoltageTransfer",
    "UPS.Output.LowVoltageTransfer", "UPS.Input.LowVoltageTransfer",
    "UPS.Output.HighVoltageTransfer", "UPS.Input.HighVoltageTransfer",
    // battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.FullChargeCapacity",
    // Kept (ADR 0011 rule 2, standard names liebert-hid does not read): ups.timer.shutdown from
    // PowerSummary.DelayBeforeShutdown, ups.realpower.nominal from Flow.ConfigActivePower, the
    // dates and temperatures
};

const char* const* LiebertDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

// liebert_format_mfr() returns hd->Vendor, or "Liebert" without one. A manufacturer string
// that decodes to empty counts as missing.
void LiebertDriver::formatDeviceStrings(UPSData& data) {
    if (data.get("ups.mfr").length() == 0) data.set("ups.mfr", "Liebert");
}
