#include <unity.h>
#include "PowercomDriver.h"
#include "IHidTransport.h"
#include "CommandCatalog.h"
#include "HIDParser.h"
#include "UPSData.h"
#include <fstream>
#include <sstream>
#include <ArduinoJson.h>
#include <algorithm>
#include <string>

class MockPowercomHost : public IHidTransport {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;
    std::vector<uint16_t> _requestedLengths;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    String _beeper_path = "UPS.PowerSummary.AudibleAlarmControl";
    String getActiveBeeperPath() const override { return _beeper_path; }
    uint32_t getQuirks() const override { return 0; }
    uint16_t _pid = 0x0004;
    uint16_t getPID() const override { return _pid; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t length) override {
        _requestedReports.push_back({report_id, report_type});
        _requestedLengths.push_back(length);
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

void setUp(void) {}
void tearDown(void) {}

// The SPD-750U descriptor of the issue #21 fixture
static std::vector<uint8_t> spd750uDescriptor() {
    std::ifstream file("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json");
    std::stringstream buffer;
    buffer << file.rdbuf();
    JsonDocument doc;
    deserializeJson(doc, buffer.str());
    std::vector<uint8_t> raw;
    for (JsonVariant v : doc["report_descriptor_hex"].as<JsonArray>()) {
        raw.push_back((uint8_t)strtol(v.as<std::string>().c_str(), nullptr, 16));
    }
    return raw;
}

static void spd750uHost(MockPowercomHost& host) {
    std::vector<uint8_t> raw = spd750uDescriptor();
    host._hid_parser.parseReportDescriptor(raw.data(), raw.size());
    host._usages = host._hid_parser.getUsages();
}

struct SynthReport {
    uint8_t type;
    std::vector<uint8_t> bytes; // report ID first, values little endian
};

// As USBHostUPS: every report goes through decodeReport() and then postDecode(), where the
// NUT table writes its rows (US-077: every Powercom value comes from powercomHid2Nut())
static void decode(PowercomDriver& driver, MockPowercomHost& host, UPSData& data, uint8_t type, const std::vector<uint8_t>& bytes) {
    driver.decodeReport(&host, bytes[0], type, bytes.data(), bytes.size(), data);
    driver.postDecode(&host, bytes[0], type, bytes.data(), bytes.size(), data);
}

static void decodeAll(PowercomDriver& driver, MockPowercomHost& host, UPSData& data, const std::vector<SynthReport>& reports) {
    for (const auto& r : reports) decode(driver, host, data, r.type, r.bytes);
}

// --- report 0xA4 (powercom_hack_voltage): battery.voltage is served on the next report 0x0A

static void hackVoltage(const std::vector<uint8_t>& a4, UPSData& data) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    driver.setup();
    decode(driver, host, data, 3, a4);
    decode(driver, host, data, 3, { 10, 0x0D, 0x00 });
}

void test_powercom_0xa4_valid(void) {
    // " 13.7 2", report ID (0xA4) in byte 0: the text is kept as the UPS sends it
    UPSData data;
    hackVoltage({ 0xA4, ' ', '1', '3', '.', '7', ' ', '2' }, data);
    TEST_ASSERT_EQUAL_STRING("13.7", data.get("battery.voltage").c_str());
}

void test_powercom_0xa4_invalid_no_dot(void) {
    // Not a valid reading: "0.0" until a valid one, as last_valid of powercom_hack_voltage
    UPSData data;
    hackVoltage({ 0xA4, ' ', '1', '3', '7', ' ', '2', ' ' }, data);
    TEST_ASSERT_EQUAL_STRING("0.0", data.get("battery.voltage").c_str());
}

void test_powercom_0xa4_invalid_empty(void) {
    UPSData data;
    hackVoltage({ 0xA4, ' ', ' ', ' ', ' ', ' ', ' ', ' ' }, data);
    TEST_ASSERT_EQUAL_STRING("0.0", data.get("battery.voltage").c_str());
}

void test_powercom_0xa4_invalid_garbage(void) {
    UPSData data;
    hackVoltage({ 0xA4, 'a', 'b', 'c', 'd', 'e', 'f', 'g' }, data);
    TEST_ASSERT_EQUAL_STRING("0.0", data.get("battery.voltage").c_str());
}

void test_powercom_0xa4_keeps_last_valid(void) {
    // powercom_hack_voltage: an invalid reading keeps the last valid one (last_valid)
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decode(driver, host, data, 3, { 0xA4, ' ', '1', '3', '.', '7', ' ', '2' });
    decode(driver, host, data, 3, { 0xA4, 'a', 'b', 'c', 'd', 'e', 'f', 'g' });
    decode(driver, host, data, 3, { 10, 0x0D, 0x00 });
    TEST_ASSERT_EQUAL_STRING("13.7", data.get("battery.voltage").c_str());
}

void test_powercom_beeper_mapping(void) {
    // powercom_beeper_info (powercom-hid.c:601): 1 = enabled, 2 = disabled
    PowercomDriver driver;
    driver.setup();
    UPSData ups_data;
    MockPowercomHost host;
    HIDUsageDef def;
    strcpy(def.path, "UPS.PowerSummary.AudibleAlarmControl");
    def.report_id = 0x1F;
    def.report_type = 3;
    def.bit_size = 8;
    def.bit_offset = 0;
    def.exponent = 0;
    def.unit = 0;
    def.found = true;
    host._usages.push_back(def);

    decode(driver, host, ups_data, 3, { 0x1F, 0x01 });
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());
    decode(driver, host, ups_data, 3, { 0x1F, 0x02 });
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());

    // The beeper is written with the values of the beeper.enable / beeper.disable rows
    // (powercom-hid.c:659-660), 1 and 0, whatever the field size
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 8));
    TEST_ASSERT_EQUAL_UINT8(0, driver.encodeBeeperValue(false, 8));
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 1));
    TEST_ASSERT_EQUAL_UINT8(0, driver.encodeBeeperValue(false, 1));
}

