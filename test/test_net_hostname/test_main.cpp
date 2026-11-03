#include <unity.h>
#include <string>
#include "NetHostname.h"

// Hostname rule of the Network page and of the DHCP client (US-113)

static const uint8_t MAC[6] = { 0x3C, 0x84, 0x27, 0x56, 0xD7, 0x84 };

void setUp(void) {}
void tearDown(void) {}

void test_valid_names(void) {
    TEST_ASSERT_TRUE(NetHostname::isValid("espnut-network"));
    TEST_ASSERT_TRUE(NetHostname::isValid("a"));
    TEST_ASSERT_TRUE(NetHostname::isValid("A1"));
    TEST_ASSERT_TRUE(NetHostname::isValid("ESP32-NUT"));
    TEST_ASSERT_TRUE(NetHostname::isValid("a-b-c"));
    TEST_ASSERT_TRUE(NetHostname::isValid("0123456789"));
}

void test_32_characters_is_the_limit(void) {
    std::string name32(32, 'a');
    std::string name33(33, 'a');
    TEST_ASSERT_TRUE(NetHostname::isValid(name32.c_str()));
    TEST_ASSERT_FALSE(NetHostname::isValid(name33.c_str()));
}

void test_empty_and_null_are_not_valid(void) {
    TEST_ASSERT_FALSE(NetHostname::isValid(nullptr));
    TEST_ASSERT_FALSE(NetHostname::isValid(""));
}

void test_hyphen_at_start_or_end_is_refused(void) {
    TEST_ASSERT_FALSE(NetHostname::isValid("-abc"));
    TEST_ASSERT_FALSE(NetHostname::isValid("abc-"));
    TEST_ASSERT_FALSE(NetHostname::isValid("-"));
}

void test_other_characters_are_refused(void) {
    TEST_ASSERT_FALSE(NetHostname::isValid("a_b"));
    TEST_ASSERT_FALSE(NetHostname::isValid("a b"));
    TEST_ASSERT_FALSE(NetHostname::isValid("a.b"));
    TEST_ASSERT_FALSE(NetHostname::isValid(" ab"));
    TEST_ASSERT_FALSE(NetHostname::isValid("caff\xC3\xA8")); // "caffè" in UTF-8
}

void test_default_from_last_three_mac_bytes_lower_case(void) {
    TEST_ASSERT_EQUAL_STRING("esp32-nut-56d784", NetHostname::defaultFor(MAC).c_str());
}

void test_effective_uses_the_saved_name_when_valid(void) {
    TEST_ASSERT_EQUAL_STRING("espnut-network", NetHostname::effective(String("espnut-network"), MAC).c_str());
}

void test_effective_falls_back_to_the_default(void) {
    TEST_ASSERT_EQUAL_STRING("esp32-nut-56d784", NetHostname::effective(String(""), MAC).c_str());
    TEST_ASSERT_EQUAL_STRING("esp32-nut-56d784", NetHostname::effective(String("bad-"), MAC).c_str());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_names);
    RUN_TEST(test_32_characters_is_the_limit);
    RUN_TEST(test_empty_and_null_are_not_valid);
    RUN_TEST(test_hyphen_at_start_or_end_is_refused);
    RUN_TEST(test_other_characters_are_refused);
    RUN_TEST(test_default_from_last_three_mac_bytes_lower_case);
    RUN_TEST(test_effective_uses_the_saved_name_when_valid);
    RUN_TEST(test_effective_falls_back_to_the_default);
    return UNITY_END();
}
