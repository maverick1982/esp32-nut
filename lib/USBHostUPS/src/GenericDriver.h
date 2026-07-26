#ifndef GENERIC_DRIVER_H
#define GENERIC_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class GenericDriver : public IUPSDriver {
public:
    GenericDriver();
    virtual ~GenericDriver() = default;

    void setup() override;
    void loop(USBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

private:
    uint32_t _last_poll;
};

#endif // GENERIC_DRIVER_H
