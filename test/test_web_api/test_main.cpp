#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include "WebApiJson.h"

// A simple mock for testing API JSON generation natively
class APIMockUSBHost : public IUSBHostUPS {
public:
    UPSData data;
    bool connected = true;
    std::vector<HIDUsageDef> usages;

    void lock() const override {}
    void unlock() const override {}
    void end() override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(data, this); }
    bool isConnected() const override { return connected; }
    String getUPSStatusString() const override { return connected ? "OL" : "UNKNOWN"; }
    bool setBeeper(bool enable) override { return true; }
    bool supportsBeeperToggle() const override { return true; }
    
    // New pure virtuals
    const std::vector<HIDUsageDef>& getUsages() const override { return usages; }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) override { return true; }
    bool requestStringDescriptor(uint8_t string_index) override { return true; }
};

APIMockUSBHost mockHost;

void setUp(void) {}
void tearDown(void) {}

void test_api_null_host() {
    String out = WebApiJson::generateUpsVars(nullptr);
    TEST_ASSERT_EQUAL_STRING("{\"error\": \"UPS non inizializzato\"}", out.c_str());
}

void test_api_disconnected_host() {
    mockHost.connected = false;
    String out = WebApiJson::generateUpsVars(&mockHost);
    
    JsonDocument doc;
    deserializeJson(doc, out);
    
    TEST_ASSERT_TRUE(doc["_disconnected"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("Disconnected", doc["ups.status"].as<const char*>());
    // Ensure no other standard fields are present
    TEST_ASSERT_TRUE(doc["ups.mfr"].isNull());
    TEST_ASSERT_TRUE(doc["ups.beeper.switchable"].isNull());
}

void test_api_connected_host_with_data() {
    mockHost.connected = true;
    mockHost.data.set("ups.mfr", "Eaton");
    
    String out = WebApiJson::generateUpsVars(&mockHost);
    
    JsonDocument doc;
    deserializeJson(doc, out);
    
    TEST_ASSERT_TRUE(doc["_disconnected"].isNull());
    TEST_ASSERT_EQUAL_STRING("OL", doc["ups.status"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Eaton", doc["ups.mfr"].as<const char*>());
    TEST_ASSERT_TRUE(doc["ups.beeper.switchable"].as<bool>());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_api_null_host);
    RUN_TEST(test_api_disconnected_host);
    RUN_TEST(test_api_connected_host_with_data);
    return UNITY_END();
}
