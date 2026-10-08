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
    // usbhid-ups "maxreport", the tweak "for buggy APC Back-UPS firmware" that returns a
    // wrong report size: libhid.c asks sizeof(rbuf->data[id]) bytes, a pointer (8 bytes on
    // 64 bit) instead of the declared length. Here at least 8, never less than declared.
    static const uint16_t BX_MIN_REQUEST_LENGTH = 8;
    static bool isBackUpsBX(const String& model) { return model.startsWith("Back-UPS BX"); }

    // apc-hid.c apc_format_model(): "<model> FW:<firmware> USB FW:<aux>". False when
    // the product string has no "FW:" (already split, or a model without firmware).
    static bool splitProduct(const String& product, String& model, String& firmware, String& aux);
    // ups.model without the firmware, which goes to ups.firmware / ups.firmware.aux (issue #76)
    void formatDeviceStrings(UPSData& data) override;

protected:
    void onLoop(IUSBHostUPS* host, UPSData& data) override;
    uint32_t quickPollMs() const override { return _back_ups_bx ? BX_QUICK_POLL_MS : GenericDriver::quickPollMs(); }
    uint32_t maxReportAgeMs() const override { return _back_ups_bx ? BX_QUICK_POLL_MS : GenericDriver::maxReportAgeMs(); }
    // Nominal values, limits, dates: half of the full poll of a BX750MI (issue #60)
    bool pollStaticReportsOnce() const override { return _back_ups_bx; }
    uint16_t requestLength(uint8_t report_type, uint8_t report_id, uint16_t expected) const override {
        return (_back_ups_bx && expected < BX_MIN_REQUEST_LENGTH) ? BX_MIN_REQUEST_LENGTH : expected;
    }

private:
    UsageMapIndex<APCDriver> _map;
    bool _back_ups_bx;
};

#endif // APC_DRIVER_H
