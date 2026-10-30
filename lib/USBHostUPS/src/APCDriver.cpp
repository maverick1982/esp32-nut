/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/apc-hid.c:
 *   Copyright (C) 2003 - 2009 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 John Stamp <kinsayder@hotmail.com>
 *                 2005 Peter Selinger <selinger@users.sourceforge.net>
 *                 2009 - 2010 Arjen de Korte <adkorte-guest@alioth.debian.org>
 */

#include "APCDriver.h"
#include "IHidTransport.h"
#include <string.h>

/**
 * @brief APC Driver Implementation
 *
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for APC HID devices.
 * - Reference: nut_repo/drivers/apc-hid.c
 * - Every variable, the status and the instant commands come from apcHid2Nut() (US-076,
 *   ADR 0011): the genericHid2Nut() rows of the same keys are superseded below, and the driver
 *   keeps only hooks: apc_format_model() (splitProduct, issue #76) and the Back-UPS BX poll
 *   and request length (issue #60).
 * - apc_fix_report_desc is not needed: it widens LogMin/LogMax of UPS.Input.Voltage on the
 *   Back-UPS XS 1400U, and extractUsage() never clips a value to the Logical Maximum.
 * - Not replicated: general_apc_check() (max_report_size for "Back-UPS ES 525" and "CS"
 *   firmwares, here only requestLength() of the BX) and disable_interrupt_pipe() of PID
 *   0x0003/0x0004.
 */

APCDriver::APCDriver() : _back_ups_bx(false) {}

void APCDriver::setup() {
    GenericDriver::setup();
    _back_ups_bx = false;
}

bool APCDriver::splitProduct(const String& product, String& model, String& firmware, String& aux) {
    int fw = product.indexOf("FW:");
    if (fw < 0) return false;
    // apc_format_model() cuts the character before "FW:", normally a space
    model = product.substring(0, fw > 0 ? fw - 1 : 0);
    String rest = product.substring(fw + 3);
    int usb = rest.indexOf("USB FW:");
    if (usb >= 0) {
        firmware = rest.substring(0, usb > 0 ? usb - 1 : 0);
        aux = rest.substring(usb + 7);
    } else {
        firmware = rest;
        aux = "";
    }
    model.trim();
    firmware.trim();
    aux.trim();
    return true;
}

void APCDriver::formatDeviceStrings(UPSData& data) {
    String model, firmware, aux;
    if (!splitProduct(data.get("ups.model"), model, firmware, aux)) return;
    if (model.length() > 0) data.set("ups.model", model);
    if (firmware.length() > 0) data.set("ups.firmware", firmware);
    if (aux.length() > 0) data.set("ups.firmware.aux", aux);
}

void APCDriver::onLoop(IHidTransport* host, UPSData& data) {
    // ups.model comes from the product string, read at claim time
    _back_ups_bx = isBackUpsBX(data.get("ups.model"));
}

// The genericHid2Nut() rows (US-079; standard mappings of GenericDriver before) whose keys come from apcHid2Nut(),
// copied character by character from that table (US-076)
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of apc_hid2nut set the internal ups.status.* keys
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
    // input.voltage, output.voltage, output.frequency
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    "UPS.PowerConverter.Output.Frequency", "UPS.Output.Frequency",
    // battery.voltage
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
    // ups.realpower.nominal
    "UPS.Flow.ConfigActivePower", "UPS.PowerConverter.ConfigActivePower", "UPS.Output.ConfigActivePower",
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
    // Kept (ADR 0011 rule 2, standard names apc-hid does not read): input.frequency,
    // ups.realpower (ActivePower) and ups.power (ApparentPower), ups.power.nominal
    // (Flow.ConfigApparentPower), ups.temperature, battery.capacity, and the ups.realpower
    // estimate of the models that do not measure it
};

const char* const* APCDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}
