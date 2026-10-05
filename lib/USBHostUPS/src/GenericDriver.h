#ifndef GENERIC_DRIVER_H
#define GENERIC_DRIVER_H

#include "IUPSDriver.h"
#include "UsageMapIndex.h"
#include <Arduino.h>
#include <vector>

/**
 * @brief Base of every driver: standard HID PDC mappings and the poll state machine.
 *
 * Poll cycles (review S3, A6, like usbhid-ups and ADR 0006):
 * - quick poll every quickPollMs(): only the reports that carry status usages
 *   (PresentStatus, RemainingCapacity, RunTimeToEmpty, PercentLoad...);
 * - full poll every fullPollMs(): the missing string descriptors, then every report.
 * One control request per loop() call, stepSpacingMs() apart, and none while
 * isPollingPaused(). A report the UPS has just sent as an INPUT report is skipped
 * (maxReportAgeMs()), and with pollStaticReportsOnce() so is a static report already
 * received. Derived drivers only change the policy through the hooks below instead of
 * copying the state machine.
 */
class GenericDriver : public IUPSDriver {
public:
    const char* getDriverName() const override { return "GenericDriver"; }

    GenericDriver();
    virtual ~GenericDriver() = default;

    void setup() override;
    void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;
    bool initialPollDone() const override { return _first_full_done; }

    // One control request of a poll cycle
    struct PollItem {
        uint8_t report_type; // 1 = INPUT, 3 = FEATURE, STRING_ITEM = string descriptor
        uint8_t id;          // report ID, or string index
        uint16_t length;     // expected length, 0 = from the report descriptor
    };
    static const uint8_t STRING_ITEM = 0;
    static const uint32_t STEP_SPACING_MS = 50;

    // True for the usages the quick poll keeps fresh
    static bool isStatusUsage(const HIDUsageDef& u);
    // True for the usages whose value changes at run time: status, measures, timers,
    // test result, beeper. The others (nominal values, limits, dates, string indices,
    // vendor usages without a name) are static.
    static bool isDynamicUsage(const HIDUsageDef& u);

protected:
    // --- Poll policy ---
    virtual uint32_t quickPollMs() const { return 2000; } // 0 = no quick poll
    virtual uint32_t fullPollMs() const { return 30000; }
    // Minimum gap between two control requests of a cycle
    virtual uint32_t stepSpacingMs() const { return STEP_SPACING_MS; }
    // A report whose values all arrived in an INPUT report younger than this is not
    // requested again (libhid.c refresh_report_buffer(), age = pollinterval, issue #60)
    virtual uint32_t maxReportAgeMs() const { return 2000; }
    // Static reports are read once per enumeration, then left out of the full poll, like
    // HU_FLAG_STATIC in usbhid-ups (issue #60). False = every full poll reads them.
    virtual bool pollStaticReportsOnce() const { return false; }
    // wLength of a GET_REPORT, given the length declared in the report descriptor
    virtual uint16_t requestLength(uint8_t report_type, uint8_t report_id, uint16_t expected) const { return expected; }
    // Reports a driver must never request (e.g. ones that freeze the firmware)
    virtual bool acceptPollReport(uint8_t report_type, uint8_t report_id) const { return true; }
    // INPUT reports without a FEATURE twin are requested with GET_REPORT too
    virtual bool pollInputReports() const { return true; }
    virtual void buildPollLists(IUSBHostUPS* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const;
    // String descriptors fetched at the start of a full poll (only the missing ones)
    virtual void collectStringRequests(IUSBHostUPS* host, const UPSData& data, std::vector<uint8_t>& out) const;
    virtual const char* upsTypeName() const { return getDriverName(); }
    // Called by every loop() with the host lock held: values that need no request
    virtual void onLoop(IUSBHostUPS* host, UPSData& data) {}

    String _active_beeper;
    uint8_t _batteryDateStringIndex;

private:
    void startCycle(IUSBHostUPS* host, const UPSData& data, bool full, uint32_t now);
    void appendNewStrings(IUSBHostUPS* host, const UPSData& data);
    void findInputTwins(IUSBHostUPS* host);
    void findStaticReports(IUSBHostUPS* host);
    bool isFreshFromInput(IUSBHostUPS* host, const PollItem& item, uint32_t now) const;
    bool isStaticAndKnown(const PollItem& item) const;
    static uint8_t typeSlot(uint8_t report_type) { return report_type == 1 ? 0 : 1; }

    UsageMapIndex<GenericDriver> _generic_map;
    bool _lists_built;
    std::vector<PollItem> _quick;
    std::vector<PollItem> _full;
    std::vector<PollItem> _queue;
    // Bitmap of the report IDs whose FEATURE values all come with the INPUT report too
    uint32_t _input_twin[8];
    // Bitmaps per report type (INPUT, FEATURE): reports with static values only, and
    // reports decoded at least once since the enumeration
    uint32_t _static_report[2][8];
    uint32_t _decoded_report[2][8];
    size_t _queue_pos;
    bool _step_now;
    bool _cycle_full;
    bool _strings_rechecked;              // second look at the strings done for this cycle
    bool _first_full_done;                // a full poll ended since setup()
    std::vector<uint8_t> _cycle_strings;  // string indices already requested in this cycle
    uint32_t _last_quick;
    uint32_t _last_full;
    uint32_t _last_step;
};

#endif // GENERIC_DRIVER_H
