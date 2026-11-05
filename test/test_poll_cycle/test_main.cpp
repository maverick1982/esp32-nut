#include <unity.h>
#include <string.h>
#include "GenericDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "APCDriver.h"
#include <map>
#include <fstream>
#include <sstream>
#include <ArduinoJson.h>
#include "IHidTransport.h"
#include "UPSData.h"
#include "Quirks.h"

// Review S3 / A6 / M4: one poll state machine in GenericDriver, policies in the drivers

class MockPollHost : public IHidTransport {
public:
    std::vector<HIDUsageDef> _usages;
    std::vector<std::pair<uint8_t, uint8_t>> _reports; // (id, type)
    std::vector<uint16_t> _lengths; // wLength of each GET_REPORT
    std::vector<uint8_t> _strings;
    uint32_t _quirks = 0;
    // Optional answers to GET_REPORT, decoded by _driver into *_data
    IUPSDriver* _driver = nullptr;
    UPSData* _data = nullptr;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> _answers;
    bool _paused = false;
    HIDParser _hid_parser;
    std::map<uint8_t, uint32_t> _input_at; // report ID -> when its INPUT report arrived
    bool _answer_all = false; // every GET_REPORT is answered with zeros of the declared length

    void lock() const override {}
    void unlock() const override {}
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    String _beeper_path;
    String getActiveBeeperPath() const override { return _beeper_path; }
    uint32_t getQuirks() const override { return _quirks; }
    bool isPollingPaused() const override { return _paused; }
    bool requestReport(uint8_t id, uint8_t type, uint16_t length) override {
        _reports.push_back({id, type});
        _lengths.push_back(length);
        for (auto& a : _answers) {
            if (a.first == id && _driver && _data) {
                // As USBHostUPS: the mappings, then the NUT table (US-066, US-073)
                _driver->decodeReport(this, id, type, a.second.data(), a.second.size(), *_data);
                _driver->postDecode(this, id, type, a.second.data(), a.second.size(), *_data);
            }
        }
        if (_answer_all && _driver && _data) {
            std::vector<uint8_t> buf(_hid_parser.getExpectedLength(id, type), 0);
            if (buf.empty()) buf.resize(2, 0);
            buf[0] = id;
            _driver->decodeReport(this, id, type, buf.data(), buf.size(), *_data);
            _driver->postDecode(this, id, type, buf.data(), buf.size(), *_data);
        }
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _strings.push_back(index);
        return true;
    }
    uint32_t inputReportAgeMs(uint8_t id, uint32_t now) const override {
        auto it = _input_at.find(id);
        return it == _input_at.end() ? UINT32_MAX : now - it->second;
    }

    void addUsage(uint32_t usage, uint8_t id, uint8_t type, const char* path) {
        HIDUsageDef u;
        u.usage = usage;
        u.report_id = id;
        u.report_type = type;
        u.bit_size = 8;
        u.found = true;
        strncpy(u.path, path, sizeof(u.path) - 1);
        _usages.push_back(u);
    }
};

static MockPollHost host;
static UPSData data;

// Report 1: status (FEATURE). Report 2: nominal values (FEATURE). Report 3: INPUT only.
// Report 1 also exists as INPUT (same values as the FEATURE report).
static void standardDevice() {
    host.addUsage(0x008500D0, 1, 3, "UPS.PowerSummary.PresentStatus.ACPresent");
    host.addUsage(0x00850066, 1, 3, "UPS.PowerSummary.RemainingCapacity");
    host.addUsage(0x008500D0, 1, 1, "UPS.PowerSummary.PresentStatus.ACPresent");
    host.addUsage(0x00840040, 2, 3, "UPS.Flow.ConfigVoltage");
    host.addUsage(0x00840030, 3, 1, "UPS.Input.Voltage");
}

// Runs loop() every 60 ms from t0 to t1
static void runLoop(IUPSDriver& drv, uint32_t t0, uint32_t t1) {
    for (uint32_t t = t0; t <= t1; t += 60) drv.loop(&host, data, t);
}

void setUp(void) {
    host = MockPollHost();
    data = UPSData();
}

void tearDown(void) {}

void test_first_cycle_is_full(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    // Every report once, INPUT 1 skipped for its FEATURE twin, INPUT 3 kept
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[0].second);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[2].first);
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[2].second);
}

void test_quick_poll_reads_status_reports_only(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    host._reports.clear();

    // 2 s later: quick poll, only report 1
    runLoop(drv, 3000, 3500);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);

    // 30 s after the first cycle: full poll again
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

void test_requests_are_spaced(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    drv.loop(&host, data, 1000); // first request right away
    drv.loop(&host, data, 1010);
    drv.loop(&host, data, 1049);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    drv.loop(&host, data, 1050);
    TEST_ASSERT_EQUAL(2, host._reports.size());
}

void test_paused_polling_resumes_where_it_stopped(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    drv.loop(&host, data, 1000);
    host._paused = true;
    runLoop(drv, 1060, 5000);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    host._paused = false;
    runLoop(drv, 5060, 5120); // the rest of the paused cycle
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
    // Then the quick poll that fell due meanwhile
    runLoop(drv, 5180, 5180);
    TEST_ASSERT_EQUAL(4, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[3].first);
}

