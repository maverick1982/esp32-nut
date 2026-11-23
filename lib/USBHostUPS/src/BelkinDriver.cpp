/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/belkin-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 Peter Selinger <selinger@users.sourceforge.net>
 *                 2011, 2014 Charles Lepple <clepple+nut@gmail.com>
 *                 2024 James R. Parks <jrjparks@zathera.com>
 *                 2024 - 2026 Jim Klimov <jimklimov+nut@gmail.com>
 */

#include "BelkinDriver.h"

/**
 * @brief Belkin / Liebert / Vertiv Driver Implementation
 *
 * Every value, status bit and instant command comes from NutTable::belkinHid2Nut(), the rows of
 * belkin_hid2nut in nut_repo/drivers/belkin-hid.c (ADR 0003, ADR 0011 rule 4). DriverRegistry
 * gives it the devices of belkin_usb_device_table that belkin_claim() takes without the
 * productid option: the listed Belkin products (050d) and Liebert 10af:0000, 0001, 0002, 0004
 * and 0008 (GXT4, PowerSure PSA, PowerSure PST / PSI5 / Vertiv Edge, PSI 1440, GXT3). The other
 * products of both vendors stay with GenericDriver, as upstream rejects them; 10af:1000 (GXT5)
 * belongs to vertiv-hid.c, which is not ported.
 *
 * Hooks that are not table rows:
 * - belkin_format_mfr / belkin_format_model (formatDeviceStrings): the Vendor string without its
 *   leading blanks, or "Belkin"; the Product string, or "unknown". belkin_format_serial is the
 *   ups.serial row of the table (UPS.PowerSummary.iSerialNumber) when the device has no serial.
 * - the ups.delay.* rows (belkinDelayPaths(), US-066).
 * Not ported: the usb_hid_rep_index=1 default of belkin_claim() for 10af:0002. Upstream needs it
 * because interface 0 of the PSI5 has only a vendor report; USBHostUPS already ignores an
 * interface without UPS or Battery pages and binds the one with the Power Device descriptor.
 * Standard HID usages without a belkin-hid row keep the genericHid2Nut() rows (ADR 0011 rule 2),
 * for example ups.status.fully_charged, ups.realpower.nominal and the temperatures.
 */

// The genericHid2Nut() rows whose keys come from belkinHid2Nut(), copied character by character
// from that table
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of belkin_hid2nut set these internal ups.status.* keys
    "UPS.PowerSummary.PresentStatus.ACPresent", "UPS.PowerSummary.ACPresent",
    "UPS.PowerSummary.PresentStatus.Discharging", "UPS.PowerSummary.Discharging",
    "UPS.PowerSummary.PresentStatus.Charging", "UPS.PowerSummary.Charging",
    "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", "UPS.PowerSummary.BelowRemainingCapacityLimit",
    "UPS.PowerSummary.PresentStatus.NeedReplacement", "UPS.PowerSummary.NeedReplacement",
    "UPS.PowerSummary.PresentStatus.Overload", "UPS.PowerSummary.Overload",
    "UPS.PowerSummary.PresentStatus.ShutdownImminent", "UPS.PowerSummary.ShutdownImminent",
    "UPS.PowerSummary.PresentStatus.CommunicationLost", "UPS.PowerSummary.CommunicationLost",
    "UPS.PowerSummary.PresentStatus.FullyDischarged",
    // input.voltage, input.frequency, output.voltage, output.frequency
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Input.Frequency", "UPS.Input.Frequency", "UPS.Flow.Frequency",
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    "UPS.PowerConverter.Output.Frequency", "UPS.Output.Frequency",
    // battery.voltage, battery.charge, battery.charge.low, battery.runtime
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // ups.load
    "UPS.PowerSummary.PercentLoad", "UPS.PowerConverter.Output.PercentLoad", "UPS.Output.PercentLoad",
    // ups.mfr.date
    "UPS.PowerSummary.ManufacturerDate", "UPS.ManufacturerDate",
    // battery.capacity: UPS.Flow.ConfigApparentPower upstream (belkin-hid.c:528)
    "UPS.BatterySystem.Battery.DesignCapacity",
    // ups.power.nominal: BELKINConfigApparentPower upstream (belkin-hid.c:505)
    "UPS.Flow.ConfigApparentPower",
    // battery.voltage.nominal, input.voltage.nominal, output.voltage.nominal
    "UPS.PowerSummary.ConfigVoltage", "UPS.Battery.ConfigVoltage",
    "UPS.Flow.ConfigVoltage", "UPS.Input.ConfigVoltage",
    "UPS.PowerConverter.Output.ConfigVoltage", "UPS.Output.ConfigVoltage",
    // input.transfer.high, input.transfer.low
    "UPS.PowerConverter.Output.HighVoltageTransfer", "UPS.PowerConverter.Output.LowVoltageTransfer",
    "UPS.Output.LowVoltageTransfer", "UPS.Input.LowVoltageTransfer",
    "UPS.Output.HighVoltageTransfer", "UPS.Input.HighVoltageTransfer",
    // ups.test.result, ups.timer.shutdown
    "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test",
    "UPS.PowerSummary.DelayBeforeShutdown",
    // Kept (ADR 0011 rule 2, standard names belkin-hid does not read): ups.status.fully_charged,
    // ups.status.no_battery, ups.status.remaining_time_limit_expired, ups.realpower,
    // ups.realpower.nominal, ups.power, battery.mfr.date and the temperatures
};

const char* const* BelkinDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

// belkin_format_mfr() returns hd->Vendor without its leading blanks, or "Belkin" when there is
// none or it is empty; belkin_format_model() returns hd->Product, or "unknown". Called after the
// strings of the device info and after every string descriptor, so a real string still
// replaces the fallback.
void BelkinDriver::formatDeviceStrings(UPSData& data) {
    String mfr = data.get("ups.mfr");
    unsigned int i = 0;
    while (i < mfr.length() && mfr[i] == ' ') i++;
    if (i == mfr.length()) data.set("ups.mfr", "Belkin");
    else if (i > 0) data.set("ups.mfr", mfr.substring(i));
    if (data.get("ups.model").length() == 0) data.set("ups.model", "unknown");
}
