#include <unity.h>
#include "ReconnectBridge.h"

// US-140 (issue #36): while the same UPS drops off the bus and comes back, the last values
// stay served until the first full poll, as usbhid-ups and upsd do

typedef ReconnectBridge B;

void setUp(void) {}
void tearDown(void) {}

static const UpsIdentity POWERCOM = UpsIdentity::make(0x0d9f, 0x0004, "004-0D9F-000", "HID UPS Battery");

// --- Identity ---

void test_same_serial_is_the_same_ups(void) {
    UpsIdentity again = UpsIdentity::make(0x0d9f, 0x0004, "004-0D9F-000", "HID UPS Battery");
    TEST_ASSERT_TRUE(B::sameUps(POWERCOM, again));
}

void test_other_serial_is_another_ups(void) {
    UpsIdentity other = UpsIdentity::make(0x0d9f, 0x0004, "004-0D9F-001", "HID UPS Battery");
    TEST_ASSERT_FALSE(B::sameUps(POWERCOM, other));
}

void test_serial_on_one_side_only_is_another_ups(void) {
    UpsIdentity no_serial = UpsIdentity::make(0x0d9f, 0x0004, "", "HID UPS Battery");
    TEST_ASSERT_FALSE(B::sameUps(POWERCOM, no_serial));
    TEST_ASSERT_FALSE(B::sameUps(no_serial, POWERCOM));
}

void test_without_serials_the_product_decides(void) {
    UpsIdentity a = UpsIdentity::make(0x0463, 0xffff, nullptr, "Eaton 3S");
    UpsIdentity b = UpsIdentity::make(0x0463, 0xffff, "", "Eaton 3S");
    UpsIdentity c = UpsIdentity::make(0x0463, 0xffff, nullptr, "Ellipse ECO");
    TEST_ASSERT_TRUE(B::sameUps(a, b));
    TEST_ASSERT_FALSE(B::sameUps(a, c));
}

void test_other_vid_or_pid_is_another_ups(void) {
    UpsIdentity other_pid = UpsIdentity::make(0x0d9f, 0x0005, "004-0D9F-000", "HID UPS Battery");
    UpsIdentity other_vid = UpsIdentity::make(0x0d9e, 0x0004, "004-0D9F-000", "HID UPS Battery");
    TEST_ASSERT_FALSE(B::sameUps(POWERCOM, other_pid));
    TEST_ASSERT_FALSE(B::sameUps(POWERCOM, other_vid));
}

void test_invalid_identity_matches_nothing(void) {
    UpsIdentity none;
    TEST_ASSERT_FALSE(B::sameUps(none, none));
    TEST_ASSERT_FALSE(B::sameUps(POWERCOM, none));
}

void test_blank_serial_counts_as_none(void) {
    UpsIdentity blank = UpsIdentity::make(0x051d, 0x0002, "Blank", "Back-UPS");
    UpsIdentity none = UpsIdentity::make(0x051d, 0x0002, nullptr, "Back-UPS");
    TEST_ASSERT_EQUAL_STRING("", blank.serial);
    TEST_ASSERT_TRUE(B::sameUps(blank, none));
}

void test_long_strings_are_cut(void) {
    char longer[100];
    for (int i = 0; i < 99; i++) longer[i] = 'x';
    longer[99] = '\0';
    UpsIdentity id = UpsIdentity::make(1, 2, longer, longer);
    TEST_ASSERT_EQUAL_UINT32(UpsIdentity::STRING_LEN, strlen(id.serial));
    TEST_ASSERT_EQUAL_UINT32(UpsIdentity::STRING_LEN, strlen(id.product));
}

// --- Bridge ---

void test_happy_path_keeps_values_until_the_first_full_poll(void) {
    B b;
    TEST_ASSERT_TRUE(b.onDisconnected(0, true, POWERCOM));
    TEST_ASSERT_TRUE(b.serving(0));
    b.onEnumerated(1000);
    TEST_ASSERT_TRUE(b.state() == B::State::WAITING_CLAIM);
    TEST_ASSERT_TRUE(b.onClaim(1100, POWERCOM) == B::Claim::KEEP_VALUES);
    TEST_ASSERT_TRUE(b.state() == B::State::WAITING_DATA);
    TEST_ASSERT_TRUE(b.serving(18000));
    TEST_ASSERT_TRUE(b.tick(18000) == B::Expiry::NONE);
    uint32_t ms = 0;
    TEST_ASSERT_TRUE(b.onFirstData(19000, ms));
    TEST_ASSERT_EQUAL_UINT32(19000, ms);
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_FALSE(b.serving(19000));
}

