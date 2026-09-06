#ifndef POWERCOM_DRIVER_H
#define POWERCOM_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class PowercomDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "PowercomDriver"; }
public:
    PowercomDriver();
    virtual ~PowercomDriver() = default;

    void setup() override;
    void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;
    uint8_t encodeBeeperValue(bool enable, uint16_t bit_size) const override;

protected:
    uint32_t getPollPacingMs() override { return 800; } // Very slow pacing for Powercom MCUs
    uint32_t getFastPollIntervalMs() override { return 5000; } // Slower fast-poll (5s)

private:
    uint32_t _last_0xa4_poll;
    uint8_t _mfr_retries;
    uint8_t _prod_retries;
    uint8_t _serial_retries;

    String fetchVoltageHack(IUSBHostUPS* host);
};

#endif // POWERCOM_DRIVER_H
