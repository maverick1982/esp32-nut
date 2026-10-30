#ifndef OPENUPS_DRIVER_H
#define OPENUPS_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class OpenUPSDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "OpenUPSDriver"; }
public:
    OpenUPSDriver() = default;
    virtual ~OpenUPSDriver() = default;

    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void postDecode(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;

    // Rows of openups_hid2nut (openups-hid.c), NutTable.h (US-078)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::openupsHid2Nut(n); }
    // Standard mappings whose keys come from openupsHid2Nut() (US-078)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of openups_hid2nut: none (US-078)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::openupsHid2Nut(n); }
    // openups_format_mfr: "openUPS" when the device has no manufacturer string (US-078)
    void formatDeviceStrings(UPSData& data) override;

    // openups-hid.c has no load.* or shutdown.* commands
    bool shutdownCommandsSupported() const override { return false; }

protected:
    // PresentStatus.BatteryPresent is unreliable on these units (issue #26, US-055): its
    // nobattery bit never reaches ups.status (US-078)
    void filterStatus(NutTable::StatusBits& status) const override;
};

#endif // OPENUPS_DRIVER_H
