#ifndef GENERIC_DRIVER_H
#define GENERIC_DRIVER_H

#include "IUPSDriver.h"
#include <Arduino.h>
#include <vector>
#include <utility>

/**
 * @brief Base of every driver: the NUT table engine, the hooks and the poll state machine.
 *
 * Every value comes from a row (ADR 0011, US-079): the driver's table (hid2nut()), then the
 * rows of genericHid2Nut() it does not list in supersededGenericPaths(). GenericDriver itself,
 * for the brands without a subdriver, has genericHid2Nut() as its table.
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
    // HidControl: commands and SET VAR built on the HID usages (US-105)
    const IUpsControl& control() const override;

    GenericDriver();
    virtual ~GenericDriver() = default;

    void setup() override;
    void loop(IHidTransport* host, UPSData& data, uint32_t now) override;
    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;
    bool initialPollDone() const override { return _first_full_done; }
    void postDecode(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void writableVariables(const UPSData& data, std::vector<NutTable::RWVariable>& out) const override;
    NutTable::SetStatus planSet(const UPSData& data, const std::vector<HIDUsageDef>& usages,
                                const char* name, const char* value, NutTable::SetPlan& plan) const override;
    void setDriverSideValue(const char* name, const char* value) override;
    void noteHostValue(const char* name, const char* value) override;
    bool resetDriverSideValue(const char* name, String& value) override;
    void invalidateReport(uint8_t report_type, uint8_t report_id) override;

    // Hook of the replay runner (US-067): ends the first full poll as loop() does, and applies
    // NutTable::finishInit() as postDecode() then would. Not called by the firmware.
    void completeInitialPoll();

    // NUT table of the driver's upstream subdriver (US-066); genericHid2Nut() for the brands
    // without one (US-079)
    virtual const NutTable::HidInfo* hid2nut(size_t& n) const { return NutTable::genericHid2Nut(n); }
    // Paths of the genericHid2Nut() rows that a driver leaves to its own table (ADR 0011,
    // US-073, US-079): their keys come only from the driver's rows. Empty by default.
    virtual const char* const* supersededGenericPaths(size_t& n) const { n = 0; return nullptr; }
    // Commands and ups.delay.* of the brands without a subdriver: the generic rows (US-079);
    // the drivers of a subdriver return their own table and paths
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::genericHid2Nut(n); }
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::genericDelayPaths(); }
    // Rows bound to the attached device; built on the first decoded report
    const std::vector<NutTable::Bound>& nutBindings() const { return _nut_rows; }

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
    // A usage whose value never changes: a report with only such usages is static. By default
    // the usage code decides (isDynamicUsage); a driver can follow its table's HU_FLAG_STATIC
    virtual bool isStaticUsage(const HIDUsageDef& u) const { return !isDynamicUsage(u); }
    // wLength of a GET_REPORT: IUPSDriver::requestLength(), public as the host uses it too
    // Reports a driver must never request (e.g. ones that freeze the firmware)
    virtual bool acceptPollReport(uint8_t report_type, uint8_t report_id) const { return true; }
    // INPUT reports without a FEATURE twin are requested with GET_REPORT too
    virtual bool pollInputReports() const { return true; }
    virtual void buildPollLists(IHidTransport* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) const;
    // True for the usages of the HU_FLAG_QUICK_POLL status rows of hid2nut() (US-073)
    bool isQuickPollStatusUsage(const HIDUsageDef& u) const;
    // The FEATURE report of the first usage with the row's path, 0 when none (US-077)
    static uint8_t featureReportOf(const std::vector<HIDUsageDef>& usages, const NutTable::HidInfo& row);
    // Appends the FEATURE reports of the hid2nut() rows keep(row, index) accepts, in table
    // order and without duplicates: poll lists built on the table (US-077, also for US-078)
    template <typename Keep>
    void appendTableReports(const std::vector<HIDUsageDef>& usages, std::vector<PollItem>& out, Keep keep) const {
        size_t n = 0;
        const NutTable::HidInfo* table = hid2nut(n);
        for (size_t i = 0; i < n; i++) {
            if (!keep(table[i], i)) continue;
            uint8_t id = featureReportOf(usages, table[i]);
            if (!id) continue;
            bool found = false;
            for (const auto& p : out) {
                if (p.report_type == 3 && p.id == id) { found = true; break; }
            }
            if (!found) out.push_back(PollItem{3, id, 0});
        }
    }
    // False when the strings of the table's stringid_conversion rows must not be requested
    // (CyberPower vendor layouts, issue #40); the device strings are requested anyway (US-075)
    virtual bool requestTableStrings(IHidTransport* host) const { return true; }
    // String descriptors fetched at the start of a full poll (only the missing ones)
    virtual void collectStringRequests(IHidTransport* host, const UPSData& data, std::vector<uint8_t>& out) const;
    // The strings of the table's stringid_conversion rows still waiting for their text;
    // own_rows false: only those of the genericHid2Nut() tail (the battery date, US-079)
    void collectTableStringRequests(std::vector<uint8_t>& out, bool own_rows = true) const;
    // Puts a report at the head of the pending requests, inside the running cycle or as a
    // cycle of its own; nothing when it is already pending (US-077). Same task as loop():
    // decodeReport() runs from it
    void pollSoon(const PollItem& item);
    // True when the driver reads the report of u: INPUT, or a FEATURE report acceptPollReport() takes
    bool isReportRead(const HIDUsageDef& u) const;
    // True when the device measures ups.realpower in a report this driver reads
    bool readsActivePower(IHidTransport* host) const;
    // Text of a string descriptor (bLength, bDescriptorType 0x03, UTF-16LE), with the
    // inverted strings of some CyberPower firmwares turned back
    static String decodeStringDescriptor(IHidTransport* host, const uint8_t *data, size_t length);
    // Called by every loop() with the host lock held: values that need no request
    virtual void onLoop(IHidTransport* host, UPSData& data) {}
    // The status bits of the BOOL rows as they become the internal ups.status.* keys: a driver
    // can drop a bit its device reports wrongly (US-078). The engine's bits are not changed.
    virtual void filterStatus(NutTable::StatusBits& status) const { (void)status; }
    // ups.beeper.status from a value of the active beeper (_active_beeper), for the drivers
    // whose own table has no ups.beeper.status row (US-079): a hook, not a row, since it
    // depends on the field (1 bit: on/off) and on the brand's encoding (ADR 0011 rule 1).
    // By default beeper_info of usbhid-ups (1 disabled, 2 enabled, 3 muted).
    virtual void decodeBeeperStatus(UPSData& data, double v, const HIDUsageDef& def);

    String _active_beeper;
    // Inputs of the model-dependent conversions (mge_type, country, nominal voltage
    // class); mutable because LIST ENUM evaluates them like the upstream functions do
    mutable NutTable::Context _nut_ctx;

private:
    void bindNutTable(IHidTransport* host);
    // Writes the values of the table rows and the internal ups.status.* keys that differ
    // from ups_data (US-073): nothing is rewritten when the value is the same
    void publishNutRows(UPSData& ups_data);
    void publishRow(NutTable::Bound& b, UPSData& ups_data);
    // ups.alarm and input.transfer.reason from the internal keys and the vendor ups.alarm
    // rows, for every driver (US-107)
    void publishAlarms(UPSData& ups_data);

    std::vector<NutTable::Bound> _nut_rows;
    size_t _own_rows;                     // _nut_rows from hid2nut(); the generic tail follows
    bool _beeper_row;                     // one of them is ups.beeper.status: no decodeBeeperStatus()
    std::vector<size_t> _beeper_usages;   // usages of the active beeper path
    bool _beeper_indexed;
    bool _nut_bound;
    bool _nut_init_done;                  // finishInit() applied after the first full poll
    bool _nut_has_realpower;              // a table row owns ups.realpower: no estimate (US-074)
    size_t _nut_usage_count;
    // Values set for the HU_FLAG_ABSENT rows (name, value): applied again at every bind
    std::vector<std::pair<String, String>> _absent_overrides;
    void applyAbsentOverride(const String& name, const String& value);
    void startCycle(IHidTransport* host, const UPSData& data, bool full, uint32_t now);
    void appendNewStrings(IHidTransport* host, const UPSData& data);
    void findInputTwins(IHidTransport* host);
    void findStaticReports(IHidTransport* host);
    bool isFreshFromInput(IHidTransport* host, const PollItem& item, uint32_t now) const;
    bool isStaticAndKnown(const PollItem& item) const;
    static uint8_t typeSlot(uint8_t report_type) { return report_type == 1 ? 0 : 1; }

    bool _lists_built;
    bool _active_power_checked;           // readsActivePower() looked at since setup()
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
