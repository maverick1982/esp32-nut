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
    _chemStrIdx = 0;
}

void EatonDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (data.upsType != getDriverName()) {
        data.has.upsType = true;
        data.upsType = getDriverName();
    }

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;

            _slow_poll_counter++;
            if (_slow_poll_counter >= 15) {
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (host->isControlPending()) return;

        if (now - _last_step_time >= 50 || _poll_step == 1) { // Execute first step immediately
            _last_step_time = now;
            
            if (_poll_step == 1) {
                if (_slow_poll_counter == 0 && data.manufacturer == "") if (host->_iManufacturer > 0) host->requestStringDescriptor(host->_iManufacturer);
            } else if (_poll_step == 2) {
                if (_slow_poll_counter == 0 && data.product == "") if (host->_iProduct > 0) host->requestStringDescriptor(host->_iProduct);
            } else if (_poll_step == 3) {
                if (_slow_poll_counter == 0 && data.serialNumber == "") if (host->_iSerialNumber > 0) host->requestStringDescriptor(host->_iSerialNumber);
            } else if (_poll_step == 4) {
                if (_slow_poll_counter == 0 && _chemStrIdx > 0 && data.batteryType == "") host->requestStringDescriptor(_chemStrIdx);
            } else {
                const auto& usages = host->getUsages();
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    if (u.report_type == 2) continue; // Skip OUTPUT reports
                    if (u.report_id == 254 || u.report_id == 255) continue; // CRITICAL QUIRK (NUT): Skip reports 254/255 for Eaton devices to prevent USB freeze/stall
                    uint16_t pair = (u.report_type << 8) | u.report_id;
                    bool found = false;
                    for (uint16_t id : rids) {
                        if (id == pair) { found = true; break; }
                    }
                    if (!found) rids.push_back(pair);
                }
                for (auto it = rids.begin(); it != rids.end(); ) {
                    if ((*it >> 8) == 1) { // If Input report
                        uint8_t id = *it & 0xFF;
                        bool has_feature = false;
                        for (uint16_t pair : rids) {
                            if ((pair >> 8) == 3 && (pair & 0xFF) == id) { has_feature = true; break; }
                        }
                        if (has_feature) {
                            it = rids.erase(it);
                            continue;
                        }
                    }
                    ++it;
                }
                
                int index = _poll_step - 5;
                if (index >= 0 && index < rids.size()) {
                    uint8_t r_type = rids[index] >> 8;
                    uint8_t r_id = rids[index] & 0xFF;
                    host->requestReport(r_id, r_type, 64);
                } else {
                    _poll_step = 0;
                    return;
                }
            }
            _poll_step++;
        }
    }
}

void EatonDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    struct Mapping {
        const char* path;
        void (*apply)(EatonDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.PresentStatus.Good", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.good = true; d.good = v != 0; } } },
        { "UPS.PowerSummary.PresentStatus.InternalFailure", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.internalFailure = true; d.internalFailure = v != 0; } } },
        { "UPS.OutletSystem.Outlet.PresentStatus.SwitchOn/Off", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.outlet1Switch = true; d.outlet1Switch = v != 0; } { d.has.outlet2Switch = true; d.outlet2Switch = v != 0; } } },
        { "UPS.PowerSummary.Voltage", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { if (v > 0) { d.has.batteryVoltage = true; d.batteryVoltage = v; } } },
        { "UPS.Flow.ConfigFrequency", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { { d.has.configFrequency = true; d.configFrequency = (uint8_t)v; } { d.has.outputFrequencyNominal = true; d.outputFrequencyNominal = (uint16_t)v; } } },
        { "UPS.PowerConverter.ConverterType", [](EatonDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            int type = (int)v;
            if (type == 1) { d.has.upsType = true; d.upsType = "offline / line interactive"; }
            else if (type == 2) { d.has.upsType = true; d.upsType = "online"; }
            else if (type == 3) { d.has.upsType = true; d.upsType = "online - unitary/parallel"; }
            else if (type == 4) { d.has.upsType = true; d.upsType = "online - parallel with hot standy"; }
            else if (type == 5) { d.has.upsType = true; d.upsType = "online - hot standby redundancy"; }
        } },
        { "UPS.PowerSummary.iDeviceChemistry", [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) { drv->_chemStrIdx = (uint8_t)v; } }
    };

    for (const auto& u : host->getUsages()) {
        if (u.report_id != report_id || u.report_type != report_type) continue;
        for (const auto& m : mappings) {
            if (u.path == String(m.path)) {
                double val = HIDParser::extractUsage(&u, report_id, data, length);
                m.apply(this, ups_data, val, &u);
                break;
            }
        }
    }
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
    
    if (_chemStrIdx > 0 && index == _chemStrIdx) { ups_data.has.batteryType = true; ups_data.batteryType = str; }
}
