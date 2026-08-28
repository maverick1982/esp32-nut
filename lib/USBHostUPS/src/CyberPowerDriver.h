#ifndef CYBERPOWER_DRIVER_H
#define CYBERPOWER_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class CyberPowerDriver : public IUPSDriver {
public:
    CyberPowerDriver();
    virtual ~CyberPowerDriver() = default;

    void setup() override;
    void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

private:
    uint32_t _last_poll;
    uint32_t _last_fast_poll;
    uint32_t _last_step_time;
    uint8_t _poll_step;
    uint8_t _slow_poll_counter;
    String _active_beeper;
};

#endif // CYBERPOWER_DRIVER_H
