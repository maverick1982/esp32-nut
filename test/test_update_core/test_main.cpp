#include <unity.h>
#include <string.h>
#include <string>
#include <ArduinoJson.h>
#include "FirmwareVersion.h"
#include "ReleaseInfo.h"
#include "UpdateSchedule.h"
#include "UpdateStatus.h"

void setUp(void) {}
void tearDown(void) {}

// --- Versions ---

void test_version_parse(void) {
    FirmwareVersion v;
    TEST_ASSERT_TRUE(parseFirmwareVersion("v1.16.0", v));
    TEST_ASSERT_EQUAL_UINT32(1, v.major);
    TEST_ASSERT_EQUAL_UINT32(16, v.minor);
    TEST_ASSERT_EQUAL_UINT32(0, v.patch);
    TEST_ASSERT_FALSE(v.prerelease);
    TEST_ASSERT_TRUE(parseFirmwareVersion("v2.0.1-rc1", v));
    TEST_ASSERT_TRUE(v.prerelease);
    TEST_ASSERT_FALSE(parseFirmwareVersion("1.16.0", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.16.0-beta", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.16.0-rc", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.16.0-rc1x", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.16.0-3-gabc123", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("update-test", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("dev", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.16", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("v1.16.0.1", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("feature-x", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion("", v));
    TEST_ASSERT_FALSE(parseFirmwareVersion(nullptr, v));
}

void test_version_compare(void) {
    FirmwareVersion a, b;
    parseFirmwareVersion("v1.16.0", a);
    parseFirmwareVersion("v1.17.0", b);
    TEST_ASSERT_TRUE(compareFirmwareVersions(a, b) < 0);
    parseFirmwareVersion("v1.16.10", b);
    TEST_ASSERT_TRUE(compareFirmwareVersions(a, b) < 0);
    parseFirmwareVersion("v1.16.0-rc1", b);
    TEST_ASSERT_TRUE(compareFirmwareVersions(b, a) < 0);
    parseFirmwareVersion("v1.16.0", b);
    TEST_ASSERT_EQUAL(0, compareFirmwareVersions(a, b));
}

void test_update_available_and_installable(void) {
    TEST_ASSERT_TRUE(updateAvailable("v1.16.0", "v1.17.0"));
    TEST_ASSERT_FALSE(updateAvailable("v1.17.0", "v1.17.0"));
    TEST_ASSERT_FALSE(updateAvailable("v1.18.0", "v1.17.0"));
    TEST_ASSERT_FALSE(updateAvailable("dev", "v1.17.0"));
    TEST_ASSERT_FALSE(updateAvailable("v1.16.0", ""));

    UpdateStatus s;
    s.current = "v1.16.0";
    s.latest = "v1.17.0";
    s.asset_name = "esp32-nut-v1.17.0-ota.bin";
    s.asset_verified = true;
    TEST_ASSERT_TRUE(updateInstallable(s));
    s.asset_verified = false;
    TEST_ASSERT_FALSE(updateInstallable(s));
    s.asset_verified = true;
    s.asset_name = "";
    TEST_ASSERT_FALSE(updateInstallable(s));
    s.asset_name = "esp32-nut-v1.17.0-ota.bin";
    s.current = "v1.17.0";
    TEST_ASSERT_FALSE(updateInstallable(s));
    s.current = "dev";
    TEST_ASSERT_FALSE(updateInstallable(s));
    TEST_ASSERT_FALSE(isReleaseBuild("dev"));
    TEST_ASSERT_TRUE(isReleaseBuild("v1.17.0-rc2"));
    s.current = "v1.17.0-rc2";
    TEST_ASSERT_TRUE(updateInstallable(s));
}

// --- Release JSON ---

void test_release_asset_names(void) {
    TEST_ASSERT_EQUAL_STRING("esp32-nut-v1.16.0-ota.bin", releaseAssetName("v1.16.0", "").c_str());
    TEST_ASSERT_EQUAL_STRING("esp32-nut-v1.16.0-esp32-p4-eth-v1-ota.bin",
                             releaseAssetName("v1.16.0", "-esp32-p4-eth-v1").c_str());
    TEST_ASSERT_EQUAL_STRING("", releaseAssetName("v1.16.0", nullptr).c_str());
    TEST_ASSERT_EQUAL_STRING("", releaseAssetName("", "-esp32-p4-eth").c_str());
}

static const char* RELEASE_JSON =
    "{\"tag_name\":\"v1.16.0\",\"html_url\":\"https://github.com/maverick1982/esp32-nut/releases/tag/v1.16.0\","
    "\"draft\":false,\"prerelease\":false,\"body\":\"notes\",\"assets\":["
    "{\"name\":\"esp32-nut-v1.16.0-esp32-p4-eth-v1-ota.bin\",\"size\":3247719,"
    "\"digest\":\"sha256:238F7563B4DDA865CC90E675708365F911D4EFE3A89F159F95E0462754953B6A\","
    "\"browser_download_url\":\"https://github.com/maverick1982/esp32-nut/releases/download/v1.16.0/"
    "esp32-nut-v1.16.0-esp32-p4-eth-v1-ota.bin\"},"
    "{\"name\":\"esp32-nut-v1.16.0-esp32-p4-eth-ota.bin\",\"size\":3242222,"
    "\"browser_download_url\":\"https://github.com/maverick1982/esp32-nut/releases/download/v1.16.0/"
    "esp32-nut-v1.16.0-esp32-p4-eth-ota.bin\"},"
    "{\"name\":\"esp32-nut-v1.16.0-ota.bin\",\"size\":3423174,\"digest\":\"sha256:8a5d\","
    "\"browser_download_url\":\"https://github.com/maverick1982/esp32-nut/releases/download/v1.16.0/"
    "esp32-nut-v1.16.0-ota.bin\"}]}";

static bool parse(const char* json, const char* env, ReleaseInfo& info, String& err) {
    JsonDocument filter;
    releaseJsonFilter(filter);
    JsonDocument doc;
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;
    return parseRelease(doc, env, info, err);
}

void test_release_picks_the_file_of_this_build(void) {
    ReleaseInfo info;
    String err;
    TEST_ASSERT_TRUE(parse(RELEASE_JSON, "-esp32-p4-eth-v1", info, err));
    TEST_ASSERT_EQUAL_STRING("v1.16.0", info.tag.c_str());
    TEST_ASSERT_EQUAL_STRING("https://github.com/maverick1982/esp32-nut/releases/tag/v1.16.0", info.page_url.c_str());
    TEST_ASSERT_TRUE(info.has_asset);
    TEST_ASSERT_EQUAL_STRING("esp32-nut-v1.16.0-esp32-p4-eth-v1-ota.bin", info.asset.name.c_str());
    TEST_ASSERT_EQUAL_UINT32(3247719, info.asset.size);
    TEST_ASSERT_EQUAL_STRING("238f7563b4dda865cc90e675708365f911d4efe3a89f159f95e0462754953b6a",
                             info.asset.sha256.c_str());
}

void test_release_digest_missing_or_malformed(void) {
    ReleaseInfo info;
    String err;
    TEST_ASSERT_TRUE(parse(RELEASE_JSON, "-esp32-p4-eth", info, err));
    TEST_ASSERT_TRUE(info.has_asset);
    TEST_ASSERT_EQUAL_STRING("", info.asset.sha256.c_str());
    TEST_ASSERT_TRUE(parse(RELEASE_JSON, "", info, err));
    TEST_ASSERT_TRUE(info.has_asset);
    TEST_ASSERT_EQUAL_STRING("", info.asset.sha256.c_str());
}

void test_release_without_file_for_this_build(void) {
    ReleaseInfo info;
    String err;
    TEST_ASSERT_TRUE(parse(RELEASE_JSON, nullptr, info, err));
    TEST_ASSERT_EQUAL_STRING("v1.16.0", info.tag.c_str());
    TEST_ASSERT_FALSE(info.has_asset);
}

void test_release_rejects_bad_input(void) {
    ReleaseInfo info;
    String err;
    TEST_ASSERT_FALSE(parse("{\"message\":\"Not Found\"}", "-esp32-p4-eth", info, err));
    TEST_ASSERT_EQUAL_STRING("No release found", err.c_str());
    TEST_ASSERT_FALSE(parse("{\"tag_name\":\"v2.0.0\",\"draft\":true}", "-esp32-p4-eth", info, err));
    TEST_ASSERT_FALSE(parse("{\"tag_name\":\"v2.0.0\",\"assets\":[{\"name\":\"esp32-nut-v2.0.0-esp32-p4-eth-ota.bin\","
                            "\"browser_download_url\":\"http://evil.example/x.bin\"}]}",
                            "-esp32-p4-eth", info, err));
    TEST_ASSERT_EQUAL_STRING("Unexpected download address for esp32-nut-v2.0.0-esp32-p4-eth-ota.bin", err.c_str());
    TEST_ASSERT_FALSE(info.has_asset);
}

// --- Schedule ---

void test_schedule_first_check_after_the_network(void) {
    UpdateSchedule s;
    TEST_ASSERT_FALSE(s.due(1000, false));
    TEST_ASSERT_FALSE(s.due(5000, true));  // network up at 5 s
    TEST_ASSERT_FALSE(s.due(5000 + UpdateSchedule::FIRST_CHECK_MS - 1, true));
    TEST_ASSERT_TRUE(s.due(5000 + UpdateSchedule::FIRST_CHECK_MS, true));
    TEST_ASSERT_FALSE(s.due(5000 + UpdateSchedule::FIRST_CHECK_MS, false));
}

void test_schedule_daily_and_retry(void) {
    UpdateSchedule s;
    s.due(0, true);
    uint32_t t = UpdateSchedule::FIRST_CHECK_MS;
    s.checked(t, true);
    TEST_ASSERT_FALSE(s.due(t + UpdateSchedule::INTERVAL_MS - 1, true));
    TEST_ASSERT_TRUE(s.due(t + UpdateSchedule::INTERVAL_MS, true));
    t += UpdateSchedule::INTERVAL_MS;
    s.checked(t, false);
    TEST_ASSERT_FALSE(s.due(t + UpdateSchedule::RETRY_MS - 1, true));
    TEST_ASSERT_TRUE(s.due(t + UpdateSchedule::RETRY_MS, true));
}

void test_schedule_auto_off_and_wrap(void) {
    UpdateSchedule s;
    s.setAuto(false);
    s.due(0, true);
    TEST_ASSERT_FALSE(s.due(UpdateSchedule::INTERVAL_MS * 3, true));
    s.setAuto(true);
    uint32_t t = 0xFFFFFFFFu - 1000;
    s.checked(t, true);
    TEST_ASSERT_FALSE(s.due(t + 2000, true));  // across the millis() wrap
    TEST_ASSERT_TRUE(s.due(t + UpdateSchedule::INTERVAL_MS, true));
}

void test_schedule_manual_cooldown(void) {
    UpdateSchedule s;
    TEST_ASSERT_TRUE(s.manualAllowed(0));
    s.manualStarted(1000);
    TEST_ASSERT_FALSE(s.manualAllowed(1000 + UpdateSchedule::MANUAL_COOLDOWN_MS - 1));
    TEST_ASSERT_TRUE(s.manualAllowed(1000 + UpdateSchedule::MANUAL_COOLDOWN_MS));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_version_parse);
    RUN_TEST(test_version_compare);
    RUN_TEST(test_update_available_and_installable);
    RUN_TEST(test_release_asset_names);
    RUN_TEST(test_release_picks_the_file_of_this_build);
    RUN_TEST(test_release_digest_missing_or_malformed);
    RUN_TEST(test_release_without_file_for_this_build);
    RUN_TEST(test_release_rejects_bad_input);
    RUN_TEST(test_schedule_first_check_after_the_network);
    RUN_TEST(test_schedule_daily_and_retry);
    RUN_TEST(test_schedule_auto_off_and_wrap);
    RUN_TEST(test_schedule_manual_cooldown);
    return UNITY_END();
}
