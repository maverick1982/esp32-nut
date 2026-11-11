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

    // Back-UPS BX: the firmware stops answering on EP0 under the default 2 s quick poll
    // (issue #60). Their users run usbhid-ups with pollinterval = 10.
    static const uint32_t BX_QUICK_POLL_MS = 10000;
    // usbhid-ups "maxreport", the tweak "for buggy APC Back-UPS firmware" that returns a
    // wrong report size: libhid.c asks sizeof(rbuf->data[id]) bytes, a pointer (8 bytes on
    // 64 bit) instead of the declared length. Here at least 8, never less than declared.
    static const uint16_t BX_MIN_REQUEST_LENGTH = 8;
    static bool isBackUpsBX(const String& model) { return model.startsWith("Back-UPS BX"); }
    // The newer BXR series (issue #123) also matches isBackUpsBX(), but keeps sending INPUT
    // reports: it is left out of the frozen-values check below
    static bool isBackUpsBXR(const String& model) { return model.startsWith("Back-UPS BXR"); }
    // Back-UPS BX (not BXR): about 18 INPUT reports per second, never under ~1040 a minute
    // and back right after a port cycle; a silence of 15 s means the UPS answers GET_REPORT
    // with frozen values until it is unplugged or the board restarts (issue #138)
    static const uint32_t BX_INPUT_SILENCE_MS = 15000;
    uint32_t inputSilenceFaultMs() const override { return _bx_frozen_inputs ? BX_INPUT_SILENCE_MS : 0; }
    // Public: the host asks the same length for the SET VAR read-modify-write and read back
    uint16_t requestLength(uint8_t report_type, uint8_t report_id, uint16_t expected) const override {
        return (_back_ups_bx && expected < BX_MIN_REQUEST_LENGTH) ? BX_MIN_REQUEST_LENGTH : expected;
    }

    // apc-hid.c apc_format_model(): "<model> FW:<firmware> USB FW:<aux>". False when
    // the product string has no "FW:" (already split, or a model without firmware).
    static bool splitProduct(const String& product, String& model, String& firmware, String& aux);
    // ups.model without the firmware, which goes to ups.firmware / ups.firmware.aux (issue #76)
    void formatDeviceStrings(UPSData& data) override;

    // Rows of apc_hid2nut (apc-hid.c), NutTable.h (US-066, US-076)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::apcHid2Nut(n); }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::apcDelayPaths(); }
    // genericHid2Nut() rows whose keys come from apcHid2Nut() (US-076)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of apc_hid2nut (US-076)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::apcHid2Nut(n); }

protected:
    void onLoop(IHidTransport* host, UPSData& data) override;
    uint32_t quickPollMs() const override { return _back_ups_bx ? BX_QUICK_POLL_MS : GenericDriver::quickPollMs(); }
    uint32_t maxReportAgeMs() const override { return _back_ups_bx ? BX_QUICK_POLL_MS : GenericDriver::maxReportAgeMs(); }
    // Nominal values, limits, dates: half of the full poll of a BX750MI (issue #60)
    bool pollStaticReportsOnce() const override { return _back_ups_bx; }

private:
    bool _back_ups_bx;
    bool _bx_frozen_inputs; // Back-UPS BX but not BXR (issue #138)
};

#endif // APC_DRIVER_H
