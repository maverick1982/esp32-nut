#ifndef GENERIC_DRIVER_H
#define GENERIC_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>

class GenericDriver : public IUPSDriver {
public:
    const char* getDriverName() const override { return "GenericDriver"; }
public:
    GenericDriver();
    virtual ~GenericDriver() = default;

    void setup() override;
    void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

protected:
    virtual uint32_t getPollPacingMs() { return 50; }
    virtual uint32_t getFastPollIntervalMs() { return 2000; }
    virtual uint32_t getFullPollIntervalMs() { return 30000; }
    virtual bool shouldPollUsage(const String& path) { return true; }
    virtual bool isStaticUsage(const String& path);

    uint32_t _last_poll;
    uint32_t _last_fast_poll;
    uint32_t _last_full_poll;
    uint32_t _last_step_time;
    uint8_t _poll_step;
    uint8_t _slow_poll_counter;
    bool _is_full_walk;
    String _active_beeper;
    uint8_t _batteryDateStringIndex;
};

#endif // GENERIC_DRIVER_H
