#include "EatonDriver.h"
#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "HIDUsages.h"

/**
 * @brief Eaton (MGE) Driver Implementation
 * 
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for Eaton/MGE HID devices.
 * - Reference: nut_repo/drivers/mge-hid.c
 * - Quirk 254/255: Mirrored from nut_repo/drivers/libhid.c (skip reports 254/255 to prevent freeze).
 * - iDeviceChemistry: Specific mapping for batteryType.
 */

EatonDriver::EatonDriver() : _chemStrIdx(0) {}

void EatonDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
    _chemStrIdx = 0;
}

bool EatonDriver::acceptPollReport(uint8_t report_type, uint8_t report_id) const {
    // CRITICAL QUIRK (NUT libhid.c): reports 254/255 freeze or stall Eaton devices
    return report_id != 254 && report_id != 255;
}

void EatonDriver::collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const {
    GenericDriver::collectStringRequests(host, data, out);
    if (_chemStrIdx > 0 && !data.hasKey("battery.type")) out.push_back(_chemStrIdx);
}

void EatonDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    typedef UsageMapIndex<EatonDriver>::Mapping Mapping;
    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.Good", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.good", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.PresentStatus.InternalFailure", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.internal_failure", v != 0 ? "1" : "0"); } },
        { "UPS.OutletSystem.Outlet.PresentStatus.SwitchOn/Off", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("outlet.1.switch", v != 0 ? "1" : "0"); d.set("outlet.2.switch", v != 0 ? "1" : "0"); } },
        { "UPS.PowerSummary.Voltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) { d.set("battery.voltage", String(v, 2)); } } },
        { "UPS.Flow.ConfigFrequency", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("input.frequency.nominal", String((int)v)); d.set("output.frequency.nominal", String((int)v)); } },
        { "UPS.PowerConverter.ConverterType", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            int type = (int)v;
            if (type == 1) { d.set("ups.type", "offline / line interactive"); }
            else if (type == 2) { d.set("ups.type", "online"); }
            else if (type == 3) { d.set("ups.type", "online - unitary/parallel"); }
            else if (type == 4) { d.set("ups.type", "online - parallel with hot standy"); }
            else if (type == 5) { d.set("ups.type", "online - hot standby redundancy"); }
        } },
        { "UPS.PowerSummary.iDeviceChemistry", [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) { drv->_chemStrIdx = (uint8_t)v; } }
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}

void EatonDriver::parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
    
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) {
            str += (char)data[i];
        }
    }
    
    if (_chemStrIdx > 0 && index == _chemStrIdx) { ups_data.set("battery.type", str); }
}
