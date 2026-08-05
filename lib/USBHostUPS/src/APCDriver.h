#ifndef APC_DRIVER_H
#define APC_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class APCDriver : public IUPSDriver {
public:
    APCDriver();
    virtual ~APCDriver() = default;

    void setup() override;
    void loop(USBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

private:
    uint32_t _last_poll;
    uint32_t _last_fast_poll;
    uint8_t  _poll_step;
    uint32_t _last_step_time;
};

#endif // APC_DRIVER_H