void test_missing_strings_first_in_full_cycle(void) {
    standardDevice();
    host._iManufacturer = 1;
    host._iProduct = 2;
    data.set("ups.model", "known");
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(1, host._strings.size()); // model already known
    TEST_ASSERT_EQUAL_UINT8(1, host._strings[0]);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

// US-108: without the interrupt pipe (APC 5G, disable_interrupt_pipe of apc-hid) every value is
// read with a FEATURE GET_REPORT, and a report that is only INPUT is never requested
void test_no_interrupt_pipe_quirk_reads_feature_reports_only(void) {
    standardDevice();
    host._quirks = QUIRK_NO_INTERRUPT_PIPE;
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[0].second);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[1].second);

    // Without the quirk the INPUT report 3 is read too (test_first_cycle_is_full)
    setUp();
    standardDevice();
    GenericDriver plain;
    plain.setup();
    runLoop(plain, 1000, 1500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[2].first);
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[2].second);
}

void test_report_id_zero_polled(void) {
    // Review M4: a device without report IDs was never polled
    host.addUsage(0x00850066, 0, 3, "UPS.PowerSummary.RemainingCapacity");
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1200);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(0, host._reports[0].first);
}

// standardDevice() with report 2 on a path the driver's table reads: UPS.Flow.ConfigVoltage is
// no row of cps-hid nor of mge-hid, and the full poll asks only for the reports something reads
// (US-120)
static void standardDeviceReport2(const char* path) {
    standardDevice();
    HIDUsageDef& u = host._usages[3];
    memset(u.path, 0, sizeof(u.path));
    strncpy(u.path, path, sizeof(u.path) - 1);
}

void test_cyberpower_policy(void) {
    standardDeviceReport2("UPS.Input.ConfigVoltage");
    // Fields of cps-hid rows: only the exclusions keep these reports out
    host.addUsage(0x00840040, 4, 3, "UPS.Output.ConfigVoltage");       // vendor layout: report 4 hung the BR700ELCD (#40)
    host.addUsage(0x00840040, 130, 3, "UPS.PowerSummary.ConfigVoltage"); // vendor defined
    CyberPowerDriver drv;
    drv.setup();
    runLoop(drv, 1000, 2000);
    // FEATURE 1 and 2 only: no INPUT on EP0, no report 4 or >= 130
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);

    // No quick poll: nothing until the next full poll 30 s later
    host._reports.clear();
    runLoop(drv, 2060, 30900);
    TEST_ASSERT_EQUAL(0, host._reports.size());
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
}

// Layout without vendor reports (CP1350C, CP1500, CP1600, CP2000): report 4 carries
// iOEMInformation (battery.mfr.date, cps-hid.c:368) and is read; report 6 never (US-111)
void test_cyberpower_standard_layout_reads_report_4(void) {
    standardDeviceReport2("UPS.Input.ConfigVoltage");
    host.addUsage(0x0085008F, 4, 3, "UPS.PowerSummary.iOEMInformation");
    host.addUsage(0x00840040, 6, 3, "UPS.Output.ConfigVoltage"); // a cps-hid row: excluded anyway
    CyberPowerDriver drv;
    drv.setup();
    runLoop(drv, 1000, 2000);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
    TEST_ASSERT_EQUAL_UINT8(4, host._reports[2].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[2].second);
}

void test_eaton_skips_reports_254_255(void) {
    standardDeviceReport2("UPS.BatterySystem.Voltage");
    // Fields of mge-hid rows: only the exclusion keeps these reports out
    host.addUsage(0x00840035, 254, 3, "UPS.PowerSummary.PercentLoad");
    host.addUsage(0x00840036, 255, 3, "UPS.BatterySystem.Battery.Temperature");
    EatonDriver drv;
    drv.setup();
    runLoop(drv, 1000, 2000);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    for (const auto& r : host._reports) {
        TEST_ASSERT_TRUE(r.first != 254 && r.first != 255);
    }
}

