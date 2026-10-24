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
#include "IUSBHostUPS.h"
#include "HIDParser.h"

/**
 * ADR 0003 COMPLIANCE:
 * This sub-driver mirrors the official NUT behavior for Phoenixtec USB HID devices.
 * - Reference: nut/drivers/liebert-hid.c (subdriver liebert_subdriver)
 * - Vendor ID 0x06DA, product 0xFFFF, as liebert_usb_device_table; DriverRegistry
 *   leaves the AEG (mge-hid.c) and GoldenMate (idowell-hid.c) devices to the other drivers
 *
 * Standard USB HID PDC fields are decoded by GenericDriver (ADR 0004); this sub-driver
 * only adds the rows of liebert_hid2nut that GenericDriver lacks or names otherwise.
 * Deviations from liebert-hid.c (US-097, decided on 2026-10-24):
 * 1. beeper.toggle keeps the behavior of the project (reads ups.beeper.status and writes
 *    the opposite): liebert-hid.c always writes 1, "disabled" in the standard HID encoding.
 * 2. The variables GenericDriver decodes beyond liebert_hid2nut stay, as in the other
 *    drivers, except input.voltage.nominal from UPS.Flow.[4].ConfigVoltage and
 *    input.transfer.* from UPS.PowerConverter.Output.*VoltageTransfer, which upstream
 *    publishes as output.voltage.nominal and output.transfer.*.
 * 3. PresentStatus.Good, InternalFailure and Input.[1].PresentStatus.Buck/Boost are kept as
 *    internal ups.status.* keys: no OFF, TRIM, BOOST token nor ups.alarm, since
 *    computeUPSStatusString() produces none of them for any driver yet.
 * 4. shutdown.default is offered, as for every driver of the project (US-058).
 * 5. GoldenMate devices (idowell-hid.c) and the other Phoenixtec products stay with
 *    GenericDriver (DriverRegistry).
 */

static const char* const kLiebertCommands[] = {
    // liebert_hid2nut instant commands
    "test.battery.start", "load.off.delay", "load.on.delay", "shutdown.stop", "beeper.toggle",
    // derived by usbhid-ups from load.off.delay / load.on.delay (instcmd() fallbacks)
    "load.off", "load.on", "shutdown.return", "shutdown.stayoff",
    // US-058, deviation 4
    "shutdown.default",
    nullptr
};

LiebertDriver::LiebertDriver() : _chemStrIdx(0), _vendorTransfer(false) {}

const char* const* LiebertDriver::commandAllowList() const {
    return kLiebertCommands;
}

void LiebertDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
    _chemStrIdx = 0;
    _vendorTransfer = false;
}

void LiebertDriver::onLoop(IUSBHostUPS* host, UPSData& data) {
    GenericDriver::onLoop(host, data);
    // liebert_format_mfr(): hd->Vendor, or "Liebert" when the device has no Vendor string
    if (host && host->_iManufacturer == 0 && !data.hasKey("ups.mfr")) {
        data.set("ups.mfr", "Liebert");
    }
}

void LiebertDriver::collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const {
    GenericDriver::collectStringRequests(host, data, out);
    if (_chemStrIdx > 0 && !data.hasKey("battery.type")) out.push_back(_chemStrIdx);
}

void LiebertDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
    // A Vendor string that decodes to empty counts as a missing one for liebert_format_mfr()
    if (host && host->_iManufacturer > 0 && index == host->_iManufacturer &&
        ups_data.get("ups.mfr").length() == 0) {
        ups_data.set("ups.mfr", "Liebert");
    }
    if (length < 2 || data[1] != 0x03) return;
    // liebert-hid.c: { "battery.type", ..., "UPS.PowerSummary.iDeviceChemistry", ..., stringid_conversion }
    if (_chemStrIdx > 0 && index == _chemStrIdx) ups_data.set("battery.type", decodeStringDescriptor(host, data, length));
}

