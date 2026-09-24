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
        { "UPS.PowerSummary.ConfigVoltage", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            // In cps-hid, this is battery.voltage.nominal. We do NOT want to map it 
            // to input.voltage.nominal like GenericDriver does.
            // Override the generic mapping by clearing the input one and setting battery.
            d.remove("input.voltage.nominal");
            d.set("battery.voltage.nominal", String((int)v));
        } },
        { "UPS.Output.Boost", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.boost", v != 0 ? "1" : "0"); } },
        { "UPS.Output.Overload", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.status.overload", v != 0 ? "1" : "0"); } },
        { "UPS.Output.CPSInputSensitivity", [](CyberPowerDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            int val = (int)v;
            if (val == 1) d.set("input.sensitivity", "low");
            else if (val == 2) d.set("input.sensitivity", "medium");
            else if (val == 3) d.set("input.sensitivity", "high");
        } }
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}