// mge-hid.c:2040-2052, 2116-2117: the ABM rows are HU_FLAG_QUICK_POLL status rows, so their
// report (Charger.Mode, FEATURE 0x26 on the Eaton 5P) is in the quick poll, as the quick
// update upstream; a report of nominal values is not (US-073)
void test_eaton_abm_report_in_quick_poll(void) {
    standardDevice();
    host.addUsage(0xFFFF0094, 0x26, 3, "UPS.BatterySystem.Charger.0xFFFF0094");
    EatonDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    host._reports.clear();

    runLoop(drv, 3000, 3500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(0x26, host._reports[1].first);

    // The plain GenericDriver has no table: report 0x26 stays in the full poll
    host._reports.clear();
    GenericDriver plain;
    plain.setup();
    runLoop(plain, 1000, 1500);
    host._reports.clear();
    runLoop(plain, 3000, 3500);
    TEST_ASSERT_EQUAL(1, host._reports.size());
}

void test_string_index_found_in_reports_requested_same_cycle(void) {
    // Eaton: the battery chemistry string index is a FEATURE value (iDeviceChemistry).
    // It used to be requested only at the next full poll, 30 s later.
    standardDevice();
    host.addUsage(0x00850089, 2, 3, "UPS.PowerSummary.iDeviceChemistry");
    host._usages.back().bit_offset = 8; // after ConfigVoltage in report 2
    EatonDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    host._answers.push_back({2, {0x02, 0xE6, 0x05}}); // report 2: ConfigVoltage 230, chemistry index 5

    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL(1, host._strings.size());
    TEST_ASSERT_EQUAL_UINT8(5, host._strings[0]);

    // Not asked twice in the same cycle, and the quick poll asks no strings
    runLoop(drv, 1560, 3500);
    TEST_ASSERT_EQUAL(1, host._strings.size());
}

// Issue #60, like libhid.c refresh_report_buffer(): the UPS just sent these values as an
// INPUT report, so they are not requested again.
// Back-UPS BX-like: 12 and 19 exist as FEATURE and INPUT with the same usages, 35 is
// FEATURE only.
static void bxLikeDevice() {
    host.addUsage(0x00850066, 12, 3, "UPS.PowerSummary.RemainingCapacity");
    host.addUsage(0x00850066, 12, 1, "UPS.PowerSummary.RemainingCapacity");
    host.addUsage(0x008500D0, 19, 3, "UPS.PowerSummary.ACPresent");
    host.addUsage(0x008500D0, 19, 1, "UPS.PowerSummary.ACPresent");
    host.addUsage(0x00850068, 35, 3, "UPS.PowerSummary.RunTimeToEmpty");
}

void test_report_fresh_from_input_is_skipped(void) {
    bxLikeDevice();
    host._input_at[12] = 900;
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(19, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(35, host._reports[1].first);
    // The skipped report took no step: the next request went out right away
}

void test_report_with_old_input_is_requested(void) {
    bxLikeDevice();
    host._input_at[12] = 900;
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 2900, 3400); // INPUT 12 is exactly 2 s old
    TEST_ASSERT_EQUAL(3, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(12, host._reports[0].first);
}

// Report 1 FEATURE also carries RemainingCapacity, which its INPUT twin lacks
void test_feature_with_values_missing_from_input_is_requested(void) {
    standardDevice();
    host._input_at[1] = 990;
    host._input_at[3] = 990;
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    // FEATURE 1 and 2 are requested, INPUT 3 is fresh
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(1, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(2, host._reports[1].first);
}

void test_all_reports_fresh_sends_nothing(void) {
    host.addUsage(0x00850066, 12, 3, "UPS.PowerSummary.RemainingCapacity");
    host.addUsage(0x00850066, 12, 1, "UPS.PowerSummary.RemainingCapacity");
    GenericDriver drv;
    drv.setup();
    for (uint32_t t = 1000; t <= 40000; t += 60) {
        host._input_at[12] = t; // a report every loop
        drv.loop(&host, data, t);
    }
    TEST_ASSERT_EQUAL(0, host._reports.size());
}

void test_apc_back_ups_bx_quick_poll_10s(void) {
    bxLikeDevice();
    data.set("ups.model", "Back-UPS BX750MI");
    APCDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    host._reports.clear();
    // No quick poll 2 s later
    runLoop(drv, 3000, 10900);
    TEST_ASSERT_EQUAL(0, host._reports.size());
    // 10 s after the full poll. INPUT 12 arrived 5 s ago: still fresh for the BX
    host._input_at[12] = 6000;
    runLoop(drv, 11000, 11500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(19, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(35, host._reports[1].first);
}

void test_apc_other_models_keep_2s_quick_poll(void) {
    bxLikeDevice();
    data.set("ups.model", "Back-UPS CS 650");
    APCDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
    host._reports.clear();
    runLoop(drv, 3000, 3500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

// usbhid-ups "maxreport" on the Back-UPS BX: GET_REPORT asks at least 8 bytes.
// FEATURE 12 is declared 2 bytes long (ID + 8 bits); 19 and 35 are not in the
// descriptor, so their length falls back to 64 and is kept.
static const uint8_t REPORT_12_DESC[] = {
    0x05, 0x84, 0x09, 0x04, 0xA1, 0x01,
    0x85, 0x0C, 0x05, 0x85, 0x09, 0x66, 0x75, 0x08, 0x95, 0x01, 0x26, 0xFF, 0x00, 0xB1, 0x02,
    0xC0
};

static void runFirstCycle(const char* model) {
    bxLikeDevice();
    host._hid_parser.parseReportDescriptor(REPORT_12_DESC, sizeof(REPORT_12_DESC));
    data.set("ups.model", model);
    APCDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1500);
}

void test_apc_back_ups_bx_requests_at_least_8_bytes(void) {
    runFirstCycle("Back-UPS BX750MI");
    TEST_ASSERT_EQUAL(3, host._lengths.size());
    TEST_ASSERT_EQUAL_UINT8(12, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT16(8, host._lengths[0]);
    TEST_ASSERT_EQUAL_UINT16(64, host._lengths[1]);
}

void test_apc_other_models_request_declared_length(void) {
    runFirstCycle("Back-UPS CS 650");
    TEST_ASSERT_EQUAL(3, host._lengths.size());
    TEST_ASSERT_EQUAL_UINT16(2, host._lengths[0]);
}

// US-067: the host asks the same wLength for the SET VAR read-modify-write and read back,
// through the public IUPSDriver::requestLength()
static uint16_t hostRequestLength(const char* model, uint16_t expected) {
    host = MockPollHost();
    data = UPSData();
    bxLikeDevice();
    data.set("ups.model", model);
    APCDriver drv;
    drv.setup();
    runLoop(drv, 1000, 1000); // onLoop() reads the model
    const IUPSDriver& d = drv;
    return d.requestLength(3, 12, expected);
}

void test_apc_back_ups_bx_request_length_public(void) {
    TEST_ASSERT_EQUAL_UINT16(APCDriver::BX_MIN_REQUEST_LENGTH, hostRequestLength("Back-UPS BX750MI", 2));
    TEST_ASSERT_EQUAL_UINT16(64, hostRequestLength("Back-UPS BX750MI", 64)); // never less than declared
    TEST_ASSERT_EQUAL_UINT16(2, hostRequestLength("Back-UPS CS 650", 2));
    GenericDriver gen;
    const IUPSDriver& d = gen;
    TEST_ASSERT_EQUAL_UINT16(2, d.requestLength(3, 12, 2));
}

// Issue #60, like HU_FLAG_STATIC in usbhid-ups: on the Back-UPS BX a report with static
// values only is read once, then left out of the full poll
static HIDUsageDef usageDef(uint32_t usage, const char* path) {
    HIDUsageDef u;
    u.usage = usage;
    strncpy(u.path, path, sizeof(u.path) - 1);
    return u;
}

void test_dynamic_usages(void) {
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x00840030, "UPS.Input.Voltage")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x00840035, "UPS.PowerConverter.PercentLoad")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x00840057, "UPS.PowerSummary.DelayBeforeShutdown")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x00840058, "UPS.Battery.Test")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x0084005a, "UPS.PowerSummary.AudibleAlarmControl")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x00850068, "UPS.Battery.RunTimeToEmpty")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x008500d0, "UPS.PowerSummary.ACPresent")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0x00840044, "UPS.PresentStatus.Charging")));
    TEST_ASSERT_TRUE(GenericDriver::isDynamicUsage(usageDef(0xff860016, "UPS.APCGeneralCollection.APCDelayBeforeShutdown")));

    TEST_ASSERT_FALSE(GenericDriver::isDynamicUsage(usageDef(0x00840040, "UPS.Battery.ConfigVoltage")));
    TEST_ASSERT_FALSE(GenericDriver::isDynamicUsage(usageDef(0x00840053, "UPS.Input.LowVoltageTransfer")));
    TEST_ASSERT_FALSE(GenericDriver::isDynamicUsage(usageDef(0x00850085, "UPS.Battery.ManufacturerDate")));
    TEST_ASSERT_FALSE(GenericDriver::isDynamicUsage(usageDef(0x00850029, "UPS.PowerSummary.RemainingCapacityLimit")));
    TEST_ASSERT_FALSE(GenericDriver::isDynamicUsage(usageDef(0x008400fe, "UPS.iProduct")));
    TEST_ASSERT_FALSE(GenericDriver::isDynamicUsage(usageDef(0xff860042, "UPS.0xFF860042")));
}

