#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include "FirmwareVersion.h"
#include "ReleaseInfo.h"
#include "UpdateSchedule.h"
#include "UpdateStatus.h"
#include "ZipEntryExtractor.h"
#include "Crc32.h"
#include "zip_fixtures.h"

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
    s.asset_name = "esp32-nut-v1.17.0.zip";
    s.asset_verified = true;
    TEST_ASSERT_TRUE(updateInstallable(s));
    s.asset_verified = false;
    TEST_ASSERT_FALSE(updateInstallable(s));
    s.asset_verified = true;
    s.asset_name = "";
    TEST_ASSERT_FALSE(updateInstallable(s));
    s.asset_name = "esp32-nut-v1.17.0.zip";
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

void test_release_zip_names(void) {
    TEST_ASSERT_EQUAL_STRING("esp32-nut-v1.16.0.zip", releaseZipName("v1.16.0", "").c_str());
    TEST_ASSERT_EQUAL_STRING("esp32-nut-v1.16.0-esp32-p4-eth-v1.zip",
                             releaseZipName("v1.16.0", "-esp32-p4-eth-v1").c_str());
    TEST_ASSERT_EQUAL_STRING("", releaseZipName("v1.16.0", nullptr).c_str());
    TEST_ASSERT_EQUAL_STRING("", releaseZipName("", "-esp32-p4-eth").c_str());
}

static const char* RELEASE_JSON =
    "{\"tag_name\":\"v1.16.0\",\"html_url\":\"https://github.com/maverick1982/esp32-nut/releases/tag/v1.16.0\","
    "\"draft\":false,\"prerelease\":false,\"body\":\"notes\",\"assets\":["
    "{\"name\":\"esp32-nut-v1.16.0-esp32-p4-eth-v1.zip\",\"size\":3247719,"
    "\"digest\":\"sha256:238F7563B4DDA865CC90E675708365F911D4EFE3A89F159F95E0462754953B6A\","
    "\"browser_download_url\":\"https://github.com/maverick1982/esp32-nut/releases/download/v1.16.0/"
    "esp32-nut-v1.16.0-esp32-p4-eth-v1.zip\"},"
    "{\"name\":\"esp32-nut-v1.16.0-esp32-p4-eth.zip\",\"size\":3242222,"
    "\"browser_download_url\":\"https://github.com/maverick1982/esp32-nut/releases/download/v1.16.0/"
    "esp32-nut-v1.16.0-esp32-p4-eth.zip\"},"
    "{\"name\":\"esp32-nut-v1.16.0.zip\",\"size\":3423174,\"digest\":\"sha256:8a5d\","
    "\"browser_download_url\":\"https://github.com/maverick1982/esp32-nut/releases/download/v1.16.0/"
    "esp32-nut-v1.16.0.zip\"}]}";

static bool parse(const char* json, const char* env, ReleaseInfo& info, String& err) {
    JsonDocument filter;
    releaseJsonFilter(filter);
    JsonDocument doc;
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;
    return parseRelease(doc, env, info, err);
}