void test_powercom_loop_polling_nut_alignment(void) {
    // A device without descriptor usages: only the keep-alive and report 0xA4
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    host._pid = 0x0004;
    driver.setup();

    // The loop sends the 0x0A keep-alive report (step 1) and no string request
    driver.loop(&host, ups_data, 100);
    TEST_ASSERT_EQUAL_UINT32(1, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x0A, host._requestedReports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[0].second); // Feature report
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[0]);
    TEST_ASSERT_EQUAL_UINT32(0, host._requestedStrings.size());

    // Issue #36: requests of a cycle are 800 ms apart
    driver.loop(&host, ups_data, 899);
    TEST_ASSERT_EQUAL_UINT32(1, host._requestedReports.size());

    // Step 2: legacy battery voltage (0xA4), FEATURE of exactly 8 bytes
    driver.loop(&host, ups_data, 900);
    TEST_ASSERT_EQUAL_UINT32(2, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0xA4, host._requestedReports[1].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[1].second);
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[1]);

    // End of the full poll: 2 s have passed, the quick keep-alive follows
    driver.loop(&host, ups_data, 2100);
    TEST_ASSERT_EQUAL_UINT32(3, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x0A, host._requestedReports[2].first);
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.model"));
}

// powercom_format_mfr / _model / _serial: the USB strings; "PowerCOM" without a manufacturer
// string, and no ups.model / ups.serial without theirs (usbhid-ups.c:2179-2195)
void test_powercom_device_strings_as_upstream(void) {
    PowercomDriver driver;
    UPSData none;
    driver.formatDeviceStrings(none);
    TEST_ASSERT_EQUAL_STRING("PowerCOM", none.get("ups.mfr").c_str());
    TEST_ASSERT_FALSE(none.hasKey("ups.model"));
    TEST_ASSERT_FALSE(none.hasKey("ups.serial"));

    UPSData strings;
    strings.set("ups.mfr", "POWERCOM Co.,LTD");
    strings.set("ups.model", "HID UPS Battery");
    driver.formatDeviceStrings(strings);
    TEST_ASSERT_EQUAL_STRING("POWERCOM Co.,LTD", strings.get("ups.mfr").c_str());
    TEST_ASSERT_EQUAL_STRING("HID UPS Battery", strings.get("ups.model").c_str());
}

