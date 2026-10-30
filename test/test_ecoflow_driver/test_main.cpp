#include <unity.h>
#include "EcoFlowDriver.h"
#include "IHidTransport.h"
#include "UPSData.h"
#include <algorithm>
#include <string>
#include <vector>

class MockEcoFlowHost : public IHidTransport {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t) override {
        _requestedReports.push_back({report_id, report_type});
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

static EcoFlowDriver driver;
static MockEcoFlowHost mockHost;
static UPSData ups_data;

static void addUsage(const char* path, uint8_t report_id, uint8_t report_type,
                     uint16_t bit_offset, uint16_t bit_size) {
    HIDUsageDef u;
    u.report_id = report_id;
    u.report_type = report_type;
    u.bit_offset = bit_offset;
    u.bit_size = bit_size;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    u.path[sizeof(u.path) - 1] = '\0';
    mockHost._usages.push_back(u);
}

void setUp(void) {
    ups_data = UPSData();
    mockHost._usages.clear();
    mockHost._requestedStrings.clear();
    mockHost._requestedReports.clear();
    mockHost._iManufacturer = 0;
    mockHost._iProduct = 0;
    mockHost._iSerialNumber = 0;
    driver.setup();
}

void tearDown(void) {}

// As USBHostUPS: every report goes through decodeReport() and then postDecode(), where the
// NUT table writes its rows (US-078: every EcoFlow value comes from ecoflowHid2Nut())
static void decode(uint8_t type, const std::vector<uint8_t>& bytes) {
    driver.decodeReport(&mockHost, bytes[0], type, bytes.data(), bytes.size(), ups_data);
    driver.postDecode(&mockHost, bytes[0], type, bytes.data(), bytes.size(), ups_data);
}

struct SynthReport {
    uint8_t type;
    std::vector<uint8_t> bytes; // report ID first, values little endian
};

static void decodeAll(const std::vector<SynthReport>& reports) {
    for (const auto& r : reports) decode(r.type, r.bytes);
}

// A USB string descriptor (UTF-16LE) passed to the driver, then formatDeviceStrings() as
// USBHostUPS does after every string descriptor
static void stringDescriptor(uint8_t index, const char* text) {
    std::vector<uint8_t> desc = { (uint8_t)(2 + 2 * strlen(text)), 0x03 };
    for (const char* c = text; *c; c++) {
        desc.push_back((uint8_t)*c);
        desc.push_back(0);
    }
    driver.parseStringDescriptor(&mockHost, index, desc.data(), desc.size(), ups_data);
    driver.formatDeviceStrings(ups_data);
}

static std::string status() { return UPSData::computeUPSStatusString(ups_data).c_str(); }

// ecoflow-hid.c:193: RunTimeToEmpty is in minutes, ecoflow_runtime_conversion_lkp gives seconds
void test_ecoflow_runtime_minutes_to_seconds(void) {
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 0x0D, 1, 0, 16);

    decode(1, { 0x0D, 0x88, 0x07 }); // 1928 minutes

    TEST_ASSERT_EQUAL_STRING("115680", ups_data.get("battery.runtime").c_str()); // 1928 * 60
}

// ecoflow-hid.c:130, 133, 184, 200: DesignCapacity and FullChargeCapacity are percentages;
// RemainingTimeLimit stays in minutes, as upstream (US-078 user decision of 2026-10-30)
void test_ecoflow_capacity_and_warning_mappings(void) {
    addUsage("UPS.PowerSummary.DesignCapacity", 0x17, 3, 0, 8);
    addUsage("UPS.PowerSummary.FullChargeCapacity", 0x0E, 3, 0, 8);
    addUsage("UPS.PowerSummary.WarningCapacityLimit", 0x0F, 3, 0, 8);
    addUsage("UPS.PowerSummary.RemainingTimeLimit", 0x08, 3, 0, 16);

    decode(3, { 0x17, 0x64 });       // 100 %
    decode(3, { 0x0E, 0x62 });       // 98 %
    decode(3, { 0x0F, 0x0A });       // 10 %
    decode(3, { 0x08, 0x58, 0x02 }); // 600 minutes

    TEST_ASSERT_EQUAL_STRING("100", ups_data.get("battery.capacity.nominal").c_str());
    TEST_ASSERT_EQUAL_STRING("98", ups_data.get("battery.capacity").c_str());
    TEST_ASSERT_EQUAL_STRING("10", ups_data.get("battery.charge.warning").c_str());
    TEST_ASSERT_EQUAL_STRING("600", ups_data.get("battery.runtime.low").c_str());
}