void test_release_picks_the_zip_of_this_build(void) {
    ReleaseInfo info;
    String err;
    TEST_ASSERT_TRUE(parse(RELEASE_JSON, "-esp32-p4-eth-v1", info, err));
    TEST_ASSERT_EQUAL_STRING("v1.16.0", info.tag.c_str());
    TEST_ASSERT_EQUAL_STRING("https://github.com/maverick1982/esp32-nut/releases/tag/v1.16.0", info.page_url.c_str());
    TEST_ASSERT_TRUE(info.has_asset);
    TEST_ASSERT_EQUAL_STRING("esp32-nut-v1.16.0-esp32-p4-eth-v1.zip", info.asset.name.c_str());
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

void test_release_without_zip_for_this_build(void) {
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
    TEST_ASSERT_FALSE(parse("{\"tag_name\":\"v2.0.0\",\"assets\":[{\"name\":\"esp32-nut-v2.0.0-esp32-p4-eth.zip\","
                            "\"browser_download_url\":\"http://evil.example/x.zip\"}]}",
                            "-esp32-p4-eth", info, err));
    TEST_ASSERT_EQUAL_STRING("Unexpected download address for esp32-nut-v2.0.0-esp32-p4-eth.zip", err.c_str());
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

// --- Zip extraction ---

struct VecSink : public IByteSink {
    std::vector<uint8_t> data;
    bool fail_writes = false;
    bool write(const uint8_t* d, size_t n) override {
        if (fail_writes) return false;
        data.insert(data.end(), d, d + n);
        return true;
    }
};

// Treats the "compressed" bytes as already decoded, finished after `expected` bytes
struct PassInflater : public IInflater {
    IByteSink* out = nullptr;
    size_t expected = 0;
    size_t seen = 0;
    bool begin(IByteSink* o) override {
        out = o;
        seen = 0;
        return true;
    }
    bool feed(const uint8_t* d, size_t n) override {
        seen += n;
        return out->write(d, n);
    }
    bool finished() const override { return seen == expected; }
};

static std::vector<uint8_t> firmware() {
    std::vector<uint8_t> fw(FW_SIZE);
    fw[0] = 0xE9;
    for (uint32_t i = 1; i < FW_SIZE; i++) fw[i] = (uint8_t)((i * 7 + 3) & 0xFF);
    return fw;
}

static size_t findLocal(const std::vector<uint8_t>& z, const char* name) {
    size_t nlen = strlen(name);
    for (size_t i = 0; i + 30 + nlen <= z.size(); i++) {
        if (z[i] == 'P' && z[i + 1] == 'K' && z[i + 2] == 3 && z[i + 3] == 4 &&
            (size_t)(z[i + 26] | (z[i + 27] << 8)) == nlen && memcmp(&z[i + 30], name, nlen) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static std::vector<uint8_t> good() {
    return std::vector<uint8_t>(ZIP_GOOD, ZIP_GOOD + sizeof(ZIP_GOOD));
}

static bool run(const std::vector<uint8_t>& z, VecSink& sink, ZipEntryExtractor& x, size_t chunk) {
    for (size_t i = 0; i < z.size(); i += chunk) {
        size_t n = z.size() - i < chunk ? z.size() - i : chunk;
        if (!x.feed(&z[i], n)) return false;
    }
    return true;
}

void test_zip_stored_entry_any_chunk_size(void) {
    size_t chunks[] = { 1, 7, 512, 4096, 100000 };
    for (size_t c : chunks) {
        PassInflater inf;
        VecSink sink;
        ZipEntryExtractor x("firmware.bin", &inf, &sink);
        TEST_ASSERT_TRUE(run(good(), sink, x, c));
        TEST_ASSERT_TRUE(x.found());
        TEST_ASSERT_TRUE(x.complete());
        TEST_ASSERT_EQUAL_UINT32(FW_SIZE, x.entrySize());
        TEST_ASSERT_TRUE(sink.data == firmware());
    }
}

void test_zip_entry_missing(void) {
    PassInflater inf;
    VecSink sink;
    ZipEntryExtractor x("firmware.bin", &inf, &sink);
    std::vector<uint8_t> z(ZIP_NO_FIRMWARE, ZIP_NO_FIRMWARE + sizeof(ZIP_NO_FIRMWARE));
    TEST_ASSERT_FALSE(run(z, sink, x, 64));
    TEST_ASSERT_EQUAL_STRING("Entry not found in the zip", x.error());
    TEST_ASSERT_FALSE(x.complete());
}

void test_zip_crc_mismatch(void) {
    std::vector<uint8_t> z = good();
    size_t h = findLocal(z, "firmware.bin");
    z[h + 30 + 12 + 100] ^= 0x01;  // one data byte
    PassInflater inf;
    VecSink sink;
    ZipEntryExtractor x("firmware.bin", &inf, &sink);
    TEST_ASSERT_FALSE(run(z, sink, x, 256));
    TEST_ASSERT_EQUAL_STRING("Entry CRC-32 mismatch", x.error());
}

void test_zip_not_a_zip_and_truncated(void) {
    std::vector<uint8_t> html(64, '<');
    PassInflater inf;
    VecSink sink;
    ZipEntryExtractor x("firmware.bin", &inf, &sink);
    TEST_ASSERT_FALSE(run(html, sink, x, 16));
    TEST_ASSERT_EQUAL_STRING("Not a zip file", x.error());

    std::vector<uint8_t> z = good();
    z.resize(findLocal(z, "firmware.bin") + 30 + 12 + 500);
    ZipEntryExtractor y("firmware.bin", &inf, &sink);
    TEST_ASSERT_TRUE(run(z, sink, y, 128));
    TEST_ASSERT_TRUE(y.found());
    TEST_ASSERT_FALSE(y.complete());
}

void test_zip_refuses_descriptor_and_encryption(void) {
    std::vector<uint8_t> z = good();
    size_t h = findLocal(z, "firmware.bin");
    z[h + 6] |= 0x08;
    PassInflater inf;
    VecSink sink;
    ZipEntryExtractor x("firmware.bin", &inf, &sink);
    TEST_ASSERT_FALSE(run(z, sink, x, 300));
    TEST_ASSERT_EQUAL_STRING("Zip entry with data descriptor not supported", x.error());

    z = good();
    z[h + 6] |= 0x01;
    ZipEntryExtractor y("firmware.bin", &inf, &sink);
    TEST_ASSERT_FALSE(run(z, sink, y, 300));
    TEST_ASSERT_EQUAL_STRING("Encrypted entry", y.error());
}

void test_zip_sink_failure_stops(void) {
    PassInflater inf;
    VecSink sink;
    sink.fail_writes = true;
    ZipEntryExtractor x("firmware.bin", &inf, &sink);
    TEST_ASSERT_FALSE(run(good(), sink, x, 512));
    TEST_ASSERT_EQUAL_STRING("Write failed", x.error());
}

void test_zip_deflated_entry_goes_through_the_inflater(void) {
    std::vector<uint8_t> z = good();
    size_t h = findLocal(z, "firmware.bin");
    z[h + 8] = 8;  // method deflate; PassInflater hands the bytes on unchanged
    PassInflater inf;
    inf.expected = FW_SIZE;
    VecSink sink;
    ZipEntryExtractor x("firmware.bin", &inf, &sink);
    TEST_ASSERT_TRUE(run(z, sink, x, 333));
    TEST_ASSERT_TRUE(x.complete());
    TEST_ASSERT_TRUE(sink.data == firmware());

    inf.expected = FW_SIZE + 1;  // decoder still expects data at the end of the entry
    VecSink sink2;
    ZipEntryExtractor y("firmware.bin", &inf, &sink2);
    TEST_ASSERT_FALSE(run(z, sink2, y, 333));
    TEST_ASSERT_EQUAL_STRING("Compressed data ended early", y.error());
}

void test_crc32_known_value(void) {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32Update(0, (const uint8_t*)s, 9));
    uint32_t c = crc32Update(0, (const uint8_t*)s, 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32Update(c, (const uint8_t*)s + 4, 5));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_version_parse);
    RUN_TEST(test_version_compare);
    RUN_TEST(test_update_available_and_installable);
    RUN_TEST(test_release_zip_names);
    RUN_TEST(test_release_picks_the_zip_of_this_build);
    RUN_TEST(test_release_digest_missing_or_malformed);
    RUN_TEST(test_release_without_zip_for_this_build);
    RUN_TEST(test_release_rejects_bad_input);
    RUN_TEST(test_schedule_first_check_after_the_network);
    RUN_TEST(test_schedule_daily_and_retry);
    RUN_TEST(test_schedule_auto_off_and_wrap);
    RUN_TEST(test_schedule_manual_cooldown);
    RUN_TEST(test_zip_stored_entry_any_chunk_size);
    RUN_TEST(test_zip_entry_missing);
    RUN_TEST(test_zip_crc_mismatch);
    RUN_TEST(test_zip_not_a_zip_and_truncated);
    RUN_TEST(test_zip_refuses_descriptor_and_encryption);
    RUN_TEST(test_zip_sink_failure_stops);
    RUN_TEST(test_zip_deflated_entry_goes_through_the_inflater);
    RUN_TEST(test_crc32_known_value);
    return UNITY_END();
}