// 37 static, 38 dynamic, 50 static but never answered
static void bxStaticDevice(const char* model) {
    host.addUsage(0x00840040, 37, 3, "UPS.Battery.ConfigVoltage");
    host.addUsage(0x00840030, 38, 3, "UPS.Battery.Voltage");
    host.addUsage(0x00840053, 50, 3, "UPS.Input.LowVoltageTransfer");
    host._answers.push_back({37, {37, 0xB0, 0x04}});
    host._answers.push_back({38, {38, 0x50, 0x05}});
    data.set("ups.model", model);
}

void test_apc_back_ups_bx_reads_static_reports_once(void) {
    bxStaticDevice("Back-UPS BX750MI");
    APCDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    // 37 was decoded: left out. 50 never answered: asked again
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(38, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(50, host._reports[1].first);
    // A new enumeration (setup()) reads it again
    drv.setup();
    host._reports.clear();
    runLoop(drv, 32000, 32500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

// US-067: a written report whose read back failed is read again by the next full poll,
// although the BX reads static reports once
void test_apc_back_ups_bx_invalidated_static_report_requested_again(void) {
    bxStaticDevice("Back-UPS BX750MI");
    APCDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL(2, host._reports.size()); // 37 known: left out

    IUPSDriver& d = drv;
    // INPUT 37 is another report: FEATURE 37 stays known. From here on 50 is not asked either:
    // its only row was never answered in the first full poll, finishInit() dropped it (DEAD),
    // and nothing else reads the report (US-120)
    d.invalidateReport(1, 37);
    host._reports.clear();
    runLoop(drv, 61000, 61500);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(38, host._reports[0].first);

    d.invalidateReport(3, 37);
    host._reports.clear();
    runLoop(drv, 91000, 91500);
    TEST_ASSERT_EQUAL(2, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(37, host._reports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._reports[0].second);
    TEST_ASSERT_EQUAL_UINT8(38, host._reports[1].first);

    // Decoded again: left out from then on
    host._reports.clear();
    runLoop(drv, 121000, 121500);
    TEST_ASSERT_EQUAL(1, host._reports.size());
    TEST_ASSERT_EQUAL_UINT8(38, host._reports[0].first);
}

void test_apc_other_models_read_static_reports_every_full_poll(void) {
    bxStaticDevice("Back-UPS CS 650");
    APCDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL(3, host._reports.size());
}

// The real BX750MI descriptor of issue #60. The UPS streams INPUT 12, 20 and 22.
static void loadFixtureDescriptor(const char* path) {
    std::ifstream f(path);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), path);
    std::stringstream ss;
    ss << f.rdbuf();
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, ss.str()));
    std::vector<uint8_t> desc;
    for (JsonVariant v : doc["report_descriptor_hex"].as<JsonArray>()) {
        desc.push_back((uint8_t)strtol(v.as<std::string>().c_str(), nullptr, 16));
    }
    host._hid_parser.parseReportDescriptor(desc.data(), desc.size());
    host._usages = host._hid_parser.getUsages();
}