// ecoflow-hid.c:122 maps UPS.Flow.[4].ConfigActivePower to ups.power.nominal; the generic
// mapping of the same usage to ups.realpower.nominal is kept (ADR 0011 rule 2)
void test_ecoflow_config_active_power_keeps_generic_realpower_nominal(void) {
    addUsage("UPS.Flow.ConfigActivePower", 0x01, 3, 0, 16);
    mockHost._usages.back().coll_index[1] = 5; // Flow is the collection [4]

    decode(3, { 0x01, 0x04, 0x01 }); // 260

    TEST_ASSERT_EQUAL_STRING("260", ups_data.get("ups.power.nominal").c_str());
    TEST_ASSERT_EQUAL_STRING("260", ups_data.get("ups.realpower.nominal").c_str());
}

// Without the [4] index the usage is not the one of ecoflow-hid.c:122
void test_ecoflow_config_active_power_needs_flow_index(void) {
    addUsage("UPS.Flow.ConfigActivePower", 0x01, 3, 0, 16);

    decode(3, { 0x01, 0x04, 0x01 });

    TEST_ASSERT_FALSE(ups_data.hasKey("ups.power.nominal"));
    TEST_ASSERT_EQUAL_STRING("260", ups_data.get("ups.realpower.nominal").c_str());
}

// ecoflow-hid.c:126-127
void test_ecoflow_outlet_timers(void) {
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeReboot", 0x13, 3, 0, 16);
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x12, 3, 0, 16);

    decode(3, { 0x13, 0x1E, 0x00 }); // 30
    decode(3, { 0x12, 0x3C, 0x00 }); // 60

    TEST_ASSERT_EQUAL_STRING("30", ups_data.get("ups.timer.reboot").c_str());
    TEST_ASSERT_EQUAL_STRING("60", ups_data.get("ups.timer.shutdown").c_str());
}

// ecoflow-hid.c:135: iDeviceChemistry via stringid_conversion -> battery.type
void test_ecoflow_chemistry_string_descriptor(void) {
    addUsage("UPS.PowerSummary.iDeviceChemistry", 0x1F, 3, 0, 8);

    decode(3, { 0x1F, 0x02 }); // string index 2

    // The next full poll must request the chemistry string
    driver.loop(&mockHost, ups_data, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, mockHost._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(2, mockHost._requestedStrings[0]);

    stringDescriptor(2, "Li");
    TEST_ASSERT_EQUAL_STRING("Li", ups_data.get("battery.type").c_str());
}

void test_ecoflow_present_status(void) {
    addUsage("UPS.PowerSummary.PresentStatus.Discharging", 0x07, 1, 1, 1);
    addUsage("UPS.PowerSummary.PresentStatus.ACPresent", 0x07, 1, 2, 1);

    decode(1, { 0x07, 0x02 }); // Discharging=1, ACPresent=0
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", status().c_str());

    decode(1, { 0x07, 0x04 }); // Discharging=0, ACPresent=1
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
}

// ecoflow_battery_runtime_conversion returns NULL for a negative value: ups_infoval_set()
// writes nothing, so the last value stays (before US-078 the key was removed)
void test_ecoflow_runtime_negative_keeps_last_value(void) {
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 0x0D, 1, 0, 16);
    mockHost._usages.back().logical_min = -32768;

    decode(1, { 0x0D, 0x0A, 0x00 }); // 10 minutes
    TEST_ASSERT_EQUAL_STRING("600", ups_data.get("battery.runtime").c_str());
    decode(1, { 0x0D, 0xFF, 0xFF });
    TEST_ASSERT_EQUAL_STRING("600", ups_data.get("battery.runtime").c_str());
}

// A negative first value publishes nothing (and, as a failed ups_infoval_set() in the INIT
// walk, leaves the row out)
void test_ecoflow_runtime_negative_first_publishes_nothing(void) {
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 0x0D, 1, 0, 16);
    mockHost._usages.back().logical_min = -32768;

    decode(1, { 0x0D, 0xFF, 0xFF });
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.runtime"));
}

