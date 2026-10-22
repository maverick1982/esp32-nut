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
#include <cmath>

/**
 * @brief CyberPower Driver Implementation
 *
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for CyberPower HID devices.
 * - Reference: nut_repo/drivers/cps-hid.c
 * - ConfigVoltage: UPS.PowerSummary.ConfigVoltage -> battery.voltage.nominal, as in cps-hid (GenericDriver mapping).
 * - String Inversion: cps-hid handles UTF-16 inversion (handled generically via QUIRK_INVERT_STRINGS).
 * - cps_battvolt / cps_input_freq / cps_output_freq: the 2/3 and 0.1 scales of PID 0x0501.
 * - cps_fix_report_desc is not needed: extractUsage() does not clip values to the
 *   Logical Maximum, so the wrong LogMax of the voltages and ConfigActivePower is harmless.
 */

// cps-hid.c: battery_voltage_sanity_check
static const double kBatteryVoltageSanityCheck = 1.4;

CyberPowerDriver::CyberPowerDriver() :
    _chemStrIdx(0),
    _oemStrIdx(0),
    _firmwareStrIdx(0),
    _mightNeedScale(false),
    _batteryScale(1.0),
    _batteryScaleChecked(false),
    _inputFreqScale(1.0),
    _outputFreqScale(1.0),
    _inputFreqScaleChecked(false),
    _outputFreqScaleChecked(false) {}

void CyberPowerDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
    _chemStrIdx = 0;
    _oemStrIdx = 0;
    _firmwareStrIdx = 0;
    _mightNeedScale = false;
    _batteryScale = 1.0;
    _batteryScaleChecked = false;
    _inputFreqScale = 1.0;
    _outputFreqScale = 1.0;
    _inputFreqScaleChecked = false;
    _outputFreqScaleChecked = false;
}

// Traffic on EP0 is what makes these firmwares stall: status values come from the
// INPUT reports the UPS sends every few seconds, the rest is read every 30 s.
bool CyberPowerDriver::acceptPollReport(uint8_t report_type, uint8_t report_id) const {
    // Vendor-defined reports (>= 130) and reports 4 and 6 hang some firmwares
    return !(report_id >= FIRST_VENDOR_REPORT || report_id == 4 || report_id == 6);
}

bool CyberPowerDriver::hasVendorReports(IUSBHostUPS* host) {
    for (const auto& u : host->getUsages()) {
        if (u.report_id >= FIRST_VENDOR_REPORT) return true;
    }
    return false;
}

void CyberPowerDriver::collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const {
    GenericDriver::collectStringRequests(host, data, out);
    if (hasVendorReports(host)) return;
    if (_chemStrIdx > 0 && !data.hasKey("battery.type")) out.push_back(_chemStrIdx);
    if (_firmwareStrIdx > 0 && !data.hasKey("ups.firmware")) out.push_back(_firmwareStrIdx);
    if (_oemStrIdx > 0 && !data.hasKey("battery.mfr.date")) out.push_back(_oemStrIdx);
}

bool CyberPowerDriver::frequencyScale(double report, double nominal, double low, double high, double& scale) {
    if (nominal == 0) {
        if (45 < low && low <= 50) nominal = 50;
        else if (50 <= high && high <= 55) nominal = 50;
        else if (45 < report && report <= 55) nominal = 50;
        else if (450 < report && report <= 550) nominal = 50;
        else if (55 < low && low <= 60) nominal = 60;
        else if (60 <= high && high <= 65) nominal = 60;
        else if (55 < report && report <= 65) nominal = 60;
        else if (550 < report && report <= 650) nominal = 60;
    }
    if (low == 0) low = (nominal == 0) ? 45.0 : nominal * 0.95;
    if (high == 0) high = (nominal == 0) ? 65.0 : nominal * 1.05;

    if (low <= report && report <= high) {
        scale = 1.0;
        return true;
    }
    if (low <= report / 10.0 && report / 10.0 <= high) {
        scale = 0.1;
        return true;
    }
    return false;
}

// cps_input_freq_fun / cps_output_freq_fun
void CyberPowerDriver::scaleFrequency(UPSData& d, double v, bool input) {
    double& scale = input ? _inputFreqScale : _outputFreqScale;
    bool& checked = input ? _inputFreqScaleChecked : _outputFreqScaleChecked;
    const char* prefix = input ? "input" : "output";
    if (_mightNeedScale && !checked) {
        double s;
        if (frequencyScale(v, d.getFloat(String(prefix) + ".frequency.nominal"),
                           d.getFloat(String(prefix) + ".frequency.low"),
                           d.getFloat(String(prefix) + ".frequency.high"), s)) {
            scale = s;
            checked = true;
        }
    }
    d.set(String(prefix) + ".frequency", String(scale * v, 1));
}

void CyberPowerDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    // cps_usb_device_table: cps_battery_scale() is the hook of PID 0x0501 only
    _mightNeedScale = host->getPID() == 0x0501;

    typedef UsageMapIndex<CyberPowerDriver>::Mapping Mapping;
    static const Mapping mappings[] = {
        { "UPS.Output.Boost", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.boost", v != 0 ? "1" : "0"); } },
        { "UPS.Output.Overload", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.overload", v != 0 ? "1" : "0"); } },
        // input.sensitivity comes from the NUT table (cps_sensitivity_info, US-066)

        // cps_battvolt: "%.1f", scaled by 2/3 when the reading is well above nominal
        { "UPS.PowerSummary.Voltage", [](CyberPowerDriver* drv, UPSData& d, double v, const HIDUsageDef*) {
            if (drv->_mightNeedScale && !drv->_batteryScaleChecked) {
                // cps_adjust_battery_scale(): decided once the nominal voltage is known
                float nominal = d.getFloat("battery.voltage.nominal");
                if (nominal != 0) {
                    if (v / nominal > kBatteryVoltageSanityCheck) drv->_batteryScale = 2.0 / 3;
                    drv->_batteryScaleChecked = true;
                }
            }
            d.set("battery.voltage", String(drv->_batteryScale * v, 1));
        } },
        { "UPS.Input.Frequency", [](CyberPowerDriver* drv, UPSData& d, double v, const HIDUsageDef*) { drv->scaleFrequency(d, v, true); } },
        { "UPS.Output.Frequency", [](CyberPowerDriver* drv, UPSData& d, double v, const HIDUsageDef*) { drv->scaleFrequency(d, v, false); } },

        { "UPS.PowerSummary.WarningCapacityLimit", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.charge.warning", String(lrint(v))); } },
        // cps_battstatus: FullChargeCapacity in percent
        { "UPS.PowerSummary.FullChargeCapacity", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("battery.status", String(lrint(v)) + "%"); } },
        { "UPS.Output.ConfigApparentPower", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) {
            d.set("ups.power.nominal", String(lrint(v)));
            d.updateRealPower();
        } },
        // Timers read from the device; ups.delay.* are the configured delays (US-058)
        { "UPS.Output.DelayBeforeStartup", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.start", String(lrint(v))); } },
        { "UPS.Output.DelayBeforeShutdown", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.shutdown", String(lrint(v))); } },
        { "UPS.Output.DelayBeforeReboot", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.reboot", String(lrint(v))); } },

        // String indices (stringid_conversion), read by collectStringRequests()
        { "UPS.PowerSummary.iDeviceChemistry", [](CyberPowerDriver* drv, UPSData&, double v, const HIDUsageDef*) { if (v > 0) drv->_chemStrIdx = (uint8_t)v; } },
        { "UPS.PowerSummary.iOEMInformation", [](CyberPowerDriver* drv, UPSData&, double v, const HIDUsageDef*) { if (v > 0) drv->_oemStrIdx = (uint8_t)v; } },
        // CPSFirmwareVersion (0xff0100d0, cps_usage_lkp)
        { "UPS.PowerSummary.0xFF0100D0", [](CyberPowerDriver* drv, UPSData&, double v, const HIDUsageDef*) { if (v > 0) drv->_firmwareStrIdx = (uint8_t)v; } }
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}

void CyberPowerDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
    if (length < 2 || data[1] != 0x03) return;

    // The same index may name more than one value (iOEMInformation is often iManufacturer)
    String str = decodeStringDescriptor(host, data, length);
    if (_chemStrIdx > 0 && index == _chemStrIdx) ups_data.set("battery.type", str);
    if (_firmwareStrIdx > 0 && index == _firmwareStrIdx) ups_data.set("ups.firmware", str);
    // Second battery.mfr.date row of cps_hid2nut, after UPS.Battery.ManufacturerDate
    if (_oemStrIdx > 0 && index == _oemStrIdx && !ups_data.hasKey("battery.mfr.date")) ups_data.set("battery.mfr.date", str);
}
