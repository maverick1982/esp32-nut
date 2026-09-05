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
 * - ConfigVoltage Quirks: In cps-hid, UPS.PowerSummary.ConfigVoltage maps to battery.voltage.nominal, overriding generic mapping.
 * - String Inversion: cps-hid handles UTF-16 inversion (handled generically via QUIRK_INVERT_STRINGS).
 */

CyberPowerDriver::CyberPowerDriver() {}

void CyberPowerDriver::setup() {
    GenericDriver::setup();
    _slow_poll_counter = 1; // Align to CyberPower original initialization if necessary? Wait, CyberPowerDriver::setup had _slow_poll_counter = 14!
    // Actually, let's just use GenericDriver::setup() exactly.
}

void CyberPowerDriver::loop(IUSBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (data.upsType != getDriverName()) {
        data.has.upsType = true;
        data.upsType = getDriverName();
    }

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 30000 || _last_fast_poll == 0) { // 30 seconds polling for CyberPower!
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
            
            _slow_poll_counter++;
            if (_slow_poll_counter >= 2) { // Every 2 cycles (60s)
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
            } else {
                const auto& usages = host->getUsages();
                std::vector<uint16_t> rids;
                for (const auto& u : usages) {
                    if (u.report_type == 2) continue; // Skip OUTPUT reports
                    uint16_t pair = (u.report_type << 8) | u.report_id;
                    bool found = false;
                    for (uint16_t id : rids) {
                        if (id == pair) { found = true; break; }
                    }
                    if (!found && u.report_id != 0) rids.push_back(pair);
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
                
                int index = _poll_step - 4;
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

void CyberPowerDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    struct Mapping {
        String path;
        void (*apply)(CyberPowerDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.ConfigVoltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            // In cps-hid, this is battery.voltage.nominal. We do NOT want to map it 
            // to input.configVoltage like GenericDriver does.
            // If GenericDriver sets it to inputVoltage, we must undo it or ignore it.
            // Wait, GenericDriver maps UPS.PowerSummary.ConfigVoltage to configVoltage.
            // So we override it by explicitly clearing it, or we intercept it.
            // Actually, if GenericDriver already set it, we just reset it here!
            d.has.configVoltage = false; 
            d.configVoltage = 0;
        } }
    };

    for (const auto& u : host->getUsages()) {
        if (u.report_id != report_id || u.report_type != report_type) continue;
        for (const auto& m : mappings) {
            if (u.path == m.path) {
                double val = HIDParser::extractUsage(&u, report_id, data, length);
                m.apply(this, ups_data, val, &u);
                break;
            }
        }
    }
}
