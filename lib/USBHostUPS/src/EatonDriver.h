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
    void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

private:
    uint8_t _chemStrIdx;
};

#endif // EATON_DRIVER_H
