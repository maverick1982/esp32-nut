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

EatonDriver::EatonDriver() : _chemStrIdx(0), _iModelIdx(0), _iModelIdxSeen(false), _iModelSeen(false),
    _apparentPowerSeen(false), _apparentPower(0), _countrySeen(false) {}

void EatonDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
    _chemStrIdx = 0;
    _iModelIdx = 0;
    _iModelIdxSeen = false;
    _iModelSeen = false;
    _iModel = "";
    _apparentPowerSeen = false;
    _apparentPower = 0;
    _countrySeen = false;
    // mge_type and country_code are known before the first walk upstream: the
    // model-dependent rows wait for them (NutTable::DecodeResult::DEFER)
    _nut_ctx.ready = false;
}

static bool hasUsagePath(const std::vector<HIDUsageDef>& usages, const char* path) {
    for (const auto& u : usages) {
        if (NutTable::pathMatches(u, path)) return true;
    }
    return false;
}

// mge-hid.c usage codes, unnamed in NUTUsages.h
static const char* const kIModelPath = "UPS.PowerSummary.0xFFFF00F0";        // iModel
static const char* const kCountryPath = "UPS.PowerSummary.0xFFFF0095";       // Country
static const char* const kApparentPowerPath = "UPS.Flow.[4].ConfigApparentPower";

void EatonDriver::updateNutContext(IUSBHostUPS* host, const UPSData& data) {
    if (_nut_ctx.ready) return;
    const auto& usages = host->getUsages();

    // mge_format_model(): iModel, else the Flow.[4] apparent power, else no model. An
    // iModel string that never arrives counts as empty, as a failed HIDGetItemString().
    String model = _iModel;
    if (model.length() == 0 && _apparentPowerSeen) model = String(_apparentPower);
    bool model_known;
    if (hasUsagePath(usages, kIModelPath) && (!_iModelIdxSeen || (_iModelIdx > 0 && !_iModelSeen))) {
        model_known = false;
    } else if (_iModel.length() > 0) {
        model_known = true;
    } else if (hasUsagePath(usages, kApparentPowerPath)) {
        model_known = _apparentPowerSeen;
    } else {
        model_known = true;
    }
    bool country_known = !hasUsagePath(usages, kCountryPath) || _countrySeen;
    bool product_known = data.hasKey("ups.model") || host->_iProduct == 0;

    // The first full poll is where upstream's initinfo would be done: decide with what is known
    if ((model_known && country_known && product_known) || initialPollDone()) {
        String product = data.get("ups.model", "unknown");
        _nut_ctx.mge_type = NutTable::mgeModelType(product.c_str(), model.c_str());
        _nut_ctx.ready = true;
    }
}

void EatonDriver::onLoop(IUSBHostUPS* host, UPSData& data) {
    updateNutContext(host, data);
}

bool EatonDriver::acceptPollReport(uint8_t report_type, uint8_t report_id) const {
    // CRITICAL QUIRK (NUT libhid.c): reports 254/255 freeze or stall Eaton devices
    return report_id != 254 && report_id != 255;
}

void EatonDriver::collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const {
    GenericDriver::collectStringRequests(host, data, out);
    if (_chemStrIdx > 0 && !data.hasKey("battery.type")) out.push_back(_chemStrIdx);
    if (_iModelIdx > 0 && !_iModelSeen) out.push_back(_iModelIdx);
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
        { "UPS.PowerSummary.iDeviceChemistry", [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) { drv->_chemStrIdx = (uint8_t)v; } },
        // Inputs of mge_type and country_code (NutTable::Context, US-066)
        { kIModelPath, [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) { drv->_iModelIdx = (uint8_t)v; drv->_iModelIdxSeen = true; } },
        { kCountryPath, [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) {
            // eaton_check_country_fun: the value is the country code, never published
            drv->_nut_ctx.country = (int)v;
            drv->_countrySeen = true;
        } },
        { "UPS.Flow.ConfigApparentPower", [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef* def) {
            if (def && NutTable::pathMatches(*def, kApparentPowerPath)) {
                drv->_apparentPower = (long)v;
                drv->_apparentPowerSeen = true;
            }
        } }
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
    if (_iModelIdx > 0 && index == _iModelIdx) {
        _iModel = str;
        _iModelSeen = true;
    }
}