void test_powercom_real_descriptor_parsing(void) {
    std::vector<uint8_t> rawDesc = spd750uDescriptor();
    TEST_ASSERT_EQUAL_INT(996, rawDesc.size());

    HIDParser parser;
    parser.parseReportDescriptor(rawDesc.data(), rawDesc.size());
    const auto& usages = parser.getUsages();
    TEST_ASSERT_GREATER_THAN(0, usages.size());

    // Verify key Powercom usages are found in descriptor
    bool found_present_status = false;
    bool found_remaining_capacity = false;
    bool found_run_time_to_empty = false;
    bool found_beeper = false;

    for (const auto& u : usages) {
        if (strstr(u.path, "PresentStatus") != nullptr || u.usage == 0x00840002 || u.usage == 0x00850044) found_present_status = true;
        if (strstr(u.path, "RemainingCapacity") != nullptr || u.usage == 0x00850066) found_remaining_capacity = true;
        if (strstr(u.path, "RunTimeToEmpty") != nullptr || u.usage == 0x00850068) found_run_time_to_empty = true;
        if (strstr(u.path, "AudibleAlarmControl") != nullptr || u.usage == 0x0084005A || u.usage == 0x0085005A) found_beeper = true;
    }

    TEST_ASSERT_TRUE_MESSAGE(found_present_status, "PresentStatus usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_remaining_capacity, "RemainingCapacity usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_run_time_to_empty, "RunTimeToEmpty usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_beeper, "AudibleAlarmControl usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(parser.hasFeatureBeeperControl(), "Powercom SPD-750U descriptor contains Feature report for beeper");
}

// Issue #36 and US-077: on the SPD-750U the first full poll reads the reports of the bound
// rows, 800 ms apart, battery temperature (0x2D) and the beeper (0x13) among them; the
// static reports (iDeviceChemistry 5, dates 13 and 23, nominal voltages 28 and 32) are not
// read again once decoded
void test_powercom_full_poll_spd750u_descriptor(void) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    UPSData ups_data;
    driver.setup();

    const uint8_t expected[] = { 10, 38, 14, 12, 11, 55, 23, 5, 19, 31, 13, 21, 16, 15, 29, 28, 30, 33, 32, 34, 45, 0xA4 };
    const size_t count = sizeof(expected);
    uint32_t t = 100;
    for (size_t i = 0; i < count; i++, t += 800) driver.loop(&host, ups_data, t);
    TEST_ASSERT_EQUAL_UINT32(count, host._requestedReports.size());
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_UINT8(expected[i], host._requestedReports[i].first);
        TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[i].second);
    }
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[0]);  // 0x0A keep-alive
    TEST_ASSERT_EQUAL_UINT16(2, host._requestedLengths[20]); // 0x2D: id + 8 bit
    TEST_ASSERT_EQUAL_UINT16(2, host._requestedLengths[8]);  // 0x13: id + 8 bit
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[14]); // 0x1D input voltage, as in issue #36
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[21]); // 0xA4

    // UPS.Battery.Temperature becomes battery.temperature (powercom-hid.c:677, "%.1f")
    decode(driver, host, ups_data, 3, { 0x2D, 41 });
    TEST_ASSERT_EQUAL_STRING("41.0", ups_data.get("battery.temperature").c_str());
}

// The static reports, once decoded, leave the full poll (pollStaticReportsOnce)
void test_powercom_static_reports_read_once(void) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    UPSData ups_data;
    driver.setup();
    uint32_t t = 100;
    for (int i = 0; i < 22; i++, t += 800) driver.loop(&host, ups_data, t);
    for (uint8_t id : { 5, 13, 23, 28, 32 }) {
        std::vector<uint8_t> bytes = { id, 1, 0 };
        if (id == 5) bytes = { id, 4 };
        decode(driver, host, ups_data, 3, bytes);
    }
    host._requestedReports.clear();
    // The next full poll, 30 s after the first one
    for (t = 30200; t < 30200 + 40 * 800; t += 800) driver.loop(&host, ups_data, t);
    for (const auto& r : host._requestedReports) {
        TEST_ASSERT_TRUE(r.first != 5 && r.first != 13 && r.first != 23 && r.first != 28 && r.first != 32);
    }
    bool dynamic_read = false;
    for (const auto& r : host._requestedReports) dynamic_read = dynamic_read || r.first == 12;
    TEST_ASSERT_TRUE(dynamic_read); // battery.charge.low (591) has no HU_FLAG_STATIC
}

