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
#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "HIDUsages.h"
#include "Quirks.h"

/**
 * @brief CyberPower Driver Implementation
 * 
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for CyberPower HID devices.
 * - Reference: nut_repo/drivers/cps-hid.c
 * - ConfigVoltage: UPS.PowerSummary.ConfigVoltage -> battery.voltage.nominal, as in cps-hid (GenericDriver mapping).
 * - String Inversion: cps-hid handles UTF-16 inversion (handled generically via QUIRK_INVERT_STRINGS).
 */

CyberPowerDriver::CyberPowerDriver() {}

void CyberPowerDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
}

// Traffic on EP0 is what makes these firmwares stall: status values come from the
// INPUT reports the UPS sends every few seconds, the rest is read every 30 s.
bool CyberPowerDriver::acceptPollReport(uint8_t report_type, uint8_t report_id) const {
    // Vendor-defined reports (>= 130) and reports 4 and 6 hang some firmwares
    return !(report_id >= 130 || report_id == 4 || report_id == 6);
}

void CyberPowerDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    typedef UsageMapIndex<CyberPowerDriver>::Mapping Mapping;
    static const Mapping mappings[] = {
        { "UPS.Output.Boost", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.boost", v != 0 ? "1" : "0"); } },
        { "UPS.Output.Overload", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.overload", v != 0 ? "1" : "0"); } }
        // input.sensitivity comes from the NUT table (cps_sensitivity_info, US-066)
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}
