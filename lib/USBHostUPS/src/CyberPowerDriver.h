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
    void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
};

#endif // CYBERPOWER_DRIVER_H
