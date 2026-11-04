#include <unity.h>
#include <string>
#include <vector>
#include "NutOverrides.h"

// Custom variables of the NUT Server page (US-115): rules, lookup and saved form

using NutOverrides::Check;

void setUp(void) {}
void tearDown(void) {}

static NutOverride ov(const char* key, const char* value, const char* desc = "") {
    NutOverride o;
    o.key = key;
    o.value = value;
    o.description = desc;
    return o;
}

void test_valid_keys(void) {
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("battery.date"));
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("ups.model"));
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("outlet.1.desc"));
    // Upper case exists in NUT names
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("input.L1-N.voltage"));
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("experimental.my_value"));
    // The aliases device.* other than device.type can be overridden
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("device.model"));
    std::string longest(31, 'a');
    longest += '.';
    longest += std::string(31, 'b');
    TEST_ASSERT_EQUAL(63, (int)longest.size());
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey(longest.c_str()));
}

void test_invalid_keys(void) {
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey(""));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey(nullptr));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey("battery"));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey(".battery.date"));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey("battery.date."));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey("battery..date"));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey("battery date"));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey("battery.date\""));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey("battery.dat\xC3\xA8"));
    std::string too_long(32, 'a');
    too_long += '.';
    too_long += std::string(31, 'b');
    TEST_ASSERT_EQUAL(64, (int)too_long.size());
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkKey(too_long.c_str()));
}

void test_protected_keys_ignore_case(void) {
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("ups.status"));
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("UPS.Status"));
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("ups.status.charging"));
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("device.type"));
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("Device.Type"));
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("driver.parameter.override.x"));
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkKey("driver.parameter.pollinterval"));
    // Only the exact names and the prefixes: these are ordinary keys
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("ups.statusx"));
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkKey("driver.name"));
}

void test_value_and_description_limits(void) {
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkEntry(ov("battery.date", "")));
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkEntry(ov("battery.date", "a \"quoted\" \\ value #1")));
    std::string v64(64, 'x');
    std::string v65(65, 'x');
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkEntry(ov("battery.date", v64.c_str(), v64.c_str())));
    TEST_ASSERT_TRUE(Check::INVALID_VALUE == NutOverrides::checkEntry(ov("battery.date", v65.c_str())));
    TEST_ASSERT_TRUE(Check::INVALID_DESCRIPTION == NutOverrides::checkEntry(ov("battery.date", "1", v65.c_str())));
    TEST_ASSERT_TRUE(Check::INVALID_VALUE == NutOverrides::checkEntry(ov("battery.date", "a\tb")));
    TEST_ASSERT_TRUE(Check::INVALID_VALUE == NutOverrides::checkEntry(ov("battery.date", "caf\xC3\xA9")));
    TEST_ASSERT_TRUE(Check::INVALID_DESCRIPTION == NutOverrides::checkEntry(ov("battery.date", "1", "line\nbreak")));
    // The key is checked first
    TEST_ASSERT_TRUE(Check::PROTECTED_KEY == NutOverrides::checkEntry(ov("ups.status", v65.c_str())));
}

void test_check_list(void) {
    std::vector<NutOverride> list;
    size_t bad = 99;
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkList(list, bad));

    list.push_back(ov("battery.date", "2025-03-01"));
    list.push_back(ov("ups.model", "Studio"));
    list.push_back(ov("bad key", "1"));
    TEST_ASSERT_TRUE(Check::INVALID_KEY == NutOverrides::checkList(list, bad));
    TEST_ASSERT_EQUAL(2, (int)bad);

    list[2] = ov("UPS.MODEL", "Other");
    TEST_ASSERT_TRUE(Check::DUPLICATE_KEY == NutOverrides::checkList(list, bad));
    TEST_ASSERT_EQUAL(2, (int)bad);

    list.clear();
    for (int i = 0; i < 16; i++) list.push_back(ov((std::string("custom.v") + std::to_string(i)).c_str(), "1"));
    TEST_ASSERT_TRUE(Check::OK == NutOverrides::checkList(list, bad));
    list.push_back(ov("custom.v16", "1"));
    TEST_ASSERT_TRUE(Check::TOO_MANY == NutOverrides::checkList(list, bad));
    TEST_ASSERT_EQUAL(16, (int)bad);
}

void test_find_ignores_case(void) {
    std::vector<NutOverride> list;
    list.push_back(ov("battery.date", "2025-03-01"));
    list.push_back(ov("input.L1-N.voltage", "230"));
    TEST_ASSERT_NOT_NULL(NutOverrides::find(list, "BATTERY.DATE"));
    TEST_ASSERT_EQUAL_STRING("230", NutOverrides::find(list, "input.l1-n.voltage")->value.c_str());
    TEST_ASSERT_NULL(NutOverrides::find(list, "battery.dat"));
    TEST_ASSERT_NULL(NutOverrides::find((const std::vector<NutOverride>*)nullptr, "battery.date"));
    TEST_ASSERT_NOT_NULL(NutOverrides::find(&list, "battery.date"));
}