// ecoflow_format_mfr() falls back to "EcoFlow" when the device has no Vendor string
void test_ecoflow_mfr_falls_back_when_no_manufacturer_string(void) {
    driver.formatDeviceStrings(ups_data);

    TEST_ASSERT_EQUAL_STRING("EcoFlow", ups_data.get("ups.mfr").c_str());
}

// A Vendor string that decodes to empty keeps the same "EcoFlow" fallback
void test_ecoflow_mfr_falls_back_when_manufacturer_string_empty(void) {
    mockHost._iManufacturer = 3;

    stringDescriptor(3, "");

    TEST_ASSERT_EQUAL_STRING("EcoFlow", ups_data.get("ups.mfr").c_str());
}

// A reported Vendor string is kept, and replaces the fallback when it arrives later
void test_ecoflow_mfr_keeps_reported_vendor(void) {
    mockHost._iManufacturer = 3;
    driver.formatDeviceStrings(ups_data);

    stringDescriptor(3, "A");

    TEST_ASSERT_EQUAL_STRING("A", ups_data.get("ups.mfr").c_str());
}

// ecoflow-hid.c leaves the beeper commands commented out: the device ignores them (US-054)
void test_ecoflow_beeper_not_controllable(void) {
    TEST_ASSERT_FALSE(driver.beeperControllable());
}

// ecoflow-hid.c:114: ups.beeper.status from beeper_info
void test_ecoflow_beeper_status(void) {
    addUsage("UPS.PowerSummary.AudibleAlarmControl", 0x02, 3, 0, 8);

    decode(3, { 0x02, 0x02 });
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());
    decode(3, { 0x02, 0x01 });
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());
    decode(3, { 0x02, 0x03 });
    TEST_ASSERT_EQUAL_STRING("muted", ups_data.get("ups.beeper.status").c_str());
}

// ecoflow-hid.c:127 maps only UPS.OutletSystem.Outlet.DelayBeforeShutdown to
// ups.timer.shutdown; the PowerSummary usage is an unmapped row upstream, whatever the order
void test_ecoflow_shutdown_timer_outlet_wins_when_outlet_first(void) {
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x12, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x20, 3, 0, 16);

    decode(3, { 0x12, 0x3C, 0x00 }); // 60
    decode(3, { 0x20, 0x1E, 0x00 }); // 30

    TEST_ASSERT_EQUAL_STRING("60", ups_data.get("ups.timer.shutdown").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
}

void test_ecoflow_shutdown_timer_outlet_wins_when_powersummary_first(void) {
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 0x12, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x20, 3, 0, 16);

    decode(3, { 0x20, 0x1E, 0x00 }); // 30
    decode(3, { 0x12, 0x3C, 0x00 }); // 60

    TEST_ASSERT_EQUAL_STRING("60", ups_data.get("ups.timer.shutdown").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
}