static size_t bx750miRequestsPerMinute(const char* model) {
    loadFixtureDescriptor("test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json");
    data.set("ups.model", model);
    APCDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    host._answer_all = true;
    size_t at_60s = 0;
    // The second minute, once the static reports are known
    for (uint32_t t = 1000; t < 121000; t += 60) {
        if (t % 960 == 40) { host._input_at[12] = t; host._input_at[20] = t; host._input_at[22] = t; }
        if (t >= 61000 && at_60s == 0) at_60s = host._reports.size();
        drv.loop(&host, data, t);
    }
    return host._reports.size() - at_60s;
}

void test_apc_back_ups_bx750mi_requests_per_minute(void) {
    // v1.6.1: ~336/min. fix-issue-60-v1: 102/min (Tim's log). Static reports read once: 2 full
    // polls of 13 dynamic reports + 4 quick polls of 6 (15, 19, 34, 35, 80, 122) = 50.
    // US-120: the full poll asks only for the reports a row reads, as hid_ups_walk(): 2 full
    // polls of 6 (19, 21, 35, 38, 49, 80) + the same quick polls = 36. Left out: 14, 34, 120 and
    // 122 (no row), 15 (battery.runtime DEAD: UPS.Battery.RunTimeToEmpty comes first), 24 and 33
    // (their only value rows fail on the zeros this mock answers, the rest are command rows)
    TEST_ASSERT_EQUAL(36, bx750miRequestsPerMinute("Back-UPS BX750MI"));
}

