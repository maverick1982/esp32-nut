#include <unity.h>
#include "PowercomDriver.h"
#include "IUSBHostUPS.h"

class MockPowercomHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

    const UPSData& getUPSData() const override { return _data; }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    uint16_t _pid = 0x0004;
    uint16_t getPID() const override { return _pid; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t) override {
        _requestedReports.push_back({report_id, report_type});
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
    
    TEST_ASSERT_EQUAL_FLOAT(13.7f, ups_data.batteryVoltage);
    TEST_ASSERT_TRUE(ups_data.has.batteryVoltage);
}

void test_powercom_0xa4_invalid_no_dot(void) {
    PowercomDriver driver;
    UPSData ups_data;
    ups_data.batteryVoltage = 0.0f;
    ups_data.has.batteryVoltage = false;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, ' ', '1', '3', '7', ' ', '2', ' ' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not update batteryVoltage if no dot is present
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ups_data.batteryVoltage);
    TEST_ASSERT_FALSE(ups_data.has.batteryVoltage);
}

void test_powercom_0xa4_invalid_empty(void) {
    PowercomDriver driver;
    UPSData ups_data;
    ups_data.batteryVoltage = 0.0f;
    ups_data.has.batteryVoltage = false;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not crash and not update batteryVoltage
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ups_data.batteryVoltage);
    TEST_ASSERT_FALSE(ups_data.has.batteryVoltage);
}

void test_powercom_0xa4_invalid_garbage(void) {
    PowercomDriver driver;
    UPSData ups_data;
    ups_data.batteryVoltage = 0.0f;
    ups_data.has.batteryVoltage = false;
    MockPowercomHost host;
    
    uint8_t data[] = { 0xA4, 'a', 'b', 'c', 'd', 'e', 'f', 'g' };
    
    driver.decodeReport(&host, 0xA4, 3, data, sizeof(data), ups_data);
    
    // Should not crash and not update batteryVoltage
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ups_data.batteryVoltage);
    TEST_ASSERT_FALSE(ups_data.has.batteryVoltage);
}

void test_powercom_beeper_mapping(void) {
    PowercomDriver driver;
    UPSData ups_data;
    MockPowercomHost host;
    
    // Powercom NUT definition: 1 = enabled, 2 = disabled
    // When AudibleAlarmControl is extracted as 1, beeperEnabled must be true
    // When AudibleAlarmControl is extracted as 2, beeperEnabled must be false
    HIDUsageDef def;
    def.path = "UPS.PowerSummary.AudibleAlarmControl";
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
    TEST_ASSERT_TRUE(ups_data.has.beeperEnabled);
    TEST_ASSERT_TRUE(ups_data.beeperEnabled);

    uint8_t data_disable[] = { 0x1F, 0x02 };
    driver.decodeReport(&host, 0x1F, 3, data_disable, sizeof(data_disable), ups_data);
    TEST_ASSERT_TRUE(ups_data.has.beeperEnabled);
    TEST_ASSERT_FALSE(ups_data.beeperEnabled);

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
    TEST_ASSERT_EQUAL_STRING("Powercom", ups_data.manufacturer.c_str());
    TEST_ASSERT_EQUAL_STRING("SPD / Vanguard / BNT", ups_data.product.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x0A, host._requestedReports[0].first);
    TEST_ASSERT_EQUAL_UINT8(3, host._requestedReports[0].second); // Feature report
    TEST_ASSERT_EQUAL_UINT32(0, host._requestedStrings.size());

    // Step 2: input.voltage (0x1D)
    driver.loop(&host, ups_data, 200);
    TEST_ASSERT_EQUAL_UINT32(2, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x1D, host._requestedReports[1].first);

    // Step 3: output.voltage (0x21)
    driver.loop(&host, ups_data, 300);
    TEST_ASSERT_EQUAL_UINT32(3, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x21, host._requestedReports[2].first);

    // Step 4: ups.load (0x1F)
    driver.loop(&host, ups_data, 400);
    TEST_ASSERT_EQUAL_UINT32(4, host._requestedReports.size());
    TEST_ASSERT_EQUAL_UINT8(0x1F, host._requestedReports[3].first);

    // Test another PID mapping
    host._pid = 0x00a3;
    ups_data.product = "";
    driver.loop(&host, ups_data, 500);
    TEST_ASSERT_EQUAL_STRING("Smart King Pro", ups_data.product.c_str());
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
        if (u.path.indexOf("PresentStatus") >= 0 || u.usage == 0x00840002 || u.usage == 0x00850044) found_present_status = true;
        if (u.path.indexOf("RemainingCapacity") >= 0 || u.usage == 0x00850066) found_remaining_capacity = true;
        if (u.path.indexOf("RunTimeToEmpty") >= 0 || u.usage == 0x00850068) found_run_time_to_empty = true;
        if (u.path.indexOf("AudibleAlarmControl") >= 0 || u.usage == 0x0084005A || u.usage == 0x0085005A) found_beeper = true;
    }

    TEST_ASSERT_TRUE_MESSAGE(found_present_status, "PresentStatus usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_remaining_capacity, "RemainingCapacity usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_run_time_to_empty, "RunTimeToEmpty usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(found_beeper, "AudibleAlarmControl usage should be present");
    TEST_ASSERT_TRUE_MESSAGE(parser.hasFeatureBeeperControl(), "Powercom SPD-750U descriptor contains Feature report for beeper");
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
    UNITY_END();
}
void loop() {}
#endif
#endif
