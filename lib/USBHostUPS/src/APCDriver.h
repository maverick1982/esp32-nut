#ifndef APC_DRIVER_H
#define APC_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

class APCDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "APCDriver"; }
public:
    APCDriver();
    virtual ~APCDriver() = default;

    void setup() override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;

    // Back-UPS BX: the firmware stops answering on EP0 under the default 2 s quick poll
    // (issue #60). Their users run usbhid-ups with pollinterval = 10.
    static const uint32_t BX_QUICK_POLL_MS = 10000;
    static bool isBackUpsBX(const String& model) { return model.startsWith("Back-UPS BX"); }

protected:
    void onLoop(IUSBHostUPS* host, UPSData& data) override;
    uint32_t quickPollMs() const override { return _back_ups_bx ? BX_QUICK_POLL_MS : GenericDriver::quickPollMs(); }
    uint32_t maxReportAgeMs() const override { return _back_ups_bx ? BX_QUICK_POLL_MS : GenericDriver::maxReportAgeMs(); }

private:
    UsageMapIndex<APCDriver> _map;
    bool _back_ups_bx;
};

#endif // APC_DRIVER_H
