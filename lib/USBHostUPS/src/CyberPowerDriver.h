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
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;

protected:
    uint32_t quickPollMs() const override { return 0; }
    uint32_t fullPollMs() const override { return 30000; }
    bool acceptPollReport(uint8_t report_type, uint8_t report_id) const override;
    // INPUT reports arrive on the interrupt endpoint: never request them on EP0
    bool pollInputReports() const override { return false; }

private:
    UsageMapIndex<CyberPowerDriver> _map;
};

#endif // CYBERPOWER_DRIVER_H
