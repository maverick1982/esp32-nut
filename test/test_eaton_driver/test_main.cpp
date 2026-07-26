#include <Arduino.h>
#include <unity.h>
#include "EatonDriver.h"

EatonDriver driver;
UPSData ups_data;

void setUp(void) {
    ups_data = UPSData(); // reset
}

void tearDown(void) {
    // clean stuff up here
}

void test_decode_report_0x01(void) {
    uint8_t report[] = {0x01, 0x01, 0x00, 0x00}; // AC present (bit 0)
    driver.decodeReport(nullptr, 0x01, report, sizeof(report), ups_data);
    TEST_ASSERT_TRUE(ups_data.acPresent);
    TEST_ASSERT_FALSE(ups_data.discharging);

    uint8_t report2[] = {0x01, 0x10, 0x00, 0x00}; // discharging (bit 4)
    driver.decodeReport(nullptr, 0x01, report2, sizeof(report2), ups_data);
    TEST_ASSERT_FALSE(ups_data.acPresent);
    TEST_ASSERT_TRUE(ups_data.discharging);
}

void test_decode_report_0x06(void) {
    uint8_t report[] = {0x06, 50, 0x00, 0x00, 0x00, 0x00}; // 50% capacity, 0 runTimeToEmpty
    driver.decodeReport(nullptr, 0x06, report, sizeof(report), ups_data);
    TEST_ASSERT_EQUAL_UINT8(50, ups_data.remainingCapacity);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_decode_report_0x01);
    RUN_TEST(test_decode_report_0x06);
    UNITY_END();
}

void loop() {
    delay(100);
}
