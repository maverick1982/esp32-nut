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
#include "IUSBHostUPS.h"
#include "HIDParser.h"

/**
 * ADR 0003 COMPLIANCE:
 * This sub-driver mirrors the official NUT behavior for Legrand USB HID devices.
 * - Reference: nut/drivers/legrand-hid.c (subdriver legrand_subdriver)
 * - Vendor ID 0x1CB0, products 0x0038 (Keor PDU) and 0x0032 (Keor SP), as
 *   legrand_usb_device_table
 *
 * Standard USB HID PDC fields are decoded by GenericDriver (ADR 0004); this sub-driver
 * only adds what is Legrand-specific. Deviations from legrand-hid.c (US-087, decided on
 * 2026-10-18):
 * 1. No legrand_times10/100k/1M/10M nor divide_by_10_conversion: on the Keor PDU 800 they
 *    give values off by a factor of 10 or more with NUT too (DDL report: battery.voltage
 *    1.4, battery.voltage.nominal 1.2, output.voltage 6). The volts come from the HID
 *    unit exponent, as libhid.c computes them.
 * 2. ups.beeper.status is published with the Legrand meaning of the field (2 = enabled,
 *    3 = disabled), while legrand-hid.c publishes no beeper status.
 * 3. beeper.toggle is offered, as for every driver of the project.
 * 4. The variables GenericDriver decodes beyond legrand_hid2nut stay, as in the other
 *    drivers, until the EP-001 table migration.
 * 5. load.* and shutdown.* use the common catalog paths (PowerSummary, Output);
 *    UPS.OutletSystem.Outlet.DelayBefore* only counts for ups.delay.*.
 * 6. Other Legrand products stay with GenericDriver (DriverRegistry), where upstream
 *    rejects them unless the productid option is given.
 */

void LegrandDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
}

void LegrandDriver::onLoop(IUSBHostUPS* host, UPSData& data) {
    GenericDriver::onLoop(host, data);
    // legrand_format_mfr(): hd->Vendor, or "Legrand" when the device has no Vendor string
    if (host && host->_iManufacturer == 0 && !data.hasKey("ups.mfr")) {
        data.set("ups.mfr", "Legrand");
    }
}

void LegrandDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
    // A Vendor string that decodes to empty counts as a missing one for legrand_format_mfr()
    if (host && host->_iManufacturer > 0 && index == host->_iManufacturer &&
        ups_data.get("ups.mfr").length() == 0) {
        ups_data.set("ups.mfr", "Legrand");
    }
}

// Legrand meaning of AudibleAlarmControl (deviation 2): beeper.enable writes 2 and
// beeper.disable writes 3 in legrand-hid.c; 1 is the HID "disabled" as well
static void legrandBeeperStatus(UPSData& d, double v, const HIDUsageDef* def, const String& active_beeper) {
    if (def && strcmp(def->path, active_beeper.c_str()) != 0) return;
    if (def && def->bit_size == 1) { d.set("ups.beeper.status", (v != 0) ? "enabled" : "disabled"); return; }
    if (v == 2) d.set("ups.beeper.status", "enabled");
    else if (v == 1 || v == 3) d.set("ups.beeper.status", "disabled");
}

void LegrandDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    typedef UsageMapIndex<LegrandDriver>::Mapping Mapping;
    // Applied after GenericDriver: these values override its standard beeper_info decoding
    static const Mapping mappings[] = {
        { "UPS.PowerSummary.AudibleAlarmControl", [](LegrandDriver* drv, UPSData& d, double v, const HIDUsageDef* def) {
            legrandBeeperStatus(d, v, def, drv->_active_beeper);
        } },
        { "UPS.BatterySystem.Battery.AudibleAlarmControl", [](LegrandDriver* drv, UPSData& d, double v, const HIDUsageDef* def) {
            legrandBeeperStatus(d, v, def, drv->_active_beeper);
        } },
        { "UPS.AudibleAlarmControl", [](LegrandDriver* drv, UPSData& d, double v, const HIDUsageDef* def) {
            legrandBeeperStatus(d, v, def, drv->_active_beeper);
        } },
        // Rows of legrand_hid2nut that GenericDriver does not map, without the legrand_times*
        // conversions (deviation 1)
        { "UPS.PowerSummary.WarningCapacityLimit", [](LegrandDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("battery.charge.warning", String((int)v));
        } },
        { "UPS.BatterySystem.Battery.ConfigVoltage", [](LegrandDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("battery.voltage.nominal", String(lrint(v)));
        } },
        { "UPS.BatterySystem.Battery.Voltage", [](LegrandDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("battery.voltage", String(v, 2));
        } },
        { "UPS.OutletSystem.Outlet.PercentLoad", [](LegrandDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("ups.load", String((int)v));
            d.updateRealPower();
        } },
        // legrand-hid.c maps Flow.ConfigApparentPower to ups.realpower.nominal, as it is;
        // GenericDriver also keeps it as ups.power.nominal (deviation 4)
        { "UPS.Flow.ConfigApparentPower", [](LegrandDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("ups.realpower.nominal", String((int)v));
            d.updateRealPower();
        } },
        // legrand-hid.c: { "BOOL", ..., "UPS.Output.Overload", ..., overload_info }.
        // GenericDriver only knows the PowerSummary paths of Overload
        { "UPS.Output.Overload", [](LegrandDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("ups.status.overload", v != 0 ? "1" : "0");
        } },
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}