// US-058, user decision of 2026-10-09: DelayBeforeShutdown (minutes/seconds bytes) is the
// timer only (powercom-hid.c:638, powercom_shutdown_info), and there are no load/shutdown
// commands (powercom-hid.c uses its own encodings). ups.delay.* is never read from the
// device: the host sets it (see test_powercom_delay_variables_as_upstream, US-066). The
// vendor PowercomDelayBeforeShutdown is ups.delay.shutdown upstream (powercom-hid.c:727),
// excluded for the same reason: it gives nothing.
void test_powercom_delay_sets_only_timer(void) {
    PowercomDriver driver;
    driver.setup();
    UPSData ups_data;
    MockPowercomHost host;

    HIDUsageDef def;
    strcpy(def.path, "UPS.PowerSummary.DelayBeforeShutdown");
    def.usage = 0x00840057;
    def.report_id = 0x20;
    def.report_type = 3;
    def.bit_size = 16;
    def.bit_offset = 0;
    def.found = true;
    host._usages.push_back(def);
    HIDUsageDef vendor = def;
    strcpy(vendor.path, "0x00020004.0x00020016.0x00020057");
    vendor.usage = 0x00020057;
    vendor.report_id = 0x21;
    host._usages.push_back(vendor);

    decode(driver, host, ups_data, 3, { 0x20, 0x05, 0x01 }); // 1 min 5 s
    TEST_ASSERT_EQUAL_STRING("65", ups_data.get("ups.timer.shutdown").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
    decode(driver, host, ups_data, 3, { 0x21, 0x05, 0x02 });
    TEST_ASSERT_EQUAL_STRING("65", ups_data.get("ups.timer.shutdown").c_str());
    TEST_ASSERT_FALSE(ups_data.hasKey("ups.delay.shutdown"));
    TEST_ASSERT_FALSE(driver.shutdownCommandsSupported());
}

// US-066: powercom-hid.c:620-639 declares ups.delay.start (default "60") and
// ups.delay.shutdown (DEFAULT_OFFDELAY, 20) as RW with info_len 8, on
// UPS.PowerSummary.DelayBeforeStartup/Shutdown or PCMDelayBeforeShutdown (0x00ff0057).
// The variables exist even without the shutdown commands.
void test_powercom_delay_variables_as_upstream(void) {
    PowercomDriver driver;
    TEST_ASSERT_TRUE(driver.delayVariablesSupported());
    TEST_ASSERT_FALSE(driver.shutdownCommandsSupported());

    std::vector<HIDUsageDef> usages(2);
    strcpy(usages[0].path, "UPS.PowerSummary.DelayBeforeStartup");
    usages[0].usage = 0x00840056;
    usages[0].report_type = 3;
    strcpy(usages[1].path, "UPS.PowerSummary.DelayBeforeShutdown");
    usages[1].usage = 0x00840057;
    usages[1].report_type = 3;
    UPSData data;
    CommandCatalog::applyDefaultDelays(data, usages, driver.defaultOffDelay(), driver.defaultOnDelay(),
                                       driver.delayVariablesSupported(), driver.delayPaths());
    TEST_ASSERT_EQUAL_STRING("60", data.get("ups.delay.start").c_str());
    TEST_ASSERT_EQUAL_STRING("20", data.get("ups.delay.shutdown").c_str());

    std::vector<NutTable::RWVariable> rw;
    driver.writableVariables(data, rw);
    TEST_ASSERT_EQUAL(2, rw.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", rw[0].name);
    TEST_ASSERT_EQUAL(8, rw[0].aux);
    TEST_ASSERT_EQUAL_STRING("ups.delay.start", rw[1].name);
    TEST_ASSERT_EQUAL(8, rw[1].aux);

    // PCMDelayBeforeShutdown alone gives ups.delay.shutdown only (powercom-hid.c:639)
    std::vector<HIDUsageDef> pcm(1);
    strcpy(pcm[0].path, "UPS.PowerSummary.0x00FF0057");
    pcm[0].usage = 0x00ff0057;
    pcm[0].report_type = 3;
    UPSData only_pcm;
    CommandCatalog::applyDefaultDelays(only_pcm, pcm, driver.defaultOffDelay(), driver.defaultOnDelay(),
                                       driver.delayVariablesSupported(), driver.delayPaths());
    TEST_ASSERT_EQUAL_STRING("20", only_pcm.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_FALSE(only_pcm.hasKey("ups.delay.start"));
}

// --- US-077 baseline (TASK-01) and the same reports after the migration

// One synthetic value for every FEATURE report of the SPD-750U descriptor that powercom-hid
// or the firmware reads, values told apart (input/output from issue #36). Report 10/38:
// Charging, ACPresent and BatteryPresent set. Dates 0x4E6F = 2019/03/15.
static std::vector<SynthReport> spd750uReports() {
    return {
        { 3, { 10, 0x0D, 0x00 } }, { 3, { 38, 0x0D, 0x00 } },
        { 3, { 14, 87 } }, { 3, { 24, 86 } }, { 3, { 55, 0xDC, 0x05 } }, { 3, { 12, 10 } }, { 3, { 11, 20 } },
        { 3, { 13, 0x6F, 0x4E } }, { 3, { 23, 0x6F, 0x4E } }, { 3, { 5, 4 } },
        { 3, { 15, 0x02, 0x01 } }, { 3, { 16, 0x00, 0x03, 0x00, 0x00 } }, { 3, { 17, 220, 0 } }, { 3, { 18, 220, 0 } },
        { 3, { 19, 1 } }, { 3, { 37, 1 } }, { 3, { 21, 6 } }, { 3, { 25, 12, 0 } }, { 3, { 26, 137, 0 } },
        { 3, { 28, 230, 0 } }, { 3, { 29, 218, 0 } }, { 3, { 30, 50 } }, { 3, { 31, 22 } }, { 3, { 32, 220, 0 } },
        { 3, { 33, 216, 0 } }, { 3, { 34, 50 } }, { 3, { 44, 50 } }, { 3, { 45, 30 } },
        { 3, { 0xA4, ' ', '1', '3', '.', '7', ' ', '2' } },
        { 3, { 10, 0x0D, 0x00 } }, // the next keep-alive, after 0xA4
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

// PowercomDriver with its poll hook public
struct PowercomPollProbe : PowercomDriver {
    using PowercomDriver::buildPollLists;
};

static std::string pollListsOf(MockPowercomHost& host) {
    PowercomPollProbe probe;
    probe.setup();
    std::vector<GenericDriver::PollItem> quick, full;
    probe.buildPollLists(&host, quick, full);
    std::string out = "quick:";
    for (const auto& i : quick) out += " " + std::to_string(i.id) + "/" + std::to_string(i.report_type) + "/" + std::to_string(i.length);
    out += " | full:";
    for (const auto& i : full) out += " " + std::to_string(i.id) + "/" + std::to_string(i.report_type) + "/" + std::to_string(i.length);
    return out;
}

// Before US-077 (hand-written mappings), kept as the reference of the differences below
static const char* const kPowercomSnapshotBefore =
    "battery.charge.low=10\n"
    "battery.charge=87\n"
    "battery.mfr.date=2019/03/15\n"
    "battery.runtime=1500\n"
    "battery.temperature=30.0\n"
    "battery.voltage.nominal=12\n"
    "battery.voltage=13.70\n"
    "input.frequency=50.0\n"
    "input.voltage.nominal=230\n"
    "input.voltage=218.0\n"
    "output.frequency=50.0\n"
    "output.voltage.nominal=220\n"
    "output.voltage=216.0\n"
    "ups.beeper.status=enabled\n"
    "ups.load=22\n"
    "ups.mfr.date=2019/03/15\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.comm_lost=0\n"
    "ups.status.discharging=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.temperature=50.0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.shutdown=258\n"
    "ups.status=OL CHRG\n";

// After US-077, every key from powercomHid2Nut(). Differences from kPowercomSnapshotBefore:
// + battery.charge.warning=20      powercom-hid.c:592
// - battery.voltage.nominal=12     PowerSummary/Battery ConfigVoltage dropped upstream (lines
//                                  577-580: PowerCOM duplicates output.voltage.nominal there)
// ~ battery.voltage 13.70 -> 13.7  powercom-hid.c:587, the text of report 0xA4 as it is ("%s")
// + ups.date=2019/03/15            powercom-hid.c:604 (PowerSummary.ManufacturerDate)
// - ups.mfr.date=2019/03/15        upstream ups.mfr.date is the vendor PowercomManufacturerDate (675)
// + ups.status.boost/trim=0        powercom-hid.c:560-561, Boost/Buck of report 38 (internal keys)
// ~ ups.status.comm_lost -> commfault  powercom-hid.c:548 commfault_info (internal key, ALARM when set)
// ~ ups.timer.shutdown 258 -> 62   powercom-hid.c:638 powercom_shutdown_info: 0x0102 = 1 min 2 s
// + ups.timer.start=180            powercom-hid.c:621 powercom_startup_info: 0x0300 = 3 min
// = ups.temperature=50.0           kept extension (PowerSummary.Temperature, ADR 0011 rule 2);
//                                  report 44 is not polled, so the UPS never shows it
// ups.status unchanged.
static const char* const kPowercomSnapshotAfter =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=87\n"
    "battery.mfr.date=2019/03/15\n"
    "battery.runtime=1500\n"
    "battery.temperature=30.0\n"
    "battery.voltage=13.7\n"
    "input.frequency=50.0\n"
    "input.voltage.nominal=230\n"
    "input.voltage=218.0\n"
    "output.frequency=50.0\n"
    "output.voltage.nominal=220\n"
    "output.voltage=216.0\n"
    "ups.beeper.status=enabled\n"
    "ups.date=2019/03/15\n"
    "ups.load=22\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=1\n"
    "ups.status.commfault=0\n"
    "ups.status.discharging=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.status.trim=0\n"
    "ups.temperature=50.0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.shutdown=62\n"
    "ups.timer.start=180\n"
    "ups.status=OL CHRG\n";

// Poll lists ("id/type/length") before and after. After (user decisions of 2026-10-29):
// quick = keep-alive 0x0A + report 38 of the BOOL rows 557-568, so a stale UPS.PresentStatus
// never hides an OB of report 0x0A; full = the reports of the bound rows in table order
// (590 Battery.RemainingCapacity and 602 UPS.AudibleAlarmControl are second candidates: their
// reports 24 and 37 are not read), then 0xA4. 1D/1F/21 keep their 8-byte requests (#36).
static const char* const kPowercomPollBefore =
    "quick: 10/3/8 | full: 10/3/8 29/3/8 33/3/8 31/3/8 45/3/0 19/3/0 164/3/8";
static const char* const kPowercomPollAfter =
    "quick: 10/3/8 38/3/0 | full: 10/3/8 38/3/0 14/3/0 12/3/0 11/3/0 55/3/0 23/3/0 5/3/0 19/3/0 31/3/8 "
    "13/3/0 21/3/0 16/3/0 15/3/0 29/3/8 28/3/0 30/3/0 33/3/8 32/3/0 34/3/0 45/3/0 164/3/8";

void test_powercom_snapshot_baseline(void) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decodeAll(driver, host, data, spd750uReports());
    TEST_ASSERT_TRUE(strcmp(kPowercomSnapshotBefore, kPowercomSnapshotAfter) != 0);
    TEST_ASSERT_EQUAL_STRING(kPowercomSnapshotAfter, snapshotOf(data).c_str());
}

void test_powercom_poll_baseline(void) {
    MockPowercomHost host;
    spd750uHost(host);
    TEST_ASSERT_TRUE(strcmp(kPowercomPollBefore, kPowercomPollAfter) != 0);
    TEST_ASSERT_EQUAL_STRING(kPowercomPollAfter, pollListsOf(host).c_str());
}

// --- PowercomUPS vendor page (models other than the SPD-750U, no fixture)

// A hand-made descriptor with the vendor page 0x0002 of powercom_usage_lkp:
// PowercomUPS (application) > PowercomPowerConverter > PowercomInput: Voltage (report 1) and
// PowercomOutput: InternalChargeController, PrimaryBatterySupport, Voltage, PercentLoad
// (report 2), one byte each
static void vendorHost(MockPowercomHost& host) {
    static const uint8_t desc[] = {
        0x05, 0x02, 0x09, 0x04, 0xA1, 0x01,             // Usage Page 0x02, PowercomUPS, Application
        0x09, 0x16, 0xA1, 0x00,                         // PowercomPowerConverter
        0x09, 0x1A, 0xA1, 0x00,                         // PowercomInput
        0x85, 0x01, 0x09, 0x30, 0x75, 0x08, 0x95, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x00, 0xB1, 0x02,
        0xC0,
        0x09, 0x1C, 0xA1, 0x00,                         // PowercomOutput
        0x85, 0x02, 0x09, 0x81, 0x09, 0x82, 0x09, 0x30, 0x09, 0x35,
        0x75, 0x08, 0x95, 0x04, 0x15, 0x00, 0x26, 0xFF, 0x00, 0xB1, 0x02,
        0xC0,
        0xC0,
        0xC0,
    };
    host._hid_parser.parseReportDescriptor(desc, sizeof(desc));
    host._usages = host._hid_parser.getUsages();
}

static bool hasUsagePath(const MockPowercomHost& host, const char* path) {
    for (const auto& u : host._usages) {
        if (strcmp(u.path, path) == 0) return true;
    }
    return false;
}

void test_powercom_vendor_page_paths(void) {
    // The application collection is PowercomUPS itself: the paths start with its code, as
    // the translations of powercomHid2Nut() assume
    MockPowercomHost host;
    vendorHost(host);
    TEST_ASSERT_TRUE(hasUsagePath(host, "0x00020004.0x00020016.0x0002001A.0x00020030"));
    TEST_ASSERT_TRUE(hasUsagePath(host, "0x00020004.0x00020016.0x0002001C.0x00020081"));
    TEST_ASSERT_TRUE(hasUsagePath(host, "0x00020004.0x00020016.0x0002001C.0x00020082"));

    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decode(driver, host, data, 3, { 1, 55 });
    decode(driver, host, data, 3, { 2, 0x00, 0x00, 57, 33 });
    // powercom-hid.c:683-685: voltages times 4, the load as it is
    TEST_ASSERT_EQUAL_STRING("220", data.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("228", data.get("output.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("33", data.get("ups.load").c_str());
    // The rows 699-702 and 716-721 are bound: they set the internal keys
    int bound = 0;
    for (const auto& b : driver.nutBindings()) {
        if (strcmp(b.info->info_type, "BOOL") == 0) bound++;
    }
    TEST_ASSERT_EQUAL(9, bound);
}

static String vendorStatus(uint8_t icc, uint8_t pbs, UPSData& data) {
    MockPowercomHost host;
    vendorHost(host);
    PowercomDriver driver;
    driver.setup();
    decode(driver, host, data, 3, { 2, icc, pbs, 57, 33 });
    return UPSData::computeUPSStatusString(data);
}

void test_powercom_vendor_status(void) {
    UPSData d1;
    TEST_ASSERT_EQUAL_STRING("OL", vendorStatus(0x00, 0x00, d1).c_str());
    // powercom_online_conversion gives "!online", not "offline": without a Discharging bit
    // upstream reports no power state either ("does not report a power state")
    UPSData d2;
    TEST_ASSERT_TRUE(strstr(vendorStatus(0x00, 0x01, d2).c_str(), "OL") == nullptr);
    UPSData d3;
    TEST_ASSERT_EQUAL_STRING("OL LB", vendorStatus(0x00, 0x02, d3).c_str());
    UPSData d4;
    TEST_ASSERT_EQUAL_STRING("OL TRIM", vendorStatus(0x00, 0x08, d4).c_str());
    UPSData d5;
    TEST_ASSERT_EQUAL_STRING("OL BOOST", vendorStatus(0x00, 0x18, d5).c_str());
    UPSData d6;
    TEST_ASSERT_EQUAL_STRING("OL OVER", vendorStatus(0x00, 0x20, d6).c_str());
    // InternalChargeController: bit 0 is the UPS fault, "fanfail" upstream
    UPSData d7;
    TEST_ASSERT_EQUAL_STRING("ALARM OL", vendorStatus(0x01, 0x00, d7).c_str());
    TEST_ASSERT_EQUAL_STRING("Fan failure!", d7.get("ups.alarm").c_str());
    // replacebatt gives RB and its alarm (ups_alarm_set, US-107)
    UPSData d8;
    TEST_ASSERT_EQUAL_STRING("ALARM OL RB", vendorStatus(0x02, 0x00, d8).c_str());
    TEST_ASSERT_EQUAL_STRING("Replace battery!", d8.get("ups.alarm").c_str());
    UPSData d9;
    TEST_ASSERT_EQUAL_STRING("CAL OL", vendorStatus(0x04, 0x00, d9).c_str());
    // shutdownimm gives LB and its alarm (ups_alarm_set, US-107)
    UPSData d10;
    TEST_ASSERT_EQUAL_STRING("ALARM OL LB", vendorStatus(0x10, 0x00, d10).c_str());
    TEST_ASSERT_TRUE(d10.get("ups.alarm").length() > 0);
}

// The SPD-750U has the status twice: PowerSummary.PresentStatus (report 10) and
// UPS.PresentStatus (report 38). The BOOL rows are applied again in table order after each
// report, so report 38 (rows 557-568) decides: it is in the quick poll, read every 2 s with
// the keep-alive. Once it is read fresh, the OB of report 10 shows.
void test_powercom_status_38_follows_10(void) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decode(driver, host, data, 3, { 14, 87 }); // battery.charge, for CHRG
    decode(driver, host, data, 3, { 10, 0x0D, 0x00 });
    decode(driver, host, data, 3, { 38, 0x0D, 0x00 });
    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());
    // Mains lost: Discharging and BatteryPresent, no ACPresent
    decode(driver, host, data, 1, { 10, 0x0A, 0x00 });
    // Report 38 still stale: its rows carry every bit of report 10 and come later in the table
    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());
    decode(driver, host, data, 3, { 38, 0x0A, 0x00 });
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());

    std::string lists = pollListsOf(host);
    TEST_ASSERT_TRUE(lists.find("quick: 10/3/8 38/3/0 |") == 0);
}

// The quick poll does not run during a full poll (about 17 requests 800 ms apart): a change of
// report 0x0A (here an INPUT report: mains lost) makes report 38 the next request, so the OB
// shows within one step instead of waiting for the next read of report 38 (review of US-077)
void test_powercom_status_change_reads_38_next(void) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decode(driver, host, data, 3, { 14, 87 });
    uint32_t t = 100;
    for (int i = 0; i < 3; i++, t += 800) driver.loop(&host, data, t); // 10, 38, 14
    decode(driver, host, data, 3, { 10, 0x0D, 0x00 });
    decode(driver, host, data, 3, { 38, 0x0D, 0x00 });
    driver.loop(&host, data, t); // 12
    t += 800;
    TEST_ASSERT_EQUAL_UINT8(12, host._requestedReports.back().first);

    // The same status again: nothing extra
    decode(driver, host, data, 1, { 10, 0x0D, 0x00 });
    driver.loop(&host, data, t);
    t += 800;
    TEST_ASSERT_EQUAL_UINT8(11, host._requestedReports.back().first);

    // Mains lost: report 38 comes next, then the full poll goes on where it was
    decode(driver, host, data, 1, { 10, 0x0A, 0x00 });
    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());
    driver.loop(&host, data, t);
    t += 800;
    TEST_ASSERT_EQUAL_UINT8(38, host._requestedReports.back().first);
    decode(driver, host, data, 3, { 38, 0x0A, 0x00 });
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
    driver.loop(&host, data, t);
    TEST_ASSERT_EQUAL_UINT8(55, host._requestedReports.back().first);
}

// ADR 0011 rule 2 on the SPD-750U: no battery.capacity.full, no battery.voltage(.nominal)
// from the standard paths (they duplicate the output voltage on PowerCOM)
void test_powercom_extensions(void) {
    MockPowercomHost host;
    spd750uHost(host);
    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decodeAll(driver, host, data, { { 3, { 17, 220, 0 } }, { 3, { 18, 220, 0 } }, { 3, { 25, 12, 0 } }, { 3, { 26, 137, 0 } } });
    TEST_ASSERT_FALSE(data.hasKey("battery.voltage.nominal"));
    TEST_ASSERT_FALSE(data.hasKey("battery.voltage"));
    TEST_ASSERT_FALSE(data.hasKey("battery.capacity.full"));
}

// Only the strings of the table rows are requested: never iManufacturer/iProduct/iSerialNumber
void test_powercom_only_table_strings_requested(void) {
    MockPowercomHost host;
    spd750uHost(host);
    host._iManufacturer = 1;
    host._iProduct = 2;
    host._iSerialNumber = 3;
    PowercomDriver driver;
    driver.setup();
    UPSData data;
    decode(driver, host, data, 3, { 5, 4 }); // iDeviceChemistry -> string 4 (battery.type, 595)
    for (uint32_t t = 100; t < 100 + 26 * 800; t += 800) driver.loop(&host, data, t);
    TEST_ASSERT_EQUAL_UINT32(1, host._requestedStrings.size());
    TEST_ASSERT_EQUAL_UINT8(4, host._requestedStrings[0]);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_powercom_0xa4_valid);
    RUN_TEST(test_powercom_0xa4_invalid_no_dot);
    RUN_TEST(test_powercom_0xa4_invalid_empty);
    RUN_TEST(test_powercom_0xa4_invalid_garbage);
    RUN_TEST(test_powercom_0xa4_keeps_last_valid);
    RUN_TEST(test_powercom_beeper_mapping);
    RUN_TEST(test_powercom_loop_polling_nut_alignment);
    RUN_TEST(test_powercom_device_strings_as_upstream);
    RUN_TEST(test_powercom_real_descriptor_parsing);
    RUN_TEST(test_powercom_full_poll_spd750u_descriptor);
    RUN_TEST(test_powercom_static_reports_read_once);
    RUN_TEST(test_powercom_delay_sets_only_timer);
    RUN_TEST(test_powercom_delay_variables_as_upstream);
    RUN_TEST(test_powercom_snapshot_baseline);
    RUN_TEST(test_powercom_poll_baseline);
    RUN_TEST(test_powercom_vendor_page_paths);
    RUN_TEST(test_powercom_vendor_status);
    RUN_TEST(test_powercom_status_38_follows_10);
    RUN_TEST(test_powercom_status_change_reads_38_next);
    RUN_TEST(test_powercom_extensions);
    RUN_TEST(test_powercom_only_table_strings_requested);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_powercom_0xa4_valid);
    RUN_TEST(test_powercom_0xa4_invalid_no_dot);
    RUN_TEST(test_powercom_0xa4_invalid_empty);
    RUN_TEST(test_powercom_0xa4_invalid_garbage);
    RUN_TEST(test_powercom_0xa4_keeps_last_valid);
    RUN_TEST(test_powercom_beeper_mapping);
    RUN_TEST(test_powercom_loop_polling_nut_alignment);
    RUN_TEST(test_powercom_device_strings_as_upstream);
    RUN_TEST(test_powercom_real_descriptor_parsing);
    RUN_TEST(test_powercom_full_poll_spd750u_descriptor);
    RUN_TEST(test_powercom_static_reports_read_once);
    RUN_TEST(test_powercom_delay_sets_only_timer);
    RUN_TEST(test_powercom_delay_variables_as_upstream);
    RUN_TEST(test_powercom_snapshot_baseline);
    RUN_TEST(test_powercom_poll_baseline);
    RUN_TEST(test_powercom_vendor_page_paths);
    RUN_TEST(test_powercom_vendor_status);
    RUN_TEST(test_powercom_status_38_follows_10);
    RUN_TEST(test_powercom_status_change_reads_38_next);
    RUN_TEST(test_powercom_extensions);
    RUN_TEST(test_powercom_only_table_strings_requested);
    UNITY_END();
}
void loop() {}
#endif
#endif
