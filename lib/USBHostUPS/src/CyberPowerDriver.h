#ifndef CYBERPOWER_DRIVER_H
#define CYBERPOWER_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class CyberPowerDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "CyberPowerDriver"; }
public:
    CyberPowerDriver();
    virtual ~CyberPowerDriver() = default;

    void setup() override;
    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

    // DEFAULT_OFFDELAY_CPS / DEFAULT_ONDELAY_CPS of usbhid-ups.h: the CPS firmware rounds
    // the delays down to multiples of 60 s, so 20 s would shut down at once
    int32_t defaultOffDelay() const override { return 60; }
    int32_t defaultOnDelay() const override { return 120; }

    // Rows of cps_hid2nut (cps-hid.c) (NutTable.h, US-066)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::cpsHid2Nut(n); }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::cpsDelayPaths(); }

    // cps_adjust_frequency_scale(): the factor (1 or 0.1) that brings the reported
    // frequency inside low..high; nominal, low and high are 0 when unknown. False when
    // neither fits: the value is published as it is and checked again on the next one.
    static bool frequencyScale(double report, double nominal, double low, double high, double& scale);

protected:
    uint32_t quickPollMs() const override { return 0; }
    uint32_t fullPollMs() const override { return 30000; }
    bool acceptPollReport(uint8_t report_type, uint8_t report_id) const override;
    // INPUT reports arrive on the interrupt endpoint: never request them on EP0
    bool pollInputReports() const override { return false; }
    void collectStringRequests(IHidTransport* host, const UPSData& data, std::vector<uint8_t>& out) const override;

private:
    // Vendor-defined reports (>= 130) and reports 4 and 6 hang some firmwares (issue #40)
    static const uint8_t FIRST_VENDOR_REPORT = 130;
    // The BR700ELCD layout (issue #40) has vendor reports: no control request beyond the
    // ones of the device strings is added for it
    static bool hasVendorReports(IHidTransport* host);
    void scaleFrequency(UPSData& d, double v, bool input);

    UsageMapIndex<CyberPowerDriver> _map;
    // String indices read from the reports (stringid_conversion)
    uint8_t _chemStrIdx;      // battery.type
    uint8_t _oemStrIdx;       // battery.mfr.date
    uint8_t _firmwareStrIdx;  // ups.firmware
    // cps_battery_scale(): PID 0x0501 may report battery voltage and frequencies scaled
    bool _mightNeedScale;
    double _batteryScale;
    bool _batteryScaleChecked;
    double _inputFreqScale;
    double _outputFreqScale;
    bool _inputFreqScaleChecked;
    bool _outputFreqScaleChecked;
};

#endif // CYBERPOWER_DRIVER_H
