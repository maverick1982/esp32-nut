#ifndef OPENUPS_DRIVER_H
#define OPENUPS_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class OpenUPSDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "OpenUPSDriver"; }
public:
    OpenUPSDriver() = default;
    virtual ~OpenUPSDriver() = default;

    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
};

#endif // OPENUPS_DRIVER_H
