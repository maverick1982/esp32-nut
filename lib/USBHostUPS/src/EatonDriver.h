#ifndef EATON_DRIVER_H
#define EATON_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class EatonDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "EatonDriver"; }
public:
    EatonDriver();
    virtual ~EatonDriver() = default;

    void setup() override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

    // Rows of mge_hid2nut (mge-hid.c), NutTable.h (US-066)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::mgeHid2Nut(n); }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::mgeDelayPaths(); }
    // Model type and country as known now, for the tests
    const NutTable::Context& nutContext() const { return _nut_ctx; }

protected:
    bool acceptPollReport(uint8_t report_type, uint8_t report_id) const override;
    void collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const override;
    void onLoop(IUSBHostUPS* host, UPSData& data) override;

private:
    UsageMapIndex<EatonDriver> _map;
    uint8_t _chemStrIdx;

    // Inputs of mge_type (mge_format_model): the UPS.PowerSummary.iModel string, or
    // UPS.Flow.[4].ConfigApparentPower when it is empty, and UPS.PowerSummary.Country
    void updateNutContext(IUSBHostUPS* host, const UPSData& data);
    uint8_t _iModelIdx;
    bool _iModelIdxSeen;
    bool _iModelSeen;
    String _iModel;
    bool _apparentPowerSeen;
    long _apparentPower;
    bool _countrySeen;
};

#endif // EATON_DRIVER_H
