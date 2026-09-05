#ifndef APC_DRIVER_H
#define APC_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class APCDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "APCDriver"; }
public:
    APCDriver();
    virtual ~APCDriver() = default;

    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
};

#endif // APC_DRIVER_H
