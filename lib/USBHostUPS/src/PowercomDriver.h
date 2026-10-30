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
    // Rows of powercom_hid2nut (powercom-hid.c), NutTable.h (US-077)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::powercomHid2Nut(n); }
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of powercom_hid2nut (US-077)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::powercomHid2Nut(n); }
    uint8_t encodeBeeperValue(bool enable, uint16_t bit_size) const override;
    void formatDeviceStrings(UPSData& data) override;
    // powercom-hid.c drives load/shutdown with its own encodings (powercom_shutdown_info,
    // powercom_startup_info: minutes/seconds) and PCM vendor usages, not the generic
    // sequences: none are offered here (US-058). Their rows stay in powercomHid2Nut()
    bool shutdownCommandsSupported() const override { return false; }

    // powercom-hid.c:620-639 still declares ups.delay.start ("60") and ups.delay.shutdown
    // (DEFAULT_OFFDELAY) with info_len 8, also on PCMDelayBeforeShutdown (0x00ff0057 in
    // powercom_usage_lkp): the variables exist without the commands (US-066)
    bool delayVariablesSupported() const override { return true; }
    int32_t defaultOnDelay() const override { return 60; }
    uint8_t delayAux() const override { return 8; }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::powercomDelayPaths(); }

protected:
    // The SPD-750U MCU stalls when control requests come too close (issue #36)
    uint32_t stepSpacingMs() const override { return 800; }
    void buildPollLists(IHidTransport* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const override;
    // The reports whose rows are all HU_FLAG_STATIC are read once, as upstream (US-077)
    bool pollStaticReportsOnce() const override { return true; }
    bool isStaticUsage(const HIDUsageDef& u) const override;
    // The device strings are read once at the connection (formatDeviceStrings): only the
    // strings of the table's stringid_conversion rows are requested (battery.type, the
    // vendor ups.serial / ups.mfr)
    void collectStringRequests(IHidTransport*, const UPSData&, std::vector<uint8_t>& out) const override {
        collectTableStringRequests(out);
    }

private:
    // The last content of the status keep-alive report 0x0A: when it changes, the other
    // status reports are read next (see decodeReport)
    uint8_t _status_report[8];
    uint8_t _status_report_len;
};

#endif // POWERCOM_DRIVER_H
