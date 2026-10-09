#ifndef ECOFLOW_DRIVER_H
#define ECOFLOW_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class EcoFlowDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "EcoFlowDriver"; }

    // ecoflow-hid.c leaves the beeper commands commented out: the River 3 Plus and
    // Delta 3 Plus do not respond to them ("Does not seem controllable"). Keep
    // ups.beeper.status readable, but do not offer or attempt the toggle.
    bool beeperControllable() const override { return false; }

    // ecoflow-hid.c leaves shutdown.reboot/shutdown.stop commented out as well (FIXME:
    // untested): no load.* or shutdown.* commands (US-058)
    bool shutdownCommandsSupported() const override { return false; }

    EcoFlowDriver();
    virtual ~EcoFlowDriver() = default;

    void setup() override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

protected:
    void collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const override;
    void onLoop(IUSBHostUPS* host, UPSData& data) override;

private:
    UsageMapIndex<EcoFlowDriver> _map;
    uint8_t _chemStrIdx;
    // Last value seen on UPS.OutletSystem.Outlet.DelayBeforeShutdown. Upstream maps
    // only that usage to ups.timer.shutdown; the value is cached so the PowerSummary
    // one can be ignored without depending on which report is decoded last.
    bool _outletShutdownSeen;
    String _outletShutdownTimer;
};

#endif // ECOFLOW_DRIVER_H
