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
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;
    uint8_t encodeBeeperValue(bool enable, uint16_t bit_size) const override;

protected:
    const char* upsTypeName() const override { return "Powercom"; }
    void onLoop(IUSBHostUPS* host, UPSData& data) override;
    // The SPD-750U MCU stalls when control requests come too close (issue #36)
    uint32_t stepSpacingMs() const override { return 800; }
    void buildPollLists(IUSBHostUPS* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const override;
    // Model and vendor come from the PID table: no string descriptor requests
    void collectStringRequests(IUSBHostUPS*, const UPSData&, std::vector<uint8_t>&) const override {}
};

#endif // POWERCOM_DRIVER_H