// US-072: a small table. Report 1: status, dynamic rows (quick poll). Report 2: a STATIC row.
// Report 3: a SEMI_STATIC row. Report 4: mixed, a STATIC and a dynamic row. Report 5: no row, never
// requested (US-120: nothing reads it, as hid_ups_walk()).
// Report 6: a STATIC row, a row the INIT rule drops (DEAD: test.dup is taken by report 1) and a
// command row, both without flags.
static const NutTable::HidInfo kSemiStaticTable[] = {
    { "test.charge", 0, 0, "UPS.T.Charge", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.dup", 0, 0, "UPS.T.Dup1", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.nominal", 0, 0, "UPS.T.Nominal", "%.0f", NutTable::HU_FLAG_STATIC, NutTable::Conv::Number, nullptr, 0 },
    { "test.limit", NutTable::ST_FLAG_RW, 0, "UPS.T.Limit", "%.0f", NutTable::HU_FLAG_SEMI_STATIC, NutTable::Conv::Number, nullptr, 0 },
    { "test.config", 0, 0, "UPS.T.Config", "%.0f", NutTable::HU_FLAG_STATIC, NutTable::Conv::Number, nullptr, 0 },
    { "test.measure", 0, 0, "UPS.T.Measure", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.serial", 0, 0, "UPS.T.Serial", "%.0f", NutTable::HU_FLAG_STATIC, NutTable::Conv::Number, nullptr, 0 },
    { "test.dup", 0, 0, "UPS.T.Dup6", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.cmd", 0, 0, "UPS.T.Cmd", "1", NutTable::HU_TYPE_CMD, NutTable::Conv::Number, nullptr, 0 },
    // Report 7, only in test_semi_static_deferred_row_read_until_decided: deferred until the
    // conversion inputs are known, as the Eaton nominal values before mge_type
    { "test.vnom", 0, 0, "UPS.T.VNom", "%.0f", NutTable::HU_FLAG_SEMI_STATIC, NutTable::Conv::NominalVoltage, nullptr, 0 },
};

class SemiStaticDriver : public GenericDriver {
public:
    const NutTable::HidInfo* hid2nut(size_t& n) const override {
        n = sizeof(kSemiStaticTable) / sizeof(kSemiStaticTable[0]);
        return kSemiStaticTable;
    }
    using GenericDriver::pollSoon;
    void setInputsKnown(bool known) { _nut_ctx.ready = known; }
};

static void semiStaticDevice() {
    host.addUsage(0x00850066, 1, 3, "UPS.T.Charge"); // RemainingCapacity: a status usage
    host.addUsage(0xFFFF0001, 1, 3, "UPS.T.Dup1");
    host.addUsage(0xFFFF0002, 2, 3, "UPS.T.Nominal");
    host.addUsage(0xFFFF0003, 3, 3, "UPS.T.Limit");
    host.addUsage(0xFFFF0004, 4, 3, "UPS.T.Config");
    host.addUsage(0xFFFF0005, 4, 3, "UPS.T.Measure");
    host.addUsage(0xFFFF0006, 5, 3, "UPS.T.Vendor");
    host.addUsage(0xFFFF0007, 6, 3, "UPS.T.Serial");
    host.addUsage(0xFFFF0008, 6, 3, "UPS.T.Dup6");
    host.addUsage(0xFFFF0009, 6, 3, "UPS.T.Cmd");
    host._answer_all = true;
}

// The report IDs requested since the last clear, in order
static std::string requestedIds() {
    std::string out;
    for (const auto& r : host._reports) {
        if (!out.empty()) out += " ";
        out += std::to_string(r.first);
    }
    return out;
}

// First full poll at 1 s, quick polls until 30 s: the rows are bound and the reports classified
static void runFirstMinuteHalf(SemiStaticDriver& drv) {
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 30000);
}

void test_semi_static_first_full_poll_reads_every_report(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 4 6", requestedIds().c_str());
    TEST_ASSERT_TRUE(data.hasKey("test.limit"));
}

void test_semi_static_reports_left_out_of_later_full_polls(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    // Report 4 (mixed) stays; 6 is semi-static: DEAD and command rows do not count. Report 5 (no
    // row) is never requested (US-120)
    TEST_ASSERT_EQUAL_STRING("1 4", requestedIds().c_str());
    host._reports.clear();
    runLoop(drv, 61000, 61500);
    TEST_ASSERT_EQUAL_STRING("1 4", requestedIds().c_str());
}

// US-108: in "pollonly" (QUIRK_NO_INTERRUPT_PIPE) every full poll reads the semi-static reports
// too, as usbhid-ups.c:2352, 2360 skip the STATIC / SEMI_STATIC rows only with the interrupt pipe
void test_semi_static_no_interrupt_pipe_reads_every_full_poll(void) {
    semiStaticDevice();
    host._quirks = QUIRK_NO_INTERRUPT_PIPE;
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 4 6", requestedIds().c_str());
    host._reports.clear();
    runLoop(drv, 61000, 61500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 4 6", requestedIds().c_str());
}

// SET VAR or INSTCMD: the next cycle is a full poll right away, with the semi-static reports;
// the one after leaves them out again
void test_semi_static_reports_read_again_after_data_change(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    runLoop(drv, 31000, 31500);
    host._reports.clear();
    IUPSDriver& d = drv;
    d.noteDataChanged();
    runLoop(drv, 40000, 40500); // 9 s after the last full poll
    TEST_ASSERT_EQUAL_STRING("1 2 3 4 6", requestedIds().c_str());
    host._reports.clear();
    runLoop(drv, 42000, 69500); // quick polls only
    TEST_ASSERT_EQUAL_STRING("1 1 1 1 1 1 1 1 1 1 1 1 1 1", requestedIds().c_str());
    host._reports.clear();
    runLoop(drv, 70000, 70500); // 30 s after the full poll of the change
    TEST_ASSERT_EQUAL_STRING("1 4", requestedIds().c_str());
}

// A change in the middle of a full poll: the running cycle is not touched, the next one is a
// full poll with every report
void test_semi_static_change_during_full_poll_reads_next_cycle(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    host._reports.clear();
    runLoop(drv, 31000, 31000); // first request of the full poll
    IUPSDriver& d = drv;
    d.noteDataChanged();
    runLoop(drv, 31060, 31500);
    TEST_ASSERT_EQUAL_STRING("1 4 1 2 3 4 6", requestedIds().c_str());
}

// pollSoon() (Powercom) is not filtered: a semi-static report it asks for is read
void test_semi_static_report_requested_by_poll_soon(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    runLoop(drv, 31000, 31500);
    host._reports.clear();
    drv.pollSoon(GenericDriver::PollItem{3, 3, 0});
    runLoop(drv, 31560, 31600);
    TEST_ASSERT_EQUAL_STRING("3", requestedIds().c_str());
}

// The quick poll keeps its reports once the semi-static ones are known
void test_semi_static_quick_poll_unchanged(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    host._reports.clear();
    runLoop(drv, 3000, 3500); // before the classification
    TEST_ASSERT_EQUAL_STRING("1", requestedIds().c_str());
    runLoop(drv, 31000, 31500);
    host._reports.clear();
    runLoop(drv, 33000, 33500); // after
    TEST_ASSERT_EQUAL_STRING("1", requestedIds().c_str());
}

// The active beeper is read outside the rows (decodeBeeperStatus()): its report stays dynamic
void test_semi_static_active_beeper_report_stays_read(void) {
    semiStaticDevice();
    host.addUsage(0x0084005A, 3, 3, "UPS.T.Beeper"); // AudibleAlarmControl in report 3
    host._beeper_path = "UPS.T.Beeper";
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL_STRING("1 3 4", requestedIds().c_str());
}

// A deferred row is not decided by the first full poll: its report stays in the full poll
// until a read decides it, then it is left out (review of US-072: the Eaton nominal values)
void test_semi_static_deferred_row_read_until_decided(void) {
    semiStaticDevice();
    host.addUsage(0xFFFF000A, 7, 3, "UPS.T.VNom");
    host._answers.push_back({7, {7, 230}});
    SemiStaticDriver drv;
    drv.setup();
    drv.setInputsKnown(false);
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 30000);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL_STRING("1 4 7", requestedIds().c_str());
    TEST_ASSERT_FALSE(data.hasKey("test.vnom"));

    drv.setInputsKnown(true);
    host._reports.clear();
    runLoop(drv, 61000, 61500); // reads report 7 again: the row is decided
    TEST_ASSERT_EQUAL_STRING("1 4 7", requestedIds().c_str());
    TEST_ASSERT_EQUAL_STRING("230", data.get("test.vnom").c_str());
    host._reports.clear();
    runLoop(drv, 91000, 91500);
    TEST_ASSERT_EQUAL_STRING("1 4", requestedIds().c_str());
}

// A new enumeration (setup()) reads everything again and classifies again
void test_semi_static_new_enumeration_reads_every_report(void) {
    semiStaticDevice();
    SemiStaticDriver drv;
    runFirstMinuteHalf(drv);
    drv.setup();
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 4 6", requestedIds().c_str());
}

// US-120: the full poll asks only for the FEATURE reports something reads, as hid_ups_walk().
// Report 1: status, a row (quick poll) and the first row of test.dup. Report 2: a command row
// only. Report 3: the second row of test.dup (DEAD once report 1 is read). Report 4: a
// HU_FLAG_ABSENT row only. Report 5: a vendor field without rows (APC report 227, issue #123).
// Report 6: a row bound to its INPUT usage, with the FEATURE twin. Report 7: the active beeper
// outside the rows. Report 8: a status usage without rows (not even a genericHid2Nut() one).
static const NutTable::HidInfo kRowFilterTable[] = {
    { "test.charge", 0, 0, "UPS.R.Charge", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.dup", 0, 0, "UPS.R.Dup1", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.cmd", 0, 0, "UPS.R.Cmd", "1", NutTable::HU_TYPE_CMD, NutTable::Conv::Number, nullptr, 0 },
    { "test.dup", 0, 0, "UPS.R.Dup3", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
    { "test.absent", NutTable::ST_FLAG_RW | NutTable::ST_FLAG_STRING, 20, "UPS.R.Absent", "x", NutTable::HU_FLAG_ABSENT, NutTable::Conv::Number, nullptr, 0 },
    { "test.twin", 0, 0, "UPS.R.Twin", "%.0f", 0, NutTable::Conv::Number, nullptr, 0 },
};

class RowFilterDriver : public GenericDriver {
public:
    const NutTable::HidInfo* hid2nut(size_t& n) const override {
        n = sizeof(kRowFilterTable) / sizeof(kRowFilterTable[0]);
        return kRowFilterTable;
    }
};

static void rowFilterDevice() {
    host.addUsage(0x00850066, 1, 3, "UPS.R.Charge"); // RemainingCapacity: a status usage
    host.addUsage(0xFFFF0001, 1, 3, "UPS.R.Dup1");
    host.addUsage(0xFFFF0002, 2, 3, "UPS.R.Cmd");
    host.addUsage(0xFFFF0003, 3, 3, "UPS.R.Dup3");
    host.addUsage(0xFFFF0004, 4, 3, "UPS.R.Absent");
    host.addUsage(0xFFFF00EF, 5, 3, "UPS.0xFFFF00FF.0xFFFF00EF");
    host.addUsage(0xFFFF0006, 6, 1, "UPS.R.Twin");
    host.addUsage(0xFFFF0006, 6, 3, "UPS.R.Twin");
    host.addUsage(0x0084005A, 7, 3, "UPS.R.Beeper"); // AudibleAlarmControl
    host.addUsage(0xFFFF0008, 8, 3, "UPS.R.PresentStatus.Flag"); // a status path no row reads
    host._beeper_path = "UPS.R.Beeper";
    host._answer_all = true;
}

// INIT: every bound row but HU_FLAG_ABSENT, so the command row and the row that will be DEAD
// too; nothing reads reports 4 and 5
void test_full_poll_first_cycle_reads_reports_with_rows(void) {
    rowFilterDevice();
    RowFilterDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 6 7", requestedIds().c_str());
}

// After finishInit(): the command row and the DEAD row no longer count, the twin and the beeper
// keep their reports; report 5 is never requested
void test_full_poll_after_init_drops_command_and_dead_reports(void) {
    rowFilterDevice();
    RowFilterDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 30000);
    host._reports.clear();
    runLoop(drv, 31000, 31500);
    TEST_ASSERT_EQUAL_STRING("1 6 7", requestedIds().c_str());
    host._reports.clear();
    runLoop(drv, 61000, 61500);
    TEST_ASSERT_EQUAL_STRING("1 6 7", requestedIds().c_str());
    for (const auto& r : host._reports) TEST_ASSERT_EQUAL_UINT8(3, r.second); // INPUT 6: never by GET_REPORT
}

// The quick poll is not filtered: report 8 (a status usage without rows) stays in it
void test_quick_poll_unchanged_by_row_filter(void) {
    rowFilterDevice();
    RowFilterDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 1500);
    host._reports.clear();
    runLoop(drv, 3000, 3500);
    TEST_ASSERT_EQUAL_STRING("1 8", requestedIds().c_str());
    runLoop(drv, 31000, 31500);
    host._reports.clear();
    runLoop(drv, 33000, 33500);
    TEST_ASSERT_EQUAL_STRING("1 8", requestedIds().c_str());
}

