#include <Arduino.h>
#include <unity.h>
#include "BeeperLogic.h"
#include "PowercomDriver.h"

void setUp(void) {}
void tearDown(void) {}

void test_beeper_1bit_without_report_id() {
    HIDUsageDef def;
    def.report_id = 0;
    def.bit_offset = 0;
    def.bit_size = 1;
    
    uint8_t buffer[64] = {0};
    
    // Test enable
    size_t len = BeeperLogic::manipulateBeeperBuffer(true, &def, buffer, 1, nullptr);
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL(1, buffer[0]);
    
    // Test disable
    len = BeeperLogic::manipulateBeeperBuffer(false, &def, buffer, 1, nullptr);
    TEST_ASSERT_EQUAL(1, len);
    TEST_ASSERT_EQUAL(0, buffer[0]);
}

void test_beeper_1bit_with_report_id() {
    HIDUsageDef def;
    def.report_id = 5;
    def.bit_offset = 4; // 4 bits shift
    def.bit_size = 1;
    
    uint8_t buffer[64] = {0xFF, 0xFF}; // Junk
    buffer[0] = 5; // Report ID
    buffer[1] = 0;
    
    // Enable
    size_t len = BeeperLogic::manipulateBeeperBuffer(true, &def, buffer, 2, nullptr);
    TEST_ASSERT_EQUAL(2, len);
    TEST_ASSERT_EQUAL(0x10, buffer[1]); // Bit 4 set
    
    // Disable
    len = BeeperLogic::manipulateBeeperBuffer(false, &def, buffer, 2, nullptr);
    TEST_ASSERT_EQUAL(0x00, buffer[1]); // Bit 4 cleared
}

void test_beeper_8bit_cyberpower() {
    HIDUsageDef def;
    def.report_id = 2;
    def.bit_offset = 8; // byte 2
    def.bit_size = 8;
    
    uint8_t buffer[64] = {0};
    
    // Enable -> standard HID is 2
    size_t len = BeeperLogic::manipulateBeeperBuffer(true, &def, buffer, 3, nullptr);
    TEST_ASSERT_EQUAL(2, buffer[2]);
    
    // Disable -> standard HID is 1
    len = BeeperLogic::manipulateBeeperBuffer(false, &def, buffer, 3, nullptr);
    TEST_ASSERT_EQUAL(1, buffer[2]);
}

void test_beeper_powercom_quirk() {
    PowercomDriver drv;
    HIDUsageDef def;
    def.report_id = 0;
    def.bit_offset = 0;
    def.bit_size = 8;
    
    uint8_t buffer[64] = {0};
    
    // Powercom quirk: enable -> 1, disable -> 2
    BeeperLogic::manipulateBeeperBuffer(true, &def, buffer, 1, &drv);
    TEST_ASSERT_EQUAL(1, buffer[0]);
    
    BeeperLogic::manipulateBeeperBuffer(false, &def, buffer, 1, &drv);
    TEST_ASSERT_EQUAL(2, buffer[0]);
}

void test_beeper_buffer_expansion() {
    HIDUsageDef def;
    def.report_id = 1;
    def.bit_offset = 64; // byte 8 (so byte_index=1 + 8 = byte 9). We need at least 10 bytes payload.
    def.bit_size = 8;
    
    uint8_t buffer[64] = {0};
    
    // Give it a fetched_len of 1 (too small)
    size_t len = BeeperLogic::manipulateBeeperBuffer(true, &def, buffer, 1, nullptr);
    TEST_ASSERT_EQUAL(10, len); // It expanded it to 10
    TEST_ASSERT_EQUAL(2, buffer[9]); // Standard HID enabled value = 2
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_beeper_1bit_without_report_id);
    RUN_TEST(test_beeper_1bit_with_report_id);
    RUN_TEST(test_beeper_8bit_cyberpower);
    RUN_TEST(test_beeper_powercom_quirk);
    RUN_TEST(test_beeper_buffer_expansion);
    return UNITY_END();
}
