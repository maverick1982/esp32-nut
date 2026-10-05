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
#include "IUSBHostUPS.h"
#include "Quirks.h"

// Review S3 / A6 / M4: one poll state machine in GenericDriver, policies in the drivers

class MockPollHost : public IUSBHostUPS {
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
    UPSDataLock getUPSData() const override { static UPSData d; return UPSDataLock(d, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return _quirks; }
    bool isPollingPaused() const override { return _paused; }
    bool requestReport(uint8_t id, uint8_t type, uint16_t length) override {
        _reports.push_back({id, type});
        _lengths.push_back(length);
        for (auto& a : _answers) {
            if (a.first == id && _driver && _data) {
                _driver->decodeReport(this, id, type, a.second.data(), a.second.size(), *_data);
            }
        }
        if (_answer_all && _driver && _data) {
            std::vector<uint8_t> buf(_hid_parser.getExpectedLength(id, type), 0);
            if (buf.empty()) buf.resize(2, 0);
            buf[0] = id;
            _driver->decodeReport(this, id, type, buf.data(), buf.size(), *_data);
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

void test_no_get_report_quirk_skips_reports(void) {
    standardDevice();
    host._quirks = QUIRK_NO_GET_REPORT;
    GenericDriver drv;
    drv.setup();
    runLoop(drv, 1000, 40000);
    TEST_ASSERT_EQUAL(0, host._reports.size());
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

void test_cyberpower_policy(void) {
    standardDevice();
    host.addUsage(0x00840040, 4, 3, "UPS.Flow.ConfigFrequency");  // hangs some firmwares
    host.addUsage(0x00840040, 130, 3, "UPS.Vendor");              // vendor defined
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

void test_eaton_skips_reports_254_255(void) {
    standardDevice();
    host.addUsage(0x00840040, 254, 3, "UPS.X");
    host.addUsage(0x00840040, 255, 3, "UPS.Y");
    EatonDriver drv;
    drv.setup();
    runLoop(drv, 1000, 2000);
    TEST_ASSERT_EQUAL(3, host._reports.size());
    for (const auto& r : host._reports) {
        TEST_ASSERT_TRUE(r.first != 254 && r.first != 255);
    }
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
    // v1.6.1: ~336/min. fix-issue-60-v1: 102/min (Tim's log). Static reports read once:
    // 2 full polls of 13 dynamic reports + 4 quick polls of 6 (15, 19, 34, 35, 80, 122)
    TEST_ASSERT_EQUAL(50, bx750miRequestsPerMinute("Back-UPS BX750MI"));
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
    RUN_TEST(test_no_get_report_quirk_skips_reports);
    RUN_TEST(test_report_id_zero_polled);
    RUN_TEST(test_cyberpower_policy);
    RUN_TEST(test_eaton_skips_reports_254_255);
    RUN_TEST(test_string_index_found_in_reports_requested_same_cycle);
    RUN_TEST(test_report_fresh_from_input_is_skipped);
    RUN_TEST(test_report_with_old_input_is_requested);
    RUN_TEST(test_feature_with_values_missing_from_input_is_requested);
    RUN_TEST(test_all_reports_fresh_sends_nothing);
    RUN_TEST(test_apc_back_ups_bx_quick_poll_10s);
    RUN_TEST(test_apc_other_models_keep_2s_quick_poll);
    RUN_TEST(test_apc_back_ups_bx_requests_at_least_8_bytes);
    RUN_TEST(test_apc_other_models_request_declared_length);
    RUN_TEST(test_dynamic_usages);
    RUN_TEST(test_apc_back_ups_bx_reads_static_reports_once);
    RUN_TEST(test_apc_other_models_read_static_reports_every_full_poll);
    RUN_TEST(test_apc_back_ups_bx750mi_requests_per_minute);
    RUN_TEST(test_initial_poll_done_after_first_full_cycle);
    return UNITY_END();
}
