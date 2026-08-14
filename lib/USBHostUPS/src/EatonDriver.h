#ifndef EATON_DRIVER_H
#define EATON_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class EatonDriver : public IUPSDriver {
public:
    EatonDriver();
    virtual ~EatonDriver() = default;

    void setup() override;
    void loop(USBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

private:
    uint32_t _last_poll;
    uint8_t _chemStrIdx;
    uint8_t _poll_step;
    uint32_t _last_step_time;
    uint32_t _last_fast_poll;
    uint8_t _slow_poll_counter;
};

#endif // EATON_DRIVER_H
