#include <unity.h>
#include "UPSData.h"

void setUp(void) {}
void tearDown(void) {}

void test_status_online_normal(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.discharging", "0");

    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_on_battery_discharging(void) {
    UPSData data;
    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "1");

    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_on_battery_low_battery(void) {
    UPSData data;
    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "1");
    data.set("ups.status.battery_low", "1");

    TEST_ASSERT_EQUAL_STRING("OB DISCHRG LB", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_online_charging(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.charging", "1");
    data.set("battery.charge", "80");

    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());

    // Charging flag ignored when at 100% on AC
    data.set("battery.charge", "100");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_multiple_alarm_flags(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.overload", "1");
    data.set("ups.status.replace_battery", "1");

    TEST_ASSERT_EQUAL_STRING("OL RB OVER", UPSData::computeUPSStatusString(data).c_str());
}

// ShutdownImminent gives LB, never FSD; CommunicationLost gives no token (issue #65)
void test_status_shutdown_imminent_and_comm_lost(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.shutdown_imminent", "1");
    data.set("ups.status.comm_lost", "1");
    TEST_ASSERT_EQUAL_STRING("OL LB", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "1");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG LB", UPSData::computeUPSStatusString(data).c_str());

    // No duplicate LB together with lowbatt and timelimitexp
    data.set("ups.status.battery_low", "1");
    data.set("ups.status.remaining_time_limit_expired", "1");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG LB", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.shutdown_imminent", "0");
    data.set("ups.status.battery_low", "0");
    data.set("ups.status.remaining_time_limit_expired", "0");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_comm_lost_alone_gives_no_token(void) {
    UPSData data;
    data.set("ups.status.comm_lost", "1");
    TEST_ASSERT_EQUAL_STRING("Unknown", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.ac_present", "1");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_empty_data_returns_unknown(void) {
    UPSData data;
    TEST_ASSERT_EQUAL_STRING("Unknown", UPSData::computeUPSStatusString(data).c_str());
}

// Eaton keeps PresentStatus.Good at 1 on battery: it was reported as "OL OB"
void test_status_eaton_on_battery_with_good(void) {
    UPSData data;
    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "1");
    data.set("ups.status.good", "1");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_eaton_online_with_good(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.discharging", "0");
    data.set("ups.status.good", "1");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_ac_lost_before_discharging_flag(void) {
    // ACPresent and Discharging may come from different reports: no power is OB already
    UPSData data;
    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "0");
    data.set("ups.status.good", "1");
    TEST_ASSERT_EQUAL_STRING("OB", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_discharging_wins_over_stale_ac_present(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.discharging", "1");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_good_only_device(void) {
    // No ACPresent: Good stands in for it, but never while discharging
    UPSData data;
    data.set("ups.status.good", "1");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
    data.set("ups.status.discharging", "1");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

// --- Upstream ups_status_set() semantics for the extended PresentStatus flags ---

// RemainingTimeLimitExpired triggers LB, together with lowbatt and ShutdownImminent.
void test_status_timelimit_expired_triggers_lb(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.remaining_time_limit_expired", "1");
    TEST_ASSERT_EQUAL_STRING("OL LB", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "1");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG LB", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_low_battery_and_timelimit_together(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.battery_low", "1");
    data.set("ups.status.remaining_time_limit_expired", "1");
    TEST_ASSERT_EQUAL_STRING("OL LB", UPSData::computeUPSStatusString(data).c_str());
}

// FullyDischarged suppresses DISCHRG but leaves OB.
void test_status_depleted_suppresses_dischrg(void) {
    UPSData data;
    data.set("ups.status.ac_present", "0");
    data.set("ups.status.discharging", "1");
    data.set("ups.status.depleted", "1");
    TEST_ASSERT_EQUAL_STRING("OB", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.depleted", "0");
    TEST_ASSERT_EQUAL_STRING("OB DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

// BatteryPresent = 0 triggers RB, just like ReplaceBattery.
void test_status_no_battery_triggers_rb(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.no_battery", "1");
    TEST_ASSERT_EQUAL_STRING("OL RB", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.no_battery", "0");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

// FullyCharged qualifies an active charging state: 1 hides CHRG (fullycharged),
// 0 shows it (notfullycharged).
void test_status_fully_charged_controls_chrg(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.charging", "1");
    data.set("battery.charge", "100");

    data.set("ups.status.fully_charged", "1");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.fully_charged", "0");
    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());
}

// Neither flag reported: fall back to battery.charge in (0, 100), as upstream.
void test_status_chrg_falls_back_to_battery_charge(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.charging", "1");

    data.set("battery.charge", "80");
    TEST_ASSERT_EQUAL_STRING("OL CHRG", UPSData::computeUPSStatusString(data).c_str());

    data.set("battery.charge", "100");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());

    data.set("battery.charge", "0");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

// FullyCharged = 0 qualifies an active charging state; it does not create one.
void test_status_notfully_charged_without_charging(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.charging", "0");
    data.set("ups.status.fully_charged", "0");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
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
    RUN_TEST(test_status_comm_lost_alone_gives_no_token);
    RUN_TEST(test_status_empty_data_returns_unknown);
    RUN_TEST(test_status_eaton_on_battery_with_good);
    RUN_TEST(test_status_eaton_online_with_good);
    RUN_TEST(test_status_ac_lost_before_discharging_flag);
    RUN_TEST(test_status_discharging_wins_over_stale_ac_present);
    RUN_TEST(test_status_good_only_device);
    RUN_TEST(test_status_timelimit_expired_triggers_lb);
    RUN_TEST(test_status_low_battery_and_timelimit_together);
    RUN_TEST(test_status_depleted_suppresses_dischrg);
    RUN_TEST(test_status_no_battery_triggers_rb);
    RUN_TEST(test_status_fully_charged_controls_chrg);
    RUN_TEST(test_status_chrg_falls_back_to_battery_charge);
    RUN_TEST(test_status_notfully_charged_without_charging);
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
    RUN_TEST(test_status_comm_lost_alone_gives_no_token);
    RUN_TEST(test_status_empty_data_returns_unknown);
    RUN_TEST(test_status_eaton_on_battery_with_good);
    RUN_TEST(test_status_eaton_online_with_good);
    RUN_TEST(test_status_ac_lost_before_discharging_flag);
    RUN_TEST(test_status_discharging_wins_over_stale_ac_present);
    RUN_TEST(test_status_good_only_device);
    RUN_TEST(test_status_timelimit_expired_triggers_lb);
    RUN_TEST(test_status_low_battery_and_timelimit_together);
    RUN_TEST(test_status_depleted_suppresses_dischrg);
    RUN_TEST(test_status_no_battery_triggers_rb);
    RUN_TEST(test_status_fully_charged_controls_chrg);
    RUN_TEST(test_status_chrg_falls_back_to_battery_charge);
    RUN_TEST(test_status_notfully_charged_without_charging);
    UNITY_END();
}
void loop() {}
#endif
#endif

