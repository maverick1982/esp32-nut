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
    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

    // Rows of mge_hid2nut (mge-hid.c), NutTable.h (US-066)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::mgeHid2Nut(n); }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::mgeDelayPaths(); }
    // genericHid2Nut() rows whose keys come from mgeHid2Nut() (US-073)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of mge_hid2nut (US-074)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::mgeHid2Nut(n); }
    // Model type and country as known now, for the tests
    const NutTable::Context& nutContext() const { return _nut_ctx; }

protected:
    bool acceptPollReport(uint8_t report_type, uint8_t report_id) const override;
    void collectStringRequests(IHidTransport* host, const UPSData& data, std::vector<uint8_t>& out) const override;
    void onLoop(IHidTransport* host, UPSData& data) override;
    // iModel and Flow.[4].ConfigApparentPower, read by decodeReport() for mge_type (US-120)
    bool readsFieldOutsideRows(const HIDUsageDef& u) const override;

private:
    // Usages of the context inputs, found once per device (US-079: no hand-written mappings)
    std::vector<size_t> _iModelUsages;
    std::vector<size_t> _apparentPowerUsages;
    bool _contextIndexed;

    // Inputs of mge_type (mge_format_model): the UPS.PowerSummary.iModel string, or
    // UPS.Flow.[4].ConfigApparentPower when it is empty; the country comes from the
    // device.country row (NutTable::Context::country_seen)
    void updateNutContext(IHidTransport* host, const UPSData& data);
    uint8_t _iModelIdx;
    bool _iModelIdxSeen;
    bool _iModelSeen;
    String _iModel;
    bool _apparentPowerSeen;
    long _apparentPower;
};

#endif // EATON_DRIVER_H
