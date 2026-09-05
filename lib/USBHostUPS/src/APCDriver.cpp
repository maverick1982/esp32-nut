#include "APCDriver.h"
#include "IUSBHostUPS.h"
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

APCDriver::APCDriver() {}

void APCDriver::decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    // Delegate standard mappings to the base GenericDriver
    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    // Apply APC specific mappings overrides
    struct Mapping {
        String path;
        void (*apply)(APCDriver*, UPSData&, double, const HIDUsageDef*);
    };

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
        { "UPS.APCGeneralCollection.APCDelayBeforeStartup", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.delay.start", String((int)v)); d.set("ups.timer.start", String((int)v)); } },
        { "UPS.APCGeneralCollection.APCDelayBeforeShutdown", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.delay.shutdown", String((int)v)); d.set("ups.timer.shutdown", String((int)v)); } },
        { "UPS.APCGeneralCollection.APCDelayBeforeReboot", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ups.timer.reboot", String((int)v)); } },
        { "UPS.APCEnvironment.APCProbe1.Temperature", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ambient.temperature", String((v > 200.0) ? (v - 273.15) : v, 1)); } },
        { "UPS.APCEnvironment.APCProbe1.Humidity", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { d.set("ambient.humidity", String(v, 1)); } },
        { "UPS.Input.APCSensitivity", [](APCDriver*, UPSData& d, double v, const HIDUsageDef*) { 
            int val = (int)v;
            if (val == 1) d.set("input.sensitivity", "low");
            else if (val == 2) d.set("input.sensitivity", "medium");
            else if (val == 3) d.set("input.sensitivity", "high");
        }}
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
