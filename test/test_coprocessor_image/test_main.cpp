#include <unity.h>
#include <string.h>
#include <vector>
#include "CoprocessorImage.h"

// ADR 0015 section 5: the check of an uploaded Wi-Fi coprocessor image, before anything is
// written. The fixture is the first 256 bytes of esp32c6-v2.12.13.bin, the ESP-Hosted slave
// firmware of the Arduino core 3.3.12 (framework-arduinoespressif32-libs/hosted/)

using CoprocessorImage::Result;

void setUp(void) {}
void tearDown(void) {}

static const uint8_t C6_HEAD[256] = {
    0xE9, 0x05, 0x02, 0x20, 0xE0, 0x03, 0x80, 0x40, 0xEE, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00,
    0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x20, 0x00, 0x0D, 0x42, 0xF0, 0xB7, 0x03, 0x00,
    0x32, 0x54, 0xCD, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x32, 0x2E, 0x31, 0x32, 0x2E, 0x31, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x6E, 0x65, 0x74, 0x77, 0x6F, 0x72, 0x6B, 0x5F, 0x61, 0x64, 0x61, 0x70, 0x74, 0x65, 0x72, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x32, 0x31, 0x3A, 0x32, 0x34, 0x3A, 0x35, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x53, 0x65, 0x70, 0x20, 0x31, 0x36, 0x20, 0x32, 0x30, 0x32, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x76, 0x35, 0x2E, 0x35, 0x2E, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xF0, 0x8F, 0xB7, 0xF2, 0xDB, 0xEC, 0x47, 0xB1, 0x73, 0x87, 0xA6, 0x8D, 0xD2, 0x7B, 0xF4, 0x71,
    0xC3, 0xC3, 0xF1, 0xAC, 0x34, 0xF7, 0x37, 0x04, 0x6E, 0x5A, 0x1C, 0x61, 0x6E, 0x8F, 0xFA, 0x89,
    0x00, 0x00, 0x63, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static std::vector<uint8_t> head() {
    return std::vector<uint8_t>(C6_HEAD, C6_HEAD + sizeof(C6_HEAD));
}

void test_the_core_image_is_accepted(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c6", info) == Result::OK);
    TEST_ASSERT_EQUAL_HEX16(0x000D, info.chip_id);
    TEST_ASSERT_EQUAL_STRING("2.12.13", info.version);
    TEST_ASSERT_EQUAL_STRING("network_adapter", info.project);
    TEST_ASSERT_EQUAL_STRING("v5.5.5", info.idf);
    char msg[96];
    CoprocessorImage::message(Result::OK, info, "esp32c6", msg, sizeof(msg));
    TEST_ASSERT_EQUAL_STRING("ESP-Hosted firmware 2.12.13 for esp32c6", msg);
}

void test_short_file_is_refused(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), 255, "esp32c6", info) == Result::TOO_SHORT);
    TEST_ASSERT_TRUE(CoprocessorImage::check(nullptr, 0, "esp32c6", info) == Result::TOO_SHORT);
}

void test_not_an_image_is_refused(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    h[0] = 'P';  // a zip ("PK...") uploaded by mistake
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c6", info) == Result::NOT_AN_IMAGE);
}

// The board firmware of the P4 (chip ID 0x0012) or a C3 image on the C6
void test_image_for_another_chip_is_refused(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    h[12] = 0x12;
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c6", info) == Result::WRONG_CHIP);
    char msg[96];
    CoprocessorImage::message(Result::WRONG_CHIP, info, "esp32c6", msg, sizeof(msg));
    TEST_ASSERT_EQUAL_STRING("The image is for another chip (chip ID 0x0012), the coprocessor is an esp32c6", msg);
    // The same C6 image on a C5 coprocessor
    h = head();
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c5", info) == Result::WRONG_CHIP);
}

void test_image_without_app_description_is_refused(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    h[32] = 0x00;  // a bootloader or a partition table has no application description
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c6", info) == Result::NO_APP_DESC);
}

// Any C6 application other than the ESP-Hosted slave would take the Wi-Fi away
void test_other_c6_application_is_refused(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    memset(h.data() + 80, 0, 32);
    memcpy(h.data() + 80, "blink", 5);
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c6", info) == Result::NOT_ESP_HOSTED);
    TEST_ASSERT_EQUAL_STRING("blink", info.project);
    char msg[96];
    CoprocessorImage::message(Result::NOT_ESP_HOSTED, info, "esp32c6", msg, sizeof(msg));
    TEST_ASSERT_EQUAL_STRING("Not an ESP-Hosted coprocessor firmware (project \"blink\")", msg);
}

void test_unknown_target_is_refused(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "", info) == Result::UNKNOWN_TARGET);
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), nullptr, info) == Result::UNKNOWN_TARGET);
    TEST_ASSERT_EQUAL_HEX16(0x000D, CoprocessorImage::chipIdForTarget("esp32c6"));
    TEST_ASSERT_EQUAL_HEX16(0x0017, CoprocessorImage::chipIdForTarget("esp32c5"));
}

// Fields without a terminator and control bytes stay bounded and printable
void test_fields_are_bounded_and_printable(void) {
    CoprocessorImage::Info info;
    std::vector<uint8_t> h = head();
    memset(h.data() + 48, 'x', 32);  // version without a terminator
    h[49] = 0x07;
    TEST_ASSERT_TRUE(CoprocessorImage::check(h.data(), h.size(), "esp32c6", info) == Result::OK);
    TEST_ASSERT_EQUAL(32, strlen(info.version));
    TEST_ASSERT_EQUAL('?', info.version[1]);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_the_core_image_is_accepted);
    RUN_TEST(test_short_file_is_refused);
    RUN_TEST(test_not_an_image_is_refused);
    RUN_TEST(test_image_for_another_chip_is_refused);
    RUN_TEST(test_image_without_app_description_is_refused);
    RUN_TEST(test_other_c6_application_is_refused);
    RUN_TEST(test_unknown_target_is_refused);
    RUN_TEST(test_fields_are_bounded_and_printable);
    return UNITY_END();
}