// Without the Outlet usage neither key is exposed
void test_ecoflow_shutdown_timer_absent_without_outlet(void) {
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x20, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeReboot", 0x21, 3, 0, 16);

    decode(3, { 0x20, 0x1E, 0x00 });
    decode(3, { 0x21, 0x1E, 0x00 });

    TEST_ASSERT_FALSE(ups_data.hasKey("ups.timer.shutdown"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.timer.reboot"));
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
}

// ecoflow-hid.c:125 maps UPS.PowerSummary.ConfigVoltage with "%.1f" (issue 67)
void test_ecoflow_battery_voltage_nominal_one_decimal(void) {
    addUsage("UPS.PowerSummary.ConfigVoltage", 0x08, 3, 0, 16);
    mockHost._usages.back().exponent = -2;

    decode(3, { 0x08, 0xB0, 0x04 }); // 12.00 V
    TEST_ASSERT_EQUAL_STRING("12.0", ups_data.get("battery.voltage.nominal").c_str());

    decode(3, { 0x08, 0x50, 0x05 }); // 13.60 V
    TEST_ASSERT_EQUAL_STRING("13.6", ups_data.get("battery.voltage.nominal").c_str());
}

// US-058: ecoflow-hid.c leaves the shutdown commands commented out
void test_ecoflow_no_shutdown_commands(void) {
    TEST_ASSERT_FALSE(driver.shutdownCommandsSupported());
}

// The commands of ecoflow-hid.c are all commented out: the command table has none (US-078)
void test_ecoflow_command_table_has_no_commands(void) {
    size_t n = 0;
    const NutTable::HidInfo* t = driver.commandTable(n);
    TEST_ASSERT_TRUE(n > 0);
    for (size_t i = 0; i < n; i++) TEST_ASSERT_FALSE_MESSAGE(NutTable::isCommandRow(t[i]), t[i].info_type);
}

// --- Synthetic EcoFlow layout (US-078): one usage for each path of ecoflow_hid2nut, as a
// River 3+ / Delta 3+ could report them. No fixture exists (PR #66): byte values are chosen
// to be told apart in the snapshot.

static void ecoflowLayout() {
    static const char* const status[] = {
        "ACPresent", "BatteryPresent", "BelowRemainingCapacityLimit", "CommunicationLost",
        "Charging", "Discharging", "FullyCharged", "FullyDischarged", "NeedReplacement",
        "Overload", "RemainingTimeLimitExpired", "ShutdownImminent", "ShutdownRequested",
        "VoltageNotRegulated",
    };
    for (uint16_t b = 0; b < sizeof(status) / sizeof(status[0]); b++) {
        char path[80];
        snprintf(path, sizeof(path), "UPS.PowerSummary.PresentStatus.%s", status[b]);
        addUsage(path, 1, 3, b, 1);
    }
    addUsage("UPS.PowerSummary.AudibleAlarmControl", 2, 3, 0, 8);
    // UPS.Flow.[4].ConfigActivePower: Flow is the collection at depth 1, index 4 -> 5
    addUsage("UPS.Flow.ConfigActivePower", 3, 3, 0, 16);
    mockHost._usages.back().coll_index[1] = 5;
    addUsage("UPS.PowerSummary.ConfigVoltage", 4, 3, 0, 16);
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeReboot", 5, 3, 0, 16);
    addUsage("UPS.OutletSystem.Outlet.DelayBeforeShutdown", 6, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeShutdown", 7, 3, 0, 16);
    addUsage("UPS.PowerSummary.DelayBeforeReboot", 8, 3, 0, 16);
    addUsage("UPS.PowerSummary.DesignCapacity", 9, 3, 0, 8);
    addUsage("UPS.PowerSummary.FullChargeCapacity", 9, 3, 8, 8);
    addUsage("UPS.PowerSummary.iDeviceChemistry", 10, 3, 0, 8);
    addUsage("UPS.PowerSummary.iOEMInformation", 10, 3, 8, 8);
    addUsage("UPS.PowerSummary.ManufacturerDate", 11, 3, 0, 16);
    addUsage("UPS.PowerSummary.Rechargeable", 12, 3, 0, 8);
    addUsage("UPS.PowerSummary.RemainingCapacity", 13, 3, 0, 8);
    addUsage("UPS.PowerSummary.RemainingCapacityLimit", 13, 3, 8, 8);
    addUsage("UPS.PowerSummary.RemainingTimeLimit", 14, 3, 0, 16);
    addUsage("UPS.PowerSummary.RunTimeToEmpty", 15, 3, 0, 16);
    addUsage("UPS.PowerSummary.Voltage", 16, 3, 0, 16);
    addUsage("UPS.PowerSummary.WarningCapacityLimit", 17, 3, 0, 8);
}

// On mains, charging: ACPresent, BatteryPresent, Charging
static std::vector<SynthReport> ecoflowReports() {
    return {
        { 3, { 1, 0x13, 0x00 } },
        { 3, { 2, 2 } },                  // beeper enabled
        { 3, { 3, 0x00, 0x04 } },         // 1024
        { 3, { 4, 52, 0 } },
        { 3, { 5, 30, 0 } },
        { 3, { 6, 60, 0 } },
        { 3, { 7, 90, 0 } },
        { 3, { 8, 120, 0 } },
        { 3, { 9, 100, 98 } },
        { 3, { 10, 2, 3 } },              // string indices
        { 3, { 11, 0x6F, 0x4E } },        // (39<<9)|(3<<5)|15: 2019/03/15
        { 3, { 12, 1 } },
        { 3, { 13, 87, 10 } },
        { 3, { 14, 120, 0 } },            // minutes
        { 3, { 15, 0x88, 0x07 } },        // 1928 minutes
        { 3, { 16, 51, 0 } },
        { 3, { 17, 5 } },
    };
}

// Every key sorted, one "key=value" per line, then ups.status
static std::string snapshotOf(const UPSData& data) {
    std::vector<std::string> lines;
    for (const auto& p : data.getAll()) lines.push_back(std::string(p.key.c_str()) + "=" + p.value.c_str());
    std::sort(lines.begin(), lines.end());
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    out += "ups.status=" + std::string(UPSData::computeUPSStatusString(data).c_str()) + "\n";
    return out;
}

// EcoFlowDriver with its poll hook public
struct EcoflowPollProbe : EcoFlowDriver {
    using EcoFlowDriver::buildPollLists;
};

static std::string pollListsOf() {
    EcoflowPollProbe probe;
    probe.setup();
    std::vector<GenericDriver::PollItem> quick, full;
    probe.buildPollLists(&mockHost, quick, full);
    std::string out = "quick:";
    for (const auto& i : quick) out += " " + std::to_string(i.id) + "/" + std::to_string(i.report_type);
    out += " | full:";
    for (const auto& i : full) out += " " + std::to_string(i.id) + "/" + std::to_string(i.report_type);
    return out;
}

// Before US-078 (hand-written mappings), kept as the reference of the differences below
static const char* const kEcoflowSnapshotBefore =
    "battery.capacity.nominal=100\n"
    "battery.capacity=98\n"
    "battery.charge.low=10\n"
    "battery.charge.warning=5\n"
    "battery.charge=87\n"
    "battery.runtime.low=7200\n"
    "battery.runtime=115680\n"
    "battery.type=LiFePO4\n"
    "battery.voltage.nominal=52.0\n"
    "battery.voltage=51.00\n"
    "ups.mfr.date=2019/03/15\n"
    "ups.mfr=EcoFlow\n"
    "ups.power.nominal=1024\n"
    "ups.realpower.nominal=1024\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.comm_lost=0\n"
    "ups.status.depleted=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.timer.reboot=30\n"
    "ups.timer.shutdown=60\n"
    "ups.status=OL CHRG\n";
static const char* const kEcoflowPollBefore =
    "quick: 1/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3";

// After US-078, every value from ecoflowHid2Nut(). Differences from kEcoflowSnapshotBefore:
// - battery.runtime.low 7200 -> 120: RemainingTimeLimit in minutes, as ecoflow-hid.c:184
//   (user decision of 2026-10-30);
// - battery.voltage 51.00 -> 51.0: "%.1f" of ecoflow-hid.c:197;
// - experimental.ups.powersummary.*: the active rows 136, 170, 172 and 173;
// - ups.beeper.status: row 114 (beeper_info); the generic mapping did not publish it;
// - ups.status.comm_lost -> ups.status.commfault: internal key of commfault_info (row 150),
//   no change in ups.status.
// Unchanged: ups.realpower.nominal from the generic mapping of Flow.ConfigActivePower (ADR
// 0011 rule 2), ups.timer.shutdown from the Outlet only (row 127), not from report 7.
static const char* const kEcoflowSnapshotAfter =
    "battery.capacity.nominal=100\n"
    "battery.capacity=98\n"
    "battery.charge.low=10\n"
    "battery.charge.warning=5\n"
    "battery.charge=87\n"
    "battery.runtime.low=120\n"
    "battery.runtime=115680\n"
    "battery.type=LiFePO4\n"
    "battery.voltage.nominal=52.0\n"
    "battery.voltage=51.0\n"
    "experimental.ups.powersummary.ioeminformation=OEM\n"
    "experimental.ups.powersummary.presentstatus.shutdownrequested=0\n"
    "experimental.ups.powersummary.presentstatus.voltagenotregulated=0\n"
    "experimental.ups.powersummary.rechargeable=1\n"
    "ups.beeper.status=enabled\n"
    "ups.mfr.date=2019/03/15\n"
    "ups.mfr=EcoFlow\n"
    "ups.power.nominal=1024\n"
    "ups.realpower.nominal=1024\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.commfault=0\n"
    "ups.status.depleted=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.timer.reboot=30\n"
    "ups.timer.shutdown=60\n"
    "ups.status=OL CHRG\n";
// The poll is the generic one, unchanged
static const char* const kEcoflowPollAfter = kEcoflowPollBefore;

void test_ecoflow_snapshot_baseline(void) {
    ecoflowLayout();
    mockHost._iManufacturer = 1;
    decodeAll(ecoflowReports());
    stringDescriptor(1, "EcoFlow");
    stringDescriptor(2, "LiFePO4");
    stringDescriptor(3, "OEM");
    TEST_ASSERT_EQUAL_STRING(kEcoflowSnapshotAfter, snapshotOf(ups_data).c_str());
    TEST_ASSERT_EQUAL_STRING(kEcoflowPollAfter, pollListsOf().c_str());
    TEST_ASSERT_TRUE(strcmp(kEcoflowSnapshotBefore, kEcoflowSnapshotAfter) != 0);
}

// Every BOOL row of ecoflow_hid2nut set at once, then cleared: the tokens of usbhid-ups
void test_ecoflow_status_rows(void) {
    ecoflowLayout();
    // Every flag but ACPresent: on battery. As ups_status_set(): depleted hides DISCHRG,
    // fullycharged hides CHRG, lowbatt / timelimitexp / shutdownimm give LB, commfault ALARM
    decode(3, { 1, 0xFE, 0x0F });
    TEST_ASSERT_EQUAL_STRING("ALARM OB LB OVER RB", status().c_str());
    TEST_ASSERT_EQUAL_STRING("Replace battery! Shutdown imminent! Internal UPS fault!", ups_data.get("ups.alarm").c_str());

    decode(3, { 1, 0x03, 0x00 }); // ACPresent and BatteryPresent only
    TEST_ASSERT_EQUAL_STRING("OL", status().c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.alarm"));
}

// The experimental.* rows of ecoflow-hid.c:136, 170, 172, 173 are published as upstream
void test_ecoflow_experimental_rows(void) {
    ecoflowLayout();
    decode(3, { 1, 0x00, 0x30 }); // ShutdownRequested, VoltageNotRegulated
    decode(3, { 12, 1 });
    decode(3, { 10, 2, 3 });
    stringDescriptor(3, "OEM");
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("experimental.ups.powersummary.presentstatus.shutdownrequested").c_str());
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("experimental.ups.powersummary.presentstatus.voltagenotregulated").c_str());
    TEST_ASSERT_EQUAL_STRING("1", ups_data.get("experimental.ups.powersummary.rechargeable").c_str());
    TEST_ASSERT_EQUAL_STRING("OEM", ups_data.get("experimental.ups.powersummary.ioeminformation").c_str());
}

#ifdef PIO_UNIT_TESTING
static void runAll() {
    RUN_TEST(test_ecoflow_runtime_minutes_to_seconds);
    RUN_TEST(test_ecoflow_capacity_and_warning_mappings);
    RUN_TEST(test_ecoflow_config_active_power_keeps_generic_realpower_nominal);
    RUN_TEST(test_ecoflow_config_active_power_needs_flow_index);
    RUN_TEST(test_ecoflow_outlet_timers);
    RUN_TEST(test_ecoflow_chemistry_string_descriptor);
    RUN_TEST(test_ecoflow_present_status);
    RUN_TEST(test_ecoflow_runtime_negative_keeps_last_value);
    RUN_TEST(test_ecoflow_runtime_negative_first_publishes_nothing);
    RUN_TEST(test_ecoflow_mfr_falls_back_when_no_manufacturer_string);
    RUN_TEST(test_ecoflow_mfr_falls_back_when_manufacturer_string_empty);
    RUN_TEST(test_ecoflow_mfr_keeps_reported_vendor);
    RUN_TEST(test_ecoflow_beeper_not_controllable);
    RUN_TEST(test_ecoflow_beeper_status);
    RUN_TEST(test_ecoflow_shutdown_timer_outlet_wins_when_outlet_first);
    RUN_TEST(test_ecoflow_shutdown_timer_outlet_wins_when_powersummary_first);
    RUN_TEST(test_ecoflow_shutdown_timer_absent_without_outlet);
    RUN_TEST(test_ecoflow_no_shutdown_commands);
    RUN_TEST(test_ecoflow_command_table_has_no_commands);
    RUN_TEST(test_ecoflow_battery_voltage_nominal_one_decimal);
    RUN_TEST(test_ecoflow_snapshot_baseline);
    RUN_TEST(test_ecoflow_status_rows);
    RUN_TEST(test_ecoflow_experimental_rows);
}

#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    runAll();
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    runAll();
    UNITY_END();
}
void loop() {}
#endif
#endif
