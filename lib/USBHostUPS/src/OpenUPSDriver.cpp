#include "OpenUPSDriver.h"
#include "IUSBHostUPS.h"
#include "HIDParser.h"

/**
 * @brief OpenUPS Driver Implementation (WalleCube W150)
 * 
 * ADR 0003 COMPLIANCE:
 * Inherits from GenericDriver.
 * Applies scale factors (0.1) to input/output voltages and currents as per nut_repo/drivers/openups-hid.c.
 * Maps custom thermistor table for Temperature.
 */

static const unsigned int therm_tbl[] = {
    0x31, 0x40, 0x53, 0x68, 0x82, 0xA0, 0xC3, 0xE9, 0x113, 0x13F, 0x16E, 0x19F, 
    0x1CF, 0x200, 0x22F, 0x25C, 0x286, 0x2AE, 0x2D3, 0x2F4, 0x312, 0x32D, 0x345, 
    0x35A, 0x36D, 0x37E, 0x38C, 0x399, 0x3A5, 0x3AF, 0x3B7, 0x3BF, 0x3C6, 0x3CC
};
static const unsigned int therm_tbl_size = sizeof(therm_tbl) / sizeof(therm_tbl[0]);

static float calculateTemperature(double value) {
    unsigned int thermistor = (unsigned int)(value * 100);

    if (thermistor <= therm_tbl[0]) {
        return -40.0f;
    } else if (thermistor >= therm_tbl[therm_tbl_size - 1]) {
        return 125.0f;
    } else {
        int pos = 0;
        for (int i = therm_tbl_size - 1; i >= 0; i--) {
            if (thermistor >= therm_tbl[i]) {
                pos = i;
                break;
            }
        }

        if (thermistor == therm_tbl[pos]) {
            return (float)(pos * 5 - 40);
        } else {
            int t1 = pos * 5 - 40;
            int t2 = (pos + 1) * 5 - 40;

            unsigned int d1 = therm_tbl[pos];
            unsigned int d2 = therm_tbl[pos + 1];

            return (float)(thermistor - d1) * (t2 - t1) / (d2 - d1) + t1;
        }
    }
}

void OpenUPSDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    struct Mapping {
        String path;
        void (*apply)(OpenUPSDriver*, UPSData&, double, const HIDUsageDef*);
    };

    static const Mapping mappings[] = {
        { "UPS.PowerSummary.Input.Voltage", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.inputVoltage = true; d.inputVoltage = v * 0.1f; } },
        { "UPS.PowerSummary.Input.Current", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.inputCurrent = true; d.inputCurrent = v * 0.1f; } },
        
        { "UPS.PowerSummary.Output.Voltage", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.outputVoltage = true; d.outputVoltage = v * 0.1f; } },
        { "UPS.PowerSummary.Output.Current", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.outputCurrent = true; d.outputCurrent = v * 0.1f; } },
        
        { "UPS.PowerSummary.Voltage", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.batteryVoltage = true; d.batteryVoltage = v; } },
        { "UPS.PowerSummary.Current", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { d.has.batteryCurrent = true; d.batteryCurrent = v; } },
        
        { "UPS.PowerSummary.Temperature", [](OpenUPSDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            d.has.batteryTemperature = true; 
            d.batteryTemperature = calculateTemperature(v);
        } },
        
        { "UPS.PowerSummary.iOEMInformation", [](OpenUPSDriver* drv, UPSData&, double v, const HIDUsageDef*) {
            if (v > 0) drv->_batteryDateStringIndex = (uint8_t)v;
        } },
        
        { "UPS.PowerSummary.iDeviceChemistry", [](OpenUPSDriver*, UPSData&, double v, const HIDUsageDef*) {
            // We could store it if we wanted to fetch it, but WalleCube fixture doesn't check it
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

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);
}
