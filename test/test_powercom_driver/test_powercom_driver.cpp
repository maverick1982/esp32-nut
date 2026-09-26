#include <unity.h>
#include "PowercomDriver.h"
#include "IUSBHostUPS.h"

class MockPowercomHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;
    std::vector<uint16_t> _requestedLengths;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
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

void test_powercom_0xa4_valid(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    // " 13.7 2"
    // Report ID (0xA4) is byte 0
    uint8_t data[] = { 0xA4, ' ', '1', '3', '.', '7', ' ', '2' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    TEST_ASSERT_EQUAL_FLOAT(13.7f, ups_data.getFloat("battery.voltage"));
    TEST_ASSERT_TRUE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_0xa4_invalid_no_dot(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, ' ', '1', '3', '7', ' ', '2', ' ' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not update batteryVoltage if no dot is present
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_0xa4_invalid_empty(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not crash and not update batteryVoltage
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_0xa4_invalid_garbage(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, 'a', 'b', 'c', 'd', 'e', 'f', 'g' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not crash and not update batteryVoltage
    TEST_ASSERT_FALSE(ups_data.hasKey("battery.voltage"));
}

void test_powercom_beeper_mapping(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    // Powercom NUT definition: 1 = enabled, 2 = disabled
    // When AudibleAlarmControl is extracted as 1, beeperEnabled must be true
    // When AudibleAlarmControl is extracted as 2, beeperEnabled must be false
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

    uint8_t data_enable[] = { 0x1F, 0x01 };
    driver.decodeReport(&host, 0x1F, 3, data_enable, sizeof(data_enable), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.beeper.status"));
    TEST_ASSERT_EQUAL_STRING("enabled", ups_data.get("ups.beeper.status").c_str());

    uint8_t data_disable[] = { 0x1F, 0x02 };
    driver.decodeReport(&host, 0x1F, 3, data_disable, sizeof(data_disable), ups_data);
    TEST_ASSERT_TRUE(ups_data.hasKey("ups.beeper.status"));
    TEST_ASSERT_EQUAL_STRING("disabled", ups_data.get("ups.beeper.status").c_str());

    // Test encodeBeeperValue
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 8));
    TEST_ASSERT_EQUAL_UINT8(2, driver.encodeBeeperValue(false, 8));
    TEST_ASSERT_EQUAL_UINT8(1, driver.encodeBeeperValue(true, 1));
    TEST_ASSERT_EQUAL_UINT8(0, driver.encodeBeeperValue(false, 1));
}

void test_powercom_loop_polling_nut_alignment(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    host._pid = 0x0004;
    driver.setup();

    // Loop execution assigns vendor and product and sends 0x0A keep-alive report (step 1)
    driver.loop(&host, ups_data, 100);
    TEST_ASSERT_EQUAL_STRING("POWERCOM Co.,LTD", ups_data.get("ups.mfr").c_str());
    TEST_ASSERT_EQUAL_STRING("SPD / Vanguard / BNT", ups_data.get("ups.model").c_str());
    TEST_ASSERT_EQUAL_UINT32(1, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x0A, host._requestedReports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[0].second); // Feature report
    TEST_ASSERT_EQUAL_UINT32(0, host._requestedStrings.size());

    // Issue #36: requests of a cycle are 800 ms apart
    driver.loop(&host, ups_data, 899);
    TEST_ASSERT_EQUAL_UINT32(1, host._requestedReports.size());

    // Step 2: input.voltage (0x1D)
    driver.loop(&host, ups_data, 900);
    TEST_ASSERT_EQUAL_UINT32(2, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x1D, host._requestedReports[1].first);

    // Step 3: output.voltage (0x21)
    driver.loop(&host, ups_data, 1700);
    TEST_ASSERT_EQUAL_UINT32(3, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x21, host._requestedReports[2].first);

    // Step 4: ups.load (0x1F)
    driver.loop(&host, ups_data, 2500);
    TEST_ASSERT_EQUAL_UINT32(4, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x1F, host._requestedReports[3].first);

    // Step 5: legacy battery voltage (0xA4), FEATURE of exactly 8 bytes.
    // No temperature nor beeper report in this descriptor-less mock.
    driver.loop(&host, ups_data, 3300);
    TEST_ASSERT_EQUAL_UINT32(5, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0xA4, host._requestedReports[4].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[4].second);
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[4]);

    // End of the full poll: 2 s have passed, the quick keep-alive follows
    driver.loop(&host, ups_data, 4100);
    TEST_ASSERT_EQUAL_UINT32(6, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x0A, host._requestedReports[5].first);

    // Test another PID mapping
    host._pid = 0x00a3;
    UPSData ups_data2;
    driver.loop(&host, ups_data2, 4900);
    TEST_ASSERT_EQUAL_STRING("Smart King Pro", ups_data2.get("ups.model").c_str());
}

#include "HIDParser.h"
#include <fstream>
#include <sstream>
#include <ArduinoJson.h>

void test_powercom_real_descriptor_parsing(void) {
    std::ifstream file("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json");
    TEST_ASSERT_TRUE_MESSAGE(file.is_open(), "Fixture file powercom_spd750u_vid0d9f_pid0004_issue21.json must exist");

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    TEST_ASSERT_FALSE_MESSAGE(err, "JSON deserialization failed");

    JsonArray hexArr = doc["report_descriptor_hex"].as<JsonArray>();
    std::vector<uint8_t> rawDesc;
    for (JsonVariant v : hexArr) {
        std::string hex = v.as<std::string>();
        uint8_t byte = (uint8_t)strtol(hex.c_str(), nullptr, 16);
        rawDesc.push_back(byte);
    }

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

// Issue #36: on the SPD-750U the full poll also reads battery temperature and the active
// beeper report, IDs and lengths taken from the report descriptor
void test_powercom_full_poll_spd750u_descriptor(void) {
    std::ifstream file("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json");
    TEST_ASSERT_TRUE(file.is_open());
    std::stringstream buffer;
    buffer << file.rdbuf();
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buffer.str()));
    std::vector<uint8_t> rawDesc;
    for (JsonVariant v : doc["report_descriptor_hex"].as<JsonArray>()) {
        rawDesc.push_back((uint8_t)strtol(v.as<std::string>().c_str(), nullptr, 16));
    }

    MockPowercomHost host;
    host._hid_parser.parseReportDescriptor(rawDesc.data(), rawDesc.size());
    host._usages = host._hid_parser.getUsages();

    PowercomDriver driver;
    UPSData ups_data;
    driver.setup();
    for (uint32_t t = 100; t < 100 + 7 * 800; t += 800) driver.loop(&host, ups_data, t);

    const uint8_t expected[] = { 0x0A, 0x1D, 0x21, 0x1F, 0x2D, 0x13, 0xA4 };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), host._requestedReports.size());
    for (size_t i = 0; i < sizeof(expected); i++) {
        TEST_ASSERT_EQUAL_UINT8(expected[i], host._requestedReports[i].first);
        TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[i].second);
    }
    TEST_ASSERT_EQUAL_UINT16(2, host._requestedLengths[4]); // 0x2D: id + 8 bit
    TEST_ASSERT_EQUAL_UINT16(2, host._requestedLengths[5]); // 0x13: id + 8 bit
    TEST_ASSERT_EQUAL_UINT16(8, host._requestedLengths[6]); // 0xA4

    // UPS.Battery.Temperature becomes battery.temperature (Celsius, below 200 kept as is)
    uint8_t temp[] = { 0x2D, 41 };
    driver.decodeReport(&host, 0x2D, 3, temp, sizeof(temp), ups_data);
    TEST_ASSERT_EQUAL_STRING("41.0", ups_data.get("battery.temperature").c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_powercom_0xa4_valid);
    RUN_TEST(test_powercom_0xa4_invalid_no_dot);
    RUN_TEST(test_powercom_0xa4_invalid_empty);
    RUN_TEST(test_powercom_0xa4_invalid_garbage);
    RUN_TEST(test_powercom_beeper_mapping);
    RUN_TEST(test_powercom_loop_polling_nut_alignment);
    RUN_TEST(test_powercom_real_descriptor_parsing);
    RUN_TEST(test_powercom_full_poll_spd750u_descriptor);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_powercom_0xa4_valid);
    RUN_TEST(test_powercom_0xa4_invalid_no_dot);
    RUN_TEST(test_powercom_0xa4_invalid_empty);
    RUN_TEST(test_powercom_0xa4_invalid_garbage);
    RUN_TEST(test_powercom_beeper_mapping);
    RUN_TEST(test_powercom_loop_polling_nut_alignment);
    RUN_TEST(test_powercom_real_descriptor_parsing);
    RUN_TEST(test_powercom_full_poll_spd750u_descriptor);
    UNITY_END();
}
void loop() {}
#endif
#endif

