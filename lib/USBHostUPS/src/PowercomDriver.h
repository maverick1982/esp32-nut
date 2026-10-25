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
    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;
    uint8_t encodeBeeperValue(bool enable, uint16_t bit_size) const override;
    // powercom-hid.c drives load/shutdown with its own encodings (powercom_shutdown_info,
    // powercom_startup_info: minutes/seconds) and PCM vendor usages, not the generic
    // sequences: none are offered here (US-058)
    bool shutdownCommandsSupported() const override { return false; }

    // powercom-hid.c:620-639 still declares ups.delay.start ("60") and ups.delay.shutdown
    // (DEFAULT_OFFDELAY) with info_len 8, also on PCMDelayBeforeShutdown (0x00ff0057 in
    // powercom_usage_lkp): the variables exist without the commands (US-066)
    bool delayVariablesSupported() const override { return true; }
    int32_t defaultOnDelay() const override { return 60; }
    uint8_t delayAux() const override { return 8; }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::powercomDelayPaths(); }

protected:
    void onLoop(IHidTransport* host, UPSData& data) override;
    // The SPD-750U MCU stalls when control requests come too close (issue #36)
    uint32_t stepSpacingMs() const override { return 800; }
    void buildPollLists(IHidTransport* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const override;
    // Model and vendor come from the PID table: no string descriptor requests
    void collectStringRequests(IHidTransport*, const UPSData&, std::vector<uint8_t>&) const override {}
};

#endif // POWERCOM_DRIVER_H