static void setFlag(UPSData& d, const char* key, double v) {
    d.set(key, v != 0 ? "1" : "0");
}

void LiebertDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    typedef UsageMapIndex<LiebertDriver>::Mapping Mapping;
    // Rows of liebert_hid2nut, applied after GenericDriver. The paths carry no collection
    // index ("UPS.Flow.[4]" is UPS.Flow here), as in the GenericDriver mappings.
    static const Mapping mappings[] = {
        { "UPS.PowerSummary.FullChargeCapacity", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("experimental.battery.capacity", String(lrint(v)));
        } },
        { "UPS.PowerSummary.DesignCapacity", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("experimental.battery.capacity.nominal", String(lrint(v)));
        } },
        // String index (stringid_conversion), read by collectStringRequests()
        { "UPS.PowerSummary.iDeviceChemistry", [](LiebertDriver* drv, UPSData&, double v, const HIDUsageDef*) {
            if (v > 0) drv->_chemStrIdx = (uint8_t)v;
        } },
        // UPS.Flow.[4].ConfigVoltage is the output nominal voltage upstream (deviation 2)
        { "UPS.Flow.ConfigVoltage", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("output.voltage.nominal", String(lrint(v)));
            d.remove("input.voltage.nominal");
        } },
        { "UPS.Flow.ConfigFrequency", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("output.frequency.nominal", String(lrint(v)));
        } },
        { "UPS.PowerConverter.Output.Frequency", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("output.frequency", String(v, 2));
        } },
        { "UPS.PowerConverter.Input.Frequency", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("input.frequency", String(v, 2));
        } },
        // Output.*VoltageTransfer is output.transfer.* upstream; input.transfer.* comes from
        // the vendor usages below (deviation 2)
        { "UPS.PowerConverter.Output.HighVoltageTransfer", [](LiebertDriver* drv, UPSData& d, double v, const HIDUsageDef*) {
            d.set("output.transfer.high", String(v, 1));
            if (!drv->_vendorTransfer) d.remove("input.transfer.high");
        } },
        { "UPS.PowerConverter.Output.LowVoltageTransfer", [](LiebertDriver* drv, UPSData& d, double v, const HIDUsageDef*) {
            d.set("output.transfer.low", String(v, 1));
            if (!drv->_vendorTransfer) d.remove("input.transfer.low");
        } },
        { "UPS.PowerConverter.Output.0xFFFF0057", [](LiebertDriver* drv, UPSData& d, double v, const HIDUsageDef*) {
            drv->_vendorTransfer = true;
            d.set("input.transfer.low", String(lrint(v)));
        } },
        { "UPS.PowerConverter.Output.0xFFFF0058", [](LiebertDriver* drv, UPSData& d, double v, const HIDUsageDef*) {
            drv->_vendorTransfer = true;
            d.set("input.transfer.high", String(lrint(v)));
        } },
        { "UPS.PowerConverter.Output.0xFFFF00F9", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("input.frequency.transfer.low", String(lrint(v)));
        } },
        { "UPS.PowerConverter.Output.0xFFFF00F8", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("input.frequency.transfer.high", String(lrint(v)));
        } },
        // Status flags kept as internal keys (deviation 3): off_info, commfault_info,
        // trim_info and boost_info upstream
        { "UPS.PowerSummary.PresentStatus.Good", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            setFlag(d, "ups.status.good", v);
        } },
        { "UPS.PowerSummary.PresentStatus.InternalFailure", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            setFlag(d, "ups.status.internal_failure", v);
        } },
        { "UPS.PowerConverter.Input.PresentStatus.Buck", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            setFlag(d, "ups.status.trim", v);
        } },
        { "UPS.PowerConverter.Input.PresentStatus.Boost", [](LiebertDriver*, UPSData& d, double v, const HIDUsageDef*) {
            setFlag(d, "ups.status.boost", v);
        } },
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}
