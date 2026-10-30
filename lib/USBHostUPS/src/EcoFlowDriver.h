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

    // Rows of ecoflow_hid2nut (ecoflow-hid.c), NutTable.h (US-066, US-078)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::ecoflowHid2Nut(n); }
    // genericHid2Nut() rows whose keys come from ecoflowHid2Nut() (US-078)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of ecoflow_hid2nut: none, they are commented
    // out upstream (US-078)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::ecoflowHid2Nut(n); }
    // ecoflow_format_mfr: "EcoFlow" when the device has no manufacturer string (US-078)
    void formatDeviceStrings(UPSData& data) override;

    EcoFlowDriver() = default;
    virtual ~EcoFlowDriver() = default;
};

#endif // ECOFLOW_DRIVER_H