void test_no_enumeration_within_the_window_drops_the_values(void) {
    B b;
    TEST_ASSERT_TRUE(b.onDisconnected(1000, true, POWERCOM));
    TEST_ASSERT_TRUE(b.serving(1000 + B::ENUM_WINDOW_MS - 1));
    TEST_ASSERT_FALSE(b.serving(1000 + B::ENUM_WINDOW_MS));
    TEST_ASSERT_TRUE(b.tick(1000 + B::ENUM_WINDOW_MS - 1) == B::Expiry::NONE);
    TEST_ASSERT_TRUE(b.tick(1000 + B::ENUM_WINDOW_MS) == B::Expiry::DROP_VALUES);
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_TRUE(b.tick(1000 + B::ENUM_WINDOW_MS + 1) == B::Expiry::NONE);
}

void test_enumeration_without_claim_drops_the_values_at_the_limit(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    b.onEnumerated(1000);
    TEST_ASSERT_TRUE(b.serving(B::MAX_BRIDGE_MS - 1));
    TEST_ASSERT_FALSE(b.serving(B::MAX_BRIDGE_MS));
    TEST_ASSERT_TRUE(b.tick(B::MAX_BRIDGE_MS) == B::Expiry::DROP_VALUES);
}

void test_claim_of_another_ups_drops_the_values(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    b.onEnumerated(800);
    UpsIdentity other = UpsIdentity::make(0x0463, 0xffff, "G123", "Eaton 3S");
    TEST_ASSERT_TRUE(b.onClaim(900, other) == B::Claim::DROP_VALUES);
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_TRUE(b.tick(1000) == B::Expiry::NONE);
}

void test_claim_without_bridge_is_not_bridging(void) {
    B b;
    TEST_ASSERT_TRUE(b.onClaim(0, POWERCOM) == B::Claim::NOT_BRIDGING);
    TEST_ASSERT_FALSE(b.active());
}

void test_claim_after_the_window_before_tick_drops_the_values(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    // CONNECTED missed: the claim comes straight from WAITING_ENUM, too late
    TEST_ASSERT_TRUE(b.onClaim(6000, POWERCOM) == B::Claim::DROP_VALUES);
    TEST_ASSERT_FALSE(b.active());
}

void test_claim_straight_from_waiting_enum_keeps_the_values(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    TEST_ASSERT_TRUE(b.onClaim(1500, POWERCOM) == B::Claim::KEEP_VALUES);
    TEST_ASSERT_TRUE(b.state() == B::State::WAITING_DATA);
}

void test_first_full_poll_too_late_turns_stale_and_keeps_the_values(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    b.onEnumerated(1000);
    b.onClaim(1000, POWERCOM);
    TEST_ASSERT_TRUE(b.serving(B::MAX_BRIDGE_MS - 1));
    TEST_ASSERT_FALSE(b.serving(B::MAX_BRIDGE_MS));
    TEST_ASSERT_TRUE(b.tick(B::MAX_BRIDGE_MS) == B::Expiry::STALE_UNTIL_FIRST_DATA);
    uint32_t ms = 0;
    TEST_ASSERT_FALSE(b.onFirstData(B::MAX_BRIDGE_MS + 5000, ms));
}

void test_drop_off_during_the_bridge_does_not_extend_the_limit(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    b.onEnumerated(1000);
    b.onClaim(1000, POWERCOM);
    // Gone again at 20 s: the data is not fresh (first poll not done), the bridge goes on
    TEST_ASSERT_TRUE(b.onDisconnected(20000, false, UpsIdentity()));
    TEST_ASSERT_TRUE(b.state() == B::State::WAITING_ENUM);
    TEST_ASSERT_TRUE(b.serving(24999));
    TEST_ASSERT_FALSE(b.serving(25000));
    // Back, and gone again at 28 s: only 2 s left before the limit
    b.onEnumerated(21000);
    b.onClaim(21000, POWERCOM);
    TEST_ASSERT_TRUE(b.onDisconnected(28000, false, UpsIdentity()));
    TEST_ASSERT_TRUE(b.serving(B::MAX_BRIDGE_MS - 1));
    TEST_ASSERT_FALSE(b.serving(B::MAX_BRIDGE_MS));
    TEST_ASSERT_TRUE(b.tick(B::MAX_BRIDGE_MS) == B::Expiry::DROP_VALUES);
}

