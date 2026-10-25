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
#include "HIDParser.h"
#include "HIDUsages.h"
#include <string.h>

/**
 * @brief APC Driver Implementation
 * 
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for APC HID devices.
 * - Reference: nut_repo/drivers/apc-hid.c
 * - Date parsing (apc_date_conversion): Maps APCBattReplaceDate using APC proprietary bit-shifts.
 */

APCDriver::APCDriver() : _back_ups_bx(false) {}

void APCDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
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

void APCDriver::decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    // Delegate standard mappings to the base GenericDriver
    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    // Apply APC specific mappings overrides
    typedef UsageMapIndex<APCDriver>::Mapping Mapping;
    static const Mapping mappings[] = {
        { "UPS.PowerSummary.APCBattReplaceDate", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) {
            if (v <= 0) return;
            long date = (long)v;
            long year = (date & 0x0F) + ((date >> 4) & 0x0F) * 10;
            year = (year >= 70) ? (1900 + year) : (2000 + year);
            char buf[20];
            snprintf(buf, sizeof(buf), "%04ld/%02ld/%02ld",
                year,
                ((date >> 16) & 0x0F) + ((date >> 20) & 0x0F) * 10,
                ((date >> 8) & 0x0F) + ((date >> 12) & 0x0F) * 10);
            d.set("battery.date", String(buf));
        }},
        { "UPS.Battery.APCBattReplaceDate", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) {
            if (v <= 0) return;
            long date = (long)v;
            long year = (date & 0x0F) + ((date >> 4) & 0x0F) * 10;
            year = (year >= 70) ? (1900 + year) : (2000 + year);
            char buf[20];
            snprintf(buf, sizeof(buf), "%04ld/%02ld/%02ld",
                year,
                ((date >> 16) & 0x0F) + ((date >> 20) & 0x0F) * 10,
                ((date >> 8) & 0x0F) + ((date >> 12) & 0x0F) * 10);
            d.set("battery.date", String(buf));
        }},
        { "UPS.APCGeneralCollection.APCDelayBeforeStartup", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.start", String((int)v)); } },
        { "UPS.APCGeneralCollection.APCDelayBeforeShutdown", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.shutdown", String((int)v)); } },
        { "UPS.APCGeneralCollection.APCDelayBeforeReboot", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.reboot", String((int)v)); } },
        { "UPS.APCEnvironment.APCProbe1.Temperature", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ambient.temperature", String((v > 200.0) ? (v - 273.15) : v, 1)); } },
        { "UPS.APCEnvironment.APCProbe1.Humidity", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ambient.humidity", String(v, 1)); } },
        // apc-hid.c: battery.voltage.nominal con "%.1f", non intero come in GenericDriver (issue 67)
        { "UPS.Battery.ConfigVoltage", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage.nominal", String(v, 1)); } },
        { "UPS.PowerSummary.ConfigVoltage", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.voltage.nominal", String(v, 1)); } },
        // apc-hid.c: Back-UPS CS espone il carico solo qui (issue 48, perso nella 1.5.0)
        { "UPS.PowerConverter.PercentLoad", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("ups.load", String((int)v));
            d.updateRealPower();
        }}
        // input.sensitivity comes from the NUT table (apc_sensitivity_info, US-066)
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}
