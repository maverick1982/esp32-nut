#include <unity.h>
#include "OnlineDischargeLog.h"

// Issue #98: the OL+DISCHRG warning follows the default throttle of usbhid-ups

void setUp(void) {}
void tearDown(void) {}

void test_first_message_under_full_charge(void) {
    OnlineDischargeLog l;
    TEST_ASSERT_TRUE(l.update(true, true, 95));
    TEST_ASSERT_EQUAL_INT(95, l.charge());
    TEST_ASSERT_EQUAL_INT(-1, l.previousCharge());
}

void test_quiet_while_charge_unchanged(void) {
    OnlineDischargeLog l;
    TEST_ASSERT_TRUE(l.update(true, true, 95));
    TEST_ASSERT_FALSE(l.update(true, true, 95));
    TEST_ASSERT_FALSE(l.update(true, true, 95));
}

void test_new_message_when_charge_changes(void) {
    OnlineDischargeLog l;
    TEST_ASSERT_TRUE(l.update(true, true, 95));
    TEST_ASSERT_TRUE(l.update(true, true, 94));
    TEST_ASSERT_EQUAL_INT(94, l.charge());
    TEST_ASSERT_EQUAL_INT(95, l.previousCharge());
}

void test_quiet_at_full_charge(void) {
    // onlinedischarge_log_throttle_hovercharge 100: the BGM2200 of the dump stays quiet
    OnlineDischargeLog l;
    TEST_ASSERT_FALSE(l.update(true, true, 100));
    TEST_ASSERT_FALSE(l.update(true, true, 100));
}

void test_quiet_without_battery_charge(void) {
    // Upstream's time throttle only starts after a first message, which needs the charge
    OnlineDischargeLog l;
    TEST_ASSERT_FALSE(l.update(true, true, -1));
    TEST_ASSERT_FALSE(l.update(true, true, 0));
}

void test_quiet_on_battery(void) {
    OnlineDischargeLog l;
    TEST_ASSERT_FALSE(l.update(false, true, 50));
}

void test_reset_when_discharging_drops(void) {
    OnlineDischargeLog l;
    TEST_ASSERT_TRUE(l.update(true, true, 95));
    TEST_ASSERT_TRUE(l.update(true, true, 94));
    TEST_ASSERT_FALSE(l.update(true, false, 94));
    // A new sequence: same charge as before, reported again as the first message
    TEST_ASSERT_TRUE(l.update(true, true, 94));
    TEST_ASSERT_EQUAL_INT(-1, l.previousCharge());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_first_message_under_full_charge);
    RUN_TEST(test_quiet_while_charge_unchanged);
    RUN_TEST(test_new_message_when_charge_changes);
    RUN_TEST(test_quiet_at_full_charge);
    RUN_TEST(test_quiet_without_battery_charge);
    RUN_TEST(test_quiet_on_battery);
    RUN_TEST(test_reset_when_discharging_drops);
    return UNITY_END();
}