void test_drop_off_after_the_limit_before_tick_is_not_served(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    b.onClaim(1000, POWERCOM);
    TEST_ASSERT_TRUE(b.onDisconnected(B::MAX_BRIDGE_MS + 10, false, UpsIdentity()));
    TEST_ASSERT_FALSE(b.serving(B::MAX_BRIDGE_MS + 10));
    TEST_ASSERT_TRUE(b.tick(B::MAX_BRIDGE_MS + 10) == B::Expiry::DROP_VALUES);
}

void test_stale_data_or_unknown_ups_does_not_start(void) {
    B b;
    TEST_ASSERT_FALSE(b.onDisconnected(0, false, POWERCOM));
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_FALSE(b.onDisconnected(0, true, UpsIdentity()));
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_FALSE(b.serving(0));
}

void test_enumeration_without_bridge_does_nothing(void) {
    B b;
    b.onEnumerated(100);
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_TRUE(b.tick(100) == B::Expiry::NONE);
}

void test_reset_ends_the_bridge(void) {
    B b;
    b.onDisconnected(0, true, POWERCOM);
    b.reset();
    TEST_ASSERT_FALSE(b.active());
    TEST_ASSERT_FALSE(b.serving(0));
    TEST_ASSERT_TRUE(b.onClaim(100, POWERCOM) == B::Claim::NOT_BRIDGING);
}

void test_bridge_across_millis_wrap(void) {
    B b;
    const uint32_t t0 = 0xFFFFF000u;
    TEST_ASSERT_TRUE(b.onDisconnected(t0, true, POWERCOM));
    b.onEnumerated(t0 + 1000);
    TEST_ASSERT_TRUE(b.onClaim(t0 + 1200, POWERCOM) == B::Claim::KEEP_VALUES);
    TEST_ASSERT_TRUE(b.serving(t0 + B::MAX_BRIDGE_MS - 1));
    TEST_ASSERT_FALSE(b.serving(t0 + B::MAX_BRIDGE_MS));
    uint32_t ms = 0;
    TEST_ASSERT_TRUE(b.onFirstData(t0 + 19000, ms));
    TEST_ASSERT_EQUAL_UINT32(19000, ms);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_same_serial_is_the_same_ups);
    RUN_TEST(test_other_serial_is_another_ups);
    RUN_TEST(test_serial_on_one_side_only_is_another_ups);
    RUN_TEST(test_without_serials_the_product_decides);
    RUN_TEST(test_other_vid_or_pid_is_another_ups);
    RUN_TEST(test_invalid_identity_matches_nothing);
    RUN_TEST(test_blank_serial_counts_as_none);
    RUN_TEST(test_long_strings_are_cut);
    RUN_TEST(test_happy_path_keeps_values_until_the_first_full_poll);
    RUN_TEST(test_no_enumeration_within_the_window_drops_the_values);
    RUN_TEST(test_enumeration_without_claim_drops_the_values_at_the_limit);
    RUN_TEST(test_claim_of_another_ups_drops_the_values);
    RUN_TEST(test_claim_without_bridge_is_not_bridging);
    RUN_TEST(test_claim_after_the_window_before_tick_drops_the_values);
    RUN_TEST(test_claim_straight_from_waiting_enum_keeps_the_values);
    RUN_TEST(test_first_full_poll_too_late_turns_stale_and_keeps_the_values);
    RUN_TEST(test_drop_off_during_the_bridge_does_not_extend_the_limit);
    RUN_TEST(test_drop_off_after_the_limit_before_tick_is_not_served);
    RUN_TEST(test_stale_data_or_unknown_ups_does_not_start);
    RUN_TEST(test_enumeration_without_bridge_does_nothing);
    RUN_TEST(test_reset_ends_the_bridge);
    RUN_TEST(test_bridge_across_millis_wrap);
    return UNITY_END();
}
