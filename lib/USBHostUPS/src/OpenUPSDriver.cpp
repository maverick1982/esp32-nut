/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/openups-hid.c:
 *   Copyright (C) 2003 - 2012 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2012 Nicu Pavel <npavel@mini-box.com>
 */

/**
 * @brief OpenUPS Driver Implementation (WalleCube W150)
 *
 * Every value and status bit comes from NutTable::openupsHid2Nut(), the rows of
 * openups_hid2nut in nut_repo/drivers/openups-hid.c (US-078, ADR 0011), vendor ID 0x04D8.
 * What is left here are the hooks that are not table rows:
 * - get_voltage_multiplier(): the voltage and current scales of the product ID, read by the
 *   table conversions (Context::openups);
 * - PresentStatus.BatteryPresent ignored (deviation, issue #26, US-055): some units report
 *   0 with the battery fitted, so the nobattery bit of row 337 is dropped and ups.status
 *   never shows RB for it;
 * - ups.load, ups.realpower.nominal and ups.realpower estimated from the output voltage and
 *   current (extension with standard names, ADR 0011 rule 2: openups-hid.c has none of them);
 * - openups_format_mfr (formatDeviceStrings): "openUPS" when the device has no manufacturer
 *   string.
 */

#include "OpenUPSDriver.h"
#include "IHidTransport.h"

// The standard mappings of GenericDriver::decodeReport() whose keys come from
// openupsHid2Nut(), copied character by character from that table (US-078)
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of openups_hid2nut set the internal ups.status.* keys
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
    // input.voltage, output.voltage
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    // battery.voltage
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    // battery.charge, battery.charge.low, battery.runtime
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // battery.mfr.date: the iOEMInformation row
    "UPS.Battery.ManufacturerDate", "UPS.BatterySystem.Battery.Date", "UPS.Battery.Date",
    // battery.temperature; UPS.PowerSummary.Temperature is the thermistor reading of row 309,
    // not a temperature: no ups.temperature from it
    "UPS.PowerSummary.Temperature", "UPS.Battery.Temperature",
    // battery.capacity, and battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.DesignCapacity", "UPS.BatterySystem.Battery.FullChargeCapacity",
    // Kept (ADR 0011 rule 2, standard names openups-hid does not read): the nominal values,
    // frequencies and powers of the PowerConverter, Input, Output and Flow usages
};

const char* const* OpenUPSDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

void OpenUPSDriver::decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (!host) return;
    // get_voltage_multiplier() (openups-hid.c:50-72), the hook of both product IDs of
    // openups_usb_device_table; the scales stay 1 for any other product ID
    NutTable::Context::OpenupsScale& scale = _nut_ctx.openups;
    switch (host->getPID()) {
    case 0xD004:
        scale.vin = NutTable::kOpenupsVinScaleD004;
        scale.vout = NutTable::kOpenupsVoutScaleD004;
        scale.ccharge = NutTable::kOpenupsCchargeScaleD004;
        scale.cdischarge = NutTable::kOpenupsCdischargeScaleD004;
        break;
    case 0xD005:
        scale.vin = scale.vout = 0.1;
        scale.ccharge = scale.cdischarge = 0.1; // "unverified" upstream
        break;
    default:
        break;
    }
    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);
}

void OpenUPSDriver::filterStatus(NutTable::StatusBits& status) const {
    status.set &= ~NutTable::statusBit(NutTable::ST_NOBATTERY);
    status.known &= ~NutTable::statusBit(NutTable::ST_NOBATTERY);
}

// After the table rows: ups.load and the real power from output.voltage x output.current
// over the nominal power, taken from the model name ("Smart UPS W150") or 150 W. Written only
// when the value changes (ADR 0007).
void OpenUPSDriver::postDecode(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    GenericDriver::postDecode(host, report_id, report_type, data, length, ups_data);
    if (!ups_data.hasKey("output.voltage") || !ups_data.hasKey("output.current")) return;

    double power = ups_data.getFloat("output.voltage") * ups_data.getFloat("output.current");
    int nominalPower = 0;
    if (ups_data.hasKey("ups.model")) {
        String product = ups_data.get("ups.model");
        int wIndex = product.indexOf('W');
        if (wIndex >= 0) nominalPower = product.substring(wIndex + 1).toInt();
    }
    if (nominalPower <= 0) nominalPower = 150;

    double loadPct = (power / nominalPower) * 100.0;
    if (loadPct > 100.0) loadPct = 100.0;
    if (loadPct < 0.0) loadPct = 0.0;

    String nominal(nominalPower);
    String load((int)round(loadPct));
    bool changed = false;
    if (!ups_data.valueEquals("ups.realpower.nominal", nominal)) {
        ups_data.set("ups.realpower.nominal", nominal);
        changed = true;
    }
    if (!ups_data.valueEquals("ups.load", load)) {
        ups_data.set("ups.load", load);
        changed = true;
    }
    if (changed) ups_data.updateRealPower();
}

// openups_format_mfr() returns hd->Vendor, or "openUPS" without one
void OpenUPSDriver::formatDeviceStrings(UPSData& data) {
    if (data.get("ups.mfr").length() == 0) data.set("ups.mfr", "openUPS");
}
