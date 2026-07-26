#ifndef CYBERPOWER_DRIVER_H
#define CYBERPOWER_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class CyberPowerDriver : public IUPSDriver {
public:
    CyberPowerDriver();
    virtual ~CyberPowerDriver() = default;

    void setup() override;
    void loop(USBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

private:
    uint32_t _last_poll;
};

#endif // CYBERPOWER_DRIVER_H
