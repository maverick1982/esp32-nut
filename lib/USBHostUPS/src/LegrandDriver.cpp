/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/legrand-hid.c:
 *   Copyright (C) 2003 - 2012 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2013 Charles Lepple <clepple+nut@gmail.com>
 *                 2018 Gabriele Taormina <gabriele.taormina@legrand.com>, for Legrand
 */

#include "LegrandDriver.h"
#include "IHidTransport.h"
#include "HIDParser.h"

/**
 * @brief Legrand Driver Implementation
 *
 * Every value, status bit and instant command comes from NutTable::legrandHid2Nut(), the rows
 * of legrand_hid2nut in nut_repo/drivers/legrand-hid.c (US-106, ADR 0011), vendor ID 0x1CB0,
 * products 0x0038 (Keor PDU) and 0x0032 (Keor SP). Deviations from legrand-hid.c (US-087,
 * decided on 2026-10-18, kept by US-106):
 * 1. No legrand_times10/100k/1M/10M nor divide_by_10_conversion: on the Keor PDU 800 they give
 *    values off by a factor of 10 or more with NUT too. The rows keep the value of the HID unit
 *    exponent, as libhid.c computes it, with the "%0.1f" of the excluded function.
 * 2. beeper.toggle is offered, as for every driver of the project.
 * 3. Standard HID usages without a legrand-hid row keep the genericHid2Nut() rows (ADR 0011
 *    rule 2), for example input.frequency and ups.power.nominal.
 * 4. Other Legrand products stay with GenericDriver (DriverRegistry), where upstream rejects
 *    them unless the productid option is given.
 * The beeper follows legrand-hid.c of NUT 1c4ab78 (US-143): ups.beeper.status with beeper_info,
 * beeper.disable 1, enable 2, mute 3. Up to 6c63f77 legrand-hid.c wrote 3 for beeper.disable,
 * published no status and had no beeper.mute, and the port decoded 3 as disabled (the former
 * deviation 2, US-087).
 * Hooks that are not table rows: legrand_format_mfr (formatDeviceStrings), the delays of US-066.
 */

// The genericHid2Nut() rows (US-079; standard mappings of GenericDriver before) whose keys come from
// legrandHid2Nut(), copied character by character from that table (US-106)
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of legrand_hid2nut set these internal ups.status.* keys
    "UPS.PowerSummary.PresentStatus.ACPresent", "UPS.PowerSummary.ACPresent",
    "UPS.PowerSummary.PresentStatus.Discharging", "UPS.PowerSummary.Discharging",
    "UPS.PowerSummary.PresentStatus.Charging", "UPS.PowerSummary.Charging",
    "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", "UPS.PowerSummary.BelowRemainingCapacityLimit",
    "UPS.PowerSummary.PresentStatus.Overload", "UPS.PowerSummary.Overload",
    // input.voltage, input.transfer.*, input.voltage.nominal
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Output.HighVoltageTransfer", "UPS.PowerConverter.Output.LowVoltageTransfer",
    "UPS.Output.LowVoltageTransfer", "UPS.Input.LowVoltageTransfer",
    "UPS.Output.HighVoltageTransfer", "UPS.Input.HighVoltageTransfer",
    "UPS.Flow.ConfigVoltage", "UPS.Input.ConfigVoltage",
    // battery.voltage, battery.voltage.nominal
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    "UPS.PowerSummary.ConfigVoltage", "UPS.Battery.ConfigVoltage",
    // battery.charge, battery.charge.low, battery.runtime
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // output.voltage, output.frequency
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    "UPS.PowerConverter.Output.Frequency", "UPS.Output.Frequency",
    // ups.load
    "UPS.PowerSummary.PercentLoad", "UPS.PowerConverter.Output.PercentLoad", "UPS.Output.PercentLoad",
    // ups.realpower.nominal
    "UPS.Flow.ConfigActivePower", "UPS.PowerConverter.ConfigActivePower", "UPS.Output.ConfigActivePower",
    // ups.beeper.status
    "UPS.PowerSummary.AudibleAlarmControl", "UPS.BatterySystem.Battery.AudibleAlarmControl", "UPS.AudibleAlarmControl",
    // battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.FullChargeCapacity",
    // Kept (ADR 0011 rule 2, standard names legrand-hid does not read): input.frequency,
    // ups.power.nominal from Flow.ConfigApparentPower, the FullyCharged and
    // RemainingTimeLimitExpired flags, the dates, temperatures and timers
};

const char* const* LegrandDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

// legrand_format_mfr() returns hd->Vendor, or "Legrand" without one. A manufacturer string
// that decodes to empty counts as missing.
void LegrandDriver::formatDeviceStrings(UPSData& data) {
    if (data.get("ups.mfr").length() == 0) data.set("ups.mfr", "Legrand");
}