// A new enumeration binds the rows again: the first full poll is an INIT again
void test_full_poll_new_enumeration_reads_init_reports_again(void) {
    rowFilterDevice();
    RowFilterDriver drv;
    drv.setup();
    host._driver = &drv;
    host._data = &data;
    runLoop(drv, 1000, 31500);
    drv.setup();
    host._reports.clear();
    runLoop(drv, 32000, 32500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 6 7", requestedIds().c_str());
}

// EatonDriver::decodeReport() reads iModel outside the rows (mge_type): its report stays in the
// full poll with the Eaton driver, and is not requested by GenericDriver, which does not read it
void test_eaton_full_poll_keeps_imodel_report(void) {
    standardDeviceReport2("UPS.BatterySystem.Voltage");
    host.addUsage(0xFFFF00F0, 9, 3, "UPS.PowerSummary.0xFFFF00F0");
    host._answer_all = true;
    EatonDriver eaton;
    eaton.setup();
    host._driver = &eaton;
    host._data = &data;
    runLoop(eaton, 1000, 30000);
    host._reports.clear();
    runLoop(eaton, 31000, 31500);
    TEST_ASSERT_EQUAL_STRING("1 2 3 9", requestedIds().c_str()); // 3: the INPUT-only report, unchanged

    host = MockPollHost();
    data = UPSData();
    standardDeviceReport2("UPS.BatterySystem.Voltage");
    host.addUsage(0xFFFF00F0, 9, 3, "UPS.PowerSummary.0xFFFF00F0");
    host._answer_all = true;
    GenericDriver generic;
    generic.setup();
    host._driver = &generic;
    host._data = &data;
    runLoop(generic, 1000, 1500);
    for (const auto& r : host._reports) TEST_ASSERT_TRUE(r.first != 9);
}

// Issue #60: the host serves the data only after the first full poll
void test_initial_poll_done_after_first_full_cycle(void) {
    standardDevice();
    GenericDriver drv;
    drv.setup();
    TEST_ASSERT_FALSE(drv.initialPollDone());
    drv.loop(&host, data, 1000); // first request of the full cycle
    TEST_ASSERT_FALSE(drv.initialPollDone());
    runLoop(drv, 1060, 1500);
    TEST_ASSERT_TRUE(drv.initialPollDone());
    // A new enumeration (or an interface recovery) starts over
    drv.setup();
    TEST_ASSERT_FALSE(drv.initialPollDone());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_first_cycle_is_full);
    RUN_TEST(test_quick_poll_reads_status_reports_only);
    RUN_TEST(test_requests_are_spaced);
    RUN_TEST(test_paused_polling_resumes_where_it_stopped);
    RUN_TEST(test_missing_strings_first_in_full_cycle);
    RUN_TEST(test_no_interrupt_pipe_quirk_reads_feature_reports_only);
    RUN_TEST(test_report_id_zero_polled);
    RUN_TEST(test_cyberpower_policy);
    RUN_TEST(test_cyberpower_standard_layout_reads_report_4);
    RUN_TEST(test_eaton_skips_reports_254_255);
    RUN_TEST(test_eaton_abm_report_in_quick_poll);
    RUN_TEST(test_string_index_found_in_reports_requested_same_cycle);
    RUN_TEST(test_report_fresh_from_input_is_skipped);
    RUN_TEST(test_report_with_old_input_is_requested);
    RUN_TEST(test_feature_with_values_missing_from_input_is_requested);
    RUN_TEST(test_all_reports_fresh_sends_nothing);
    RUN_TEST(test_apc_back_ups_bx_quick_poll_10s);
    RUN_TEST(test_apc_other_models_keep_2s_quick_poll);
    RUN_TEST(test_apc_back_ups_bx_requests_at_least_8_bytes);
    RUN_TEST(test_apc_other_models_request_declared_length);
    RUN_TEST(test_apc_back_ups_bx_request_length_public);
    RUN_TEST(test_dynamic_usages);
    RUN_TEST(test_apc_back_ups_bx_reads_static_reports_once);
    RUN_TEST(test_apc_back_ups_bx_invalidated_static_report_requested_again);
    RUN_TEST(test_apc_other_models_read_static_reports_every_full_poll);
    RUN_TEST(test_apc_back_ups_bx750mi_requests_per_minute);
    RUN_TEST(test_initial_poll_done_after_first_full_cycle);
    RUN_TEST(test_semi_static_first_full_poll_reads_every_report);
    RUN_TEST(test_semi_static_reports_left_out_of_later_full_polls);
    RUN_TEST(test_semi_static_no_interrupt_pipe_reads_every_full_poll);
    RUN_TEST(test_semi_static_reports_read_again_after_data_change);
    RUN_TEST(test_semi_static_change_during_full_poll_reads_next_cycle);
    RUN_TEST(test_semi_static_report_requested_by_poll_soon);
    RUN_TEST(test_semi_static_quick_poll_unchanged);
    RUN_TEST(test_semi_static_active_beeper_report_stays_read);
    RUN_TEST(test_semi_static_deferred_row_read_until_decided);
    RUN_TEST(test_semi_static_new_enumeration_reads_every_report);
    RUN_TEST(test_full_poll_first_cycle_reads_reports_with_rows);
    RUN_TEST(test_full_poll_after_init_drops_command_and_dead_reports);
    RUN_TEST(test_quick_poll_unchanged_by_row_filter);
    RUN_TEST(test_full_poll_new_enumeration_reads_init_reports_again);
    RUN_TEST(test_eaton_full_poll_keeps_imodel_report);
    return UNITY_END();
}
