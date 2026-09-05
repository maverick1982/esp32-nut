#ifndef APC_DRIVER_H
#define APC_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class APCDriver : public IUPSDriver {
public:
    const char* getDriverName() const override { return "APCDriver"; }
public:
    APCDriver();
    virtual ~APCDriver() = default;

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
    uint8_t _batteryDateStringIndex;
};

#endif // APC_DRIVER_H
