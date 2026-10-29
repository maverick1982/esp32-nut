#ifndef CYBERPOWER_DRIVER_H
#define CYBERPOWER_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class CyberPowerDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "CyberPowerDriver"; }
public:
    CyberPowerDriver() = default;
    virtual ~CyberPowerDriver() = default;

    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;

    // DEFAULT_OFFDELAY_CPS / DEFAULT_ONDELAY_CPS of usbhid-ups.h: the CPS firmware rounds
    // the delays down to multiples of 60 s, so 20 s would shut down at once
    int32_t defaultOffDelay() const override { return 60; }
    int32_t defaultOnDelay() const override { return 120; }

    // Rows of cps_hid2nut (cps-hid.c), NutTable.h (US-066, US-075)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::cpsHid2Nut(n); }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::cpsDelayPaths(); }
    // Standard mappings whose keys come from cpsHid2Nut() (US-075)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of cps_hid2nut (US-075)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::cpsHid2Nut(n); }

protected:
    uint32_t quickPollMs() const override { return 0; }
    uint32_t fullPollMs() const override { return 30000; }
    bool acceptPollReport(uint8_t report_type, uint8_t report_id) const override;
    // INPUT reports arrive on the interrupt endpoint: never request them on EP0
    bool pollInputReports() const override { return false; }
    // The BR700ELCD layout (issue #40) has vendor reports: no control request beyond the
    // ones of the device strings is added for it
    bool requestTableStrings(IHidTransport* host) const override { return !hasVendorReports(host); }

private:
    // Vendor-defined reports (>= 130) and reports 4 and 6 hang some firmwares (issue #40)
    static const uint8_t FIRST_VENDOR_REPORT = 130;
    static bool hasVendorReports(IHidTransport* host);
};

#endif // CYBERPOWER_DRIVER_H