void test_json_round_trip(void) {
    std::vector<NutOverride> list;
    list.push_back(ov("battery.date", "2025-03-01", "Battery replaced"));
    list.push_back(ov("ups.model", "a \"quoted\" \\ value", ""));
    String json = NutOverrides::toJson(list);
    TEST_ASSERT_EQUAL_STRING(
        "[{\"k\":\"battery.date\",\"v\":\"2025-03-01\",\"d\":\"Battery replaced\"},"
        "{\"k\":\"ups.model\",\"v\":\"a \\\"quoted\\\" \\\\ value\",\"d\":\"\"}]",
        json.c_str());

    std::vector<NutOverride> back;
    TEST_ASSERT_TRUE(NutOverrides::fromJson(json, back));
    TEST_ASSERT_EQUAL(2, (int)back.size());
    TEST_ASSERT_EQUAL_STRING("battery.date", back[0].key.c_str());
    TEST_ASSERT_EQUAL_STRING("Battery replaced", back[0].description.c_str());
    TEST_ASSERT_EQUAL_STRING("a \"quoted\" \\ value", back[1].value.c_str());

    TEST_ASSERT_EQUAL_STRING("[]", NutOverrides::toJson(std::vector<NutOverride>()).c_str());
}

void test_json_corrupt_or_foreign(void) {
    std::vector<NutOverride> out;
    out.push_back(ov("x.y", "1"));
    TEST_ASSERT_FALSE(NutOverrides::fromJson("not json", out));
    TEST_ASSERT_EQUAL(0, (int)out.size());
    TEST_ASSERT_FALSE(NutOverrides::fromJson("{\"k\":\"battery.date\"}", out));
    TEST_ASSERT_EQUAL(0, (int)out.size());
    TEST_ASSERT_TRUE(NutOverrides::fromJson("[]", out));
    TEST_ASSERT_EQUAL(0, (int)out.size());
}

void test_json_drops_invalid_entries(void) {
    std::vector<NutOverride> out;
    // Protected key, value not a string, duplicate, missing description (kept, empty)
    TEST_ASSERT_FALSE(NutOverrides::fromJson(
        "[{\"k\":\"ups.status\",\"v\":\"OL\"},{\"k\":\"battery.date\",\"v\":1},"
        "{\"k\":\"ups.model\",\"v\":\"A\"},{\"k\":\"UPS.MODEL\",\"v\":\"B\"},{\"k\":\"battery.type\",\"v\":\"PbAc\"}]",
        out));
    TEST_ASSERT_EQUAL(2, (int)out.size());
    TEST_ASSERT_EQUAL_STRING("ups.model", out[0].key.c_str());
    TEST_ASSERT_EQUAL_STRING("A", out[0].value.c_str());
    TEST_ASSERT_EQUAL_STRING("", out[1].description.c_str());
}

void test_json_truncated_to_max(void) {
    std::vector<NutOverride> list;
    for (int i = 0; i < 17; i++) list.push_back(ov((std::string("custom.v") + std::to_string(i)).c_str(), "1"));
    std::vector<NutOverride> out;
    TEST_ASSERT_FALSE(NutOverrides::fromJson(NutOverrides::toJson(list), out));
    TEST_ASSERT_EQUAL(16, (int)out.size());
    TEST_ASSERT_EQUAL_STRING("custom.v15", out[15].key.c_str());
}

void test_error_messages(void) {
    TEST_ASSERT_EQUAL_STRING(
        "Invalid key \"bad key\": use dot-separated parts of letters, digits, '_' and '-' (e.g. battery.date)",
        NutOverrides::errorMessage(Check::INVALID_KEY, "bad key").c_str());
    TEST_ASSERT_EQUAL_STRING("ups.status cannot be overridden",
                             NutOverrides::errorMessage(Check::PROTECTED_KEY, "ups.status").c_str());
    TEST_ASSERT_EQUAL_STRING("Invalid value for ups.model: printable ASCII only, at most 64 characters",
                             NutOverrides::errorMessage(Check::INVALID_VALUE, "ups.model").c_str());
    TEST_ASSERT_EQUAL_STRING("Invalid description for ups.model: printable ASCII only, at most 64 characters",
                             NutOverrides::errorMessage(Check::INVALID_DESCRIPTION, "ups.model").c_str());
    TEST_ASSERT_EQUAL_STRING("Duplicate key: UPS.MODEL",
                             NutOverrides::errorMessage(Check::DUPLICATE_KEY, "UPS.MODEL").c_str());
    TEST_ASSERT_EQUAL_STRING("At most 16 custom variables", NutOverrides::errorMessage(Check::TOO_MANY, "").c_str());
    TEST_ASSERT_EQUAL_STRING("", NutOverrides::errorMessage(Check::OK, "x.y").c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_keys);
    RUN_TEST(test_invalid_keys);
    RUN_TEST(test_protected_keys_ignore_case);
    RUN_TEST(test_value_and_description_limits);
    RUN_TEST(test_check_list);
    RUN_TEST(test_find_ignores_case);
    RUN_TEST(test_json_round_trip);
    RUN_TEST(test_json_corrupt_or_foreign);
    RUN_TEST(test_json_drops_invalid_entries);
    RUN_TEST(test_json_truncated_to_max);
    RUN_TEST(test_error_messages);
    return UNITY_END();
}
