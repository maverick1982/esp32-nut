#include <unity.h>
#include "UPSData.h"

void setUp(void) {}
void tearDown(void) {}

void test_status_online_normal(void) {
    UPSData data;
    data.has.acPresent = true;
    data.acPresent = true;
    data.has.discharging = true;
    data.discharging = false;

    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_on_battery_discharging(void) {
    UPSData data;
    data.has.acPresent = true;
    data.acPresent = false;
    data.has.discharging = true;
    data.discharging = true;

    TEST_ASSERT_EQUAL_STRING("OB", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_on_battery_low_battery(void) {
    UPSData data;
    data.has.acPresent = true;
    data.acPresent = false;
    data.has.discharging = true;
    data.discharging = true;
    data.has.belowRemainingCapacityLimit = true;
    data.belowRemainingCapacityLimit = true;

    TEST_ASSERT_EQUAL_STRING("OB LB", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_online_charging(void) {
    UPSData data;
    data.has.acPresent = true;
    data.acPresent = true;
    data.has.charging = true;
    data.charging = true;
    data.remainingCapacity = 80;

    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());

    // Charging flag ignored when at 100% on AC
    data.remainingCapacity = 100;
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_multiple_alarm_flags(void) {
    UPSData data;
    data.has.acPresent = true;
    data.acPresent = true;
    data.has.overload = true;
    data.overload = true;
    data.has.needReplacement = true;
    data.needReplacement = true;

    TEST_ASSERT_EQUAL_STRING("OL RB OVER", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_shutdown_imminent_and_comm_lost(void) {
    UPSData data;
    data.has.shutdownImminent = true;
    data.shutdownImminent = true;
    data.has.communicationLost = true;
    data.communicationLost = true;

    TEST_ASSERT_EQUAL_STRING("FSD COMM_LOST", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_empty_data_returns_unknown(void) {
    UPSData data;
    TEST_ASSERT_EQUAL_STRING("Unknown", UPSData::computeUPSStatusString(data).c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_status_online_normal);
    RUN_TEST(test_status_on_battery_discharging);
    RUN_TEST(test_status_on_battery_low_battery);
    RUN_TEST(test_status_online_charging);
    RUN_TEST(test_status_multiple_alarm_flags);
    RUN_TEST(test_status_shutdown_imminent_and_comm_lost);
    RUN_TEST(test_status_empty_data_returns_unknown);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_status_online_normal);
    RUN_TEST(test_status_on_battery_discharging);
    RUN_TEST(test_status_on_battery_low_battery);
    RUN_TEST(test_status_online_charging);
    RUN_TEST(test_status_multiple_alarm_flags);
    RUN_TEST(test_status_shutdown_imminent_and_comm_lost);
    RUN_TEST(test_status_empty_data_returns_unknown);
    UNITY_END();
}
void loop() {}
#endif
#endif

