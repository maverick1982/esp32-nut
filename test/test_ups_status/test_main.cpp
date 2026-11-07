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

    // ups_status_set() writes OVER before RB (US-107: before it was "OL RB OVER")
    TEST_ASSERT_EQUAL_STRING("OL OVER RB", UPSData::computeUPSStatusString(data).c_str());
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

// ACPresent and Discharging together: the default branch of usbhid-ups ups_status_set()
// gives OL plus DISCHRG (APC BGM2200 on mains at 100 %, issue #98)
void test_status_ac_present_while_discharging_is_ol_dischrg(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.discharging", "1");
    TEST_ASSERT_EQUAL_STRING("OL DISCHRG", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_ol_dischrg_depleted(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.discharging", "1");
    data.set("ups.status.depleted", "1");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_ol_dischrg_low_battery(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.discharging", "1");
    data.set("ups.status.battery_low", "1");
    TEST_ASSERT_EQUAL_STRING("OL DISCHRG LB", UPSData::computeUPSStatusString(data).c_str());
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

// --- US-107: token order of ups_status_set(), CAL/TRIM/BOOST/BYPASS/OFF, ALARM ---

// Calibration is reported first (usbhid-ups.c:2703-2711)
void test_status_cal_comes_first(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.cal", "1");
    TEST_ASSERT_EQUAL_STRING("CAL OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_trim_and_boost(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.trim", "1");
    TEST_ASSERT_EQUAL_STRING("OL TRIM", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.trim", "0");
    data.set("ups.status.boost", "1");
    TEST_ASSERT_EQUAL_STRING("OL BOOST", UPSData::computeUPSStatusString(data).c_str());
}

// BYPASSAUTO | BYPASSMAN give one BYPASS token
void test_status_bypass_once(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.bypassauto", "1");
    TEST_ASSERT_EQUAL_STRING("OL BYPASS", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.bypassman", "1");
    TEST_ASSERT_EQUAL_STRING("OL BYPASS", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.bypassauto", "0");
    TEST_ASSERT_EQUAL_STRING("OL BYPASS", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_off(void) {
    UPSData data;
    data.set("ups.status.off", "1");
    TEST_ASSERT_EQUAL_STRING("OFF", UPSData::computeUPSStatusString(data).c_str());

    data.set("ups.status.ac_present", "1");
    TEST_ASSERT_EQUAL_STRING("OL OFF", UPSData::computeUPSStatusString(data).c_str());
}

// Every token after OL in the order of ups_status_set()
void test_status_full_upstream_order(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.off", "1");
    data.set("ups.status.bypassman", "1");
    data.set("ups.status.trim", "1");
    data.set("ups.status.replace_battery", "1");
    data.set("ups.status.overload", "1");
    TEST_ASSERT_EQUAL_STRING("OL OVER RB TRIM BYPASS OFF", UPSData::computeUPSStatusString(data).c_str());
}

// ups_status_set() writes CHRG before LB (US-107: before it was "OL LB CHRG")
void test_status_chrg_before_lb(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.charging", "1");
    data.set("ups.status.battery_low", "1");
    data.set("battery.charge", "20");
    TEST_ASSERT_EQUAL_STRING("OL CHRG LB", UPSData::computeUPSStatusString(data).c_str());
}

// ECO and ESS go to experimental.ups.mode.buzzwords upstream, not to ups.status
void test_status_eco_ess_give_no_token(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.ecomode", "1");
    data.set("ups.status.essmode", "1");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_new_keys_at_zero_give_no_token(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    const char* keys[] = { "ups.status.cal", "ups.status.trim", "ups.status.boost", "ups.status.bypassauto",
                           "ups.status.bypassman", "ups.status.off" };
    for (const char* k : keys) data.set(k, "0");
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(data).c_str());
}

// ALARM leads when ups.alarm exists, as status_commit() (dstate.c:2315-2321)
void test_status_alarm_prefix(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.replace_battery", "1");
    data.set("ups.alarm", "Replace battery!");
    TEST_ASSERT_EQUAL_STRING("ALARM OL RB", UPSData::computeUPSStatusString(data).c_str());

    data.remove("ups.alarm");
    TEST_ASSERT_EQUAL_STRING("OL RB", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_alarm_alone(void) {
    UPSData data;
    data.set("ups.alarm", "Emergency stop!");
    TEST_ASSERT_EQUAL_STRING("ALARM", UPSData::computeUPSStatusString(data).c_str());

    data.remove("ups.alarm");
    TEST_ASSERT_EQUAL_STRING("Unknown", UPSData::computeUPSStatusString(data).c_str());
}

// --- US-107: ups.alarm and input.transfer.reason (ups_alarm_set(), ups_status_set()) ---

static String alarmOf(const UPSData& d, const char* vendor = "") {
    String out;
    UPSData::alarmText(d, String(vendor), out);
    return out;
}

void test_alarm_none_writes_nothing(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    uint32_t before = data.writes();
    data.applyAlarmVariables(String());
    data.applyAlarmVariables(String());
    TEST_ASSERT_FALSE(data.hasKey("ups.alarm"));
    TEST_ASSERT_FALSE(data.hasKey("input.transfer.reason"));
    TEST_ASSERT_EQUAL_UINT32(before, data.writes());
}

// The order is the one of ups_alarm_set(), not the order the keys were written in
void test_alarm_upstream_order(void) {
    UPSData data;
    data.set("ups.status.shutdown_imminent", "1");
    data.set("ups.status.replace_battery", "1");
    TEST_ASSERT_EQUAL_STRING("Replace battery! Shutdown imminent!", alarmOf(data).c_str());
}

void test_alarm_all_bits(void) {
    UPSData data;
    const char* keys[] = { "ups.status.bypassman", "ups.status.bypassauto", "ups.status.awaitingpower",
                           "ups.status.commfault", "ups.status.overheat", "ups.status.chargerfail",
                           "ups.status.battvolthi", "ups.status.battvoltlo", "ups.status.no_battery",
                           "ups.status.fanfail", "ups.status.shutdown_imminent", "ups.status.replace_battery" };
    for (const char* k : keys) data.set(k, "1");
    TEST_ASSERT_EQUAL_STRING("Replace battery! Shutdown imminent! Fan failure! No battery installed! "
                             "Battery voltage too low! Battery voltage too high! Battery charger fail! "
                             "Temperature too high! Internal UPS fault! Awaiting power! "
                             "Automatic bypass mode! Manual bypass mode!",
                             alarmOf(data).c_str());
}

// The vendor ups.alarm rows are applied during the walk, before ups_alarm_set()
void test_alarm_vendor_first(void) {
    UPSData data;
    data.set("ups.status.replace_battery", "1");
    TEST_ASSERT_EQUAL_STRING("Emergency stop! Replace battery!", alarmOf(data, "Emergency stop!").c_str());
    data.set("ups.status.replace_battery", "0");
    TEST_ASSERT_EQUAL_STRING("Emergency stop!", alarmOf(data, "Emergency stop!").c_str());
}

// Hand-written keys not yet mapped to commfault give no alarm (US-107 planning)
void test_alarm_comm_lost_and_internal_failure_excluded(void) {
    UPSData data;
    data.set("ups.status.comm_lost", "1");
    data.set("ups.status.internal_failure", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_FALSE(data.hasKey("ups.alarm"));
}

void test_alarm_published_and_removed(void) {
    UPSData data;
    data.set("ups.status.replace_battery", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("Replace battery!", data.get("ups.alarm").c_str());

    // Same state: nothing written again
    uint32_t before = data.writes();
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_UINT32(before, data.writes());

    data.set("ups.status.replace_battery", "0");
    data.applyAlarmVariables(String());
    TEST_ASSERT_FALSE(data.hasKey("ups.alarm"));
}

// VRANGE wins over FRANGE (usbhid-ups.c:2695-2701)
void test_transfer_reason(void) {
    UPSData data;
    data.set("ups.status.vrange", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("input voltage out of range", data.get("input.transfer.reason").c_str());

    data.set("ups.status.frange", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("input voltage out of range", data.get("input.transfer.reason").c_str());

    uint32_t before = data.writes();
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_UINT32(before, data.writes());

    data.set("ups.status.vrange", "0");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("input frequency out of range", data.get("input.transfer.reason").c_str());

    data.set("ups.status.frange", "0");
    data.applyAlarmVariables(String());
    TEST_ASSERT_FALSE(data.hasKey("input.transfer.reason"));
    // Neither condition is an alarm
    TEST_ASSERT_FALSE(data.hasKey("ups.alarm"));
}

void test_alarm_gives_alarm_status(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.replace_battery", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("ALARM OL RB", UPSData::computeUPSStatusString(data).c_str());
}

// --- Q* bits (US-133): BYPASS and FSD of nutdrv_qx ups_status_set()/ups_alarm_set() ---

// BYPASS of nutdrv_qx: the token without the bypass mode alarms of the HID flags
void test_status_qx_bypass_without_alarm(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.bypass", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_NULL(data.find("ups.alarm"));
    TEST_ASSERT_EQUAL_STRING("OL BYPASS", UPSData::computeUPSStatusString(data).c_str());
}

// FSD of nutdrv_qx: token FSD (last) and alarm "Shutdown imminent!", no LB
void test_status_qx_fsd_token_and_alarm(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.fsd", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("Shutdown imminent!", data.get("ups.alarm").c_str());
    TEST_ASSERT_EQUAL_STRING("ALARM OL FSD", UPSData::computeUPSStatusString(data).c_str());
}

void test_status_qx_fsd_on_battery_low(void) {
    UPSData data;
    data.set("ups.status.ac_present", "0");
    data.set("ups.status.battery_low", "1");
    data.set("ups.status.fsd", "1");
    data.applyAlarmVariables(String());
    TEST_ASSERT_EQUAL_STRING("ALARM OB LB FSD", UPSData::computeUPSStatusString(data).c_str());
}

// The engine writes every bit as 1 or 0: the Q* keys at 0 give nothing
void test_status_qx_keys_at_zero(void) {
    UPSData data;
    data.set("ups.status.ac_present", "1");
    data.set("ups.status.bypass", "0");
    data.set("ups.status.fsd", "0");
    data.applyAlarmVariables(String());
    TEST_ASSERT_NULL(data.find("ups.alarm"));
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
    RUN_TEST(test_status_ac_present_while_discharging_is_ol_dischrg);
    RUN_TEST(test_status_ol_dischrg_depleted);
    RUN_TEST(test_status_ol_dischrg_low_battery);
    RUN_TEST(test_status_good_only_device);
    RUN_TEST(test_status_timelimit_expired_triggers_lb);
    RUN_TEST(test_status_low_battery_and_timelimit_together);
    RUN_TEST(test_status_depleted_suppresses_dischrg);
    RUN_TEST(test_status_no_battery_triggers_rb);
    RUN_TEST(test_status_fully_charged_controls_chrg);
    RUN_TEST(test_status_chrg_falls_back_to_battery_charge);
    RUN_TEST(test_status_notfully_charged_without_charging);
    RUN_TEST(test_status_cal_comes_first);
    RUN_TEST(test_status_trim_and_boost);
    RUN_TEST(test_status_bypass_once);
    RUN_TEST(test_status_off);
    RUN_TEST(test_status_full_upstream_order);
    RUN_TEST(test_status_chrg_before_lb);
    RUN_TEST(test_status_eco_ess_give_no_token);
    RUN_TEST(test_status_new_keys_at_zero_give_no_token);
    RUN_TEST(test_status_alarm_prefix);
    RUN_TEST(test_status_alarm_alone);
    RUN_TEST(test_alarm_none_writes_nothing);
    RUN_TEST(test_alarm_upstream_order);
    RUN_TEST(test_alarm_all_bits);
    RUN_TEST(test_alarm_vendor_first);
    RUN_TEST(test_alarm_comm_lost_and_internal_failure_excluded);
    RUN_TEST(test_alarm_published_and_removed);
    RUN_TEST(test_transfer_reason);
    RUN_TEST(test_alarm_gives_alarm_status);
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
    RUN_TEST(test_status_ac_present_while_discharging_is_ol_dischrg);
    RUN_TEST(test_status_ol_dischrg_depleted);
    RUN_TEST(test_status_ol_dischrg_low_battery);
    RUN_TEST(test_status_good_only_device);
    RUN_TEST(test_status_timelimit_expired_triggers_lb);
    RUN_TEST(test_status_low_battery_and_timelimit_together);
    RUN_TEST(test_status_depleted_suppresses_dischrg);
    RUN_TEST(test_status_no_battery_triggers_rb);
    RUN_TEST(test_status_fully_charged_controls_chrg);
    RUN_TEST(test_status_chrg_falls_back_to_battery_charge);
    RUN_TEST(test_status_notfully_charged_without_charging);
    RUN_TEST(test_status_cal_comes_first);
    RUN_TEST(test_status_trim_and_boost);
    RUN_TEST(test_status_bypass_once);
    RUN_TEST(test_status_off);
    RUN_TEST(test_status_full_upstream_order);
    RUN_TEST(test_status_chrg_before_lb);
    RUN_TEST(test_status_eco_ess_give_no_token);
    RUN_TEST(test_status_new_keys_at_zero_give_no_token);
    RUN_TEST(test_status_alarm_prefix);
    RUN_TEST(test_status_alarm_alone);
    RUN_TEST(test_alarm_none_writes_nothing);
    RUN_TEST(test_alarm_upstream_order);
    RUN_TEST(test_alarm_all_bits);
    RUN_TEST(test_alarm_vendor_first);
    RUN_TEST(test_alarm_comm_lost_and_internal_failure_excluded);
    RUN_TEST(test_alarm_published_and_removed);
    RUN_TEST(test_transfer_reason);
    RUN_TEST(test_alarm_gives_alarm_status);
    RUN_TEST(test_status_qx_bypass_without_alarm);
    RUN_TEST(test_status_qx_fsd_token_and_alarm);
    RUN_TEST(test_status_qx_fsd_on_battery_low);
    RUN_TEST(test_status_qx_keys_at_zero);
    UNITY_END();
}
void loop() {}
#endif
#endif

