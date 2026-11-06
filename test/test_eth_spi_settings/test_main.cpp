#include <unity.h>
#include <string.h>
#include "board/eth_spi_settings.h"

// Pins of a W5500 wired by the user (US-126, issue #64): reserved GPIOs, status LED,
// duplicates, required fields, strapping warnings and sanitize, on the S3 profile

void setUp(void) {}
void tearDown(void) {}

static EthSpiSettings pins(bool enabled, int8_t sck, int8_t mosi, int8_t miso, int8_t cs, int8_t irq, int8_t rst) {
    EthSpiSettings s = { enabled, sck, mosi, miso, cs, irq, rst };
    return s;
}

// The example wiring of the Network page: six adjacent pins on the DevKitC-1
static EthSpiSettings example() {
    return pins(true, 12, 11, 13, 10, 9, 14);
}

static void assertRefused(const EthSpiSettings& s, const char* expected) {
    EthPinCheck check;
    TEST_ASSERT_FALSE(checkEthSpiSettings(board(), s, check));
    TEST_ASSERT_FALSE(check.ok);
    TEST_ASSERT_EQUAL_STRING(expected, check.message);
}

void test_example_pins_are_valid_without_warnings(void) {
    EthPinCheck check;
    TEST_ASSERT_TRUE(checkEthSpiSettings(board(), example(), check));
    TEST_ASSERT_TRUE(check.ok);
    TEST_ASSERT_EQUAL_STRING("", check.message);
    int8_t warn[ETH_SPI_PIN_COUNT];
    TEST_ASSERT_EQUAL_UINT8(0, strappingWarnings(board(), example(), warn));
}

void test_mickel4711_wiring_is_valid_with_a_strapping_warning(void) {
    // External module of issue #94: RST on GPIO 3
    const EthSpiSettings s = pins(true, 12, 11, 13, 10, 9, 3);
    EthPinCheck check;
    TEST_ASSERT_TRUE(checkEthSpiSettings(board(), s, check));
    int8_t warn[ETH_SPI_PIN_COUNT];
    TEST_ASSERT_EQUAL_UINT8(1, strappingWarnings(board(), s, warn));
    TEST_ASSERT_EQUAL_INT8(3, warn[0]);
}

void test_usb_pins_are_refused(void) {
    assertRefused(pins(true, 19, 11, 13, 10, 9, 14), "GPIO 19 cannot be used for SCK: USB port of the UPS");
    assertRefused(pins(true, 12, 20, 13, 10, 9, 14), "GPIO 20 cannot be used for MOSI: USB port of the UPS");
}

void test_missing_gpios_are_refused(void) {
    assertRefused(pins(true, 12, 11, 23, 10, 9, 14), "GPIO 23 cannot be used for MISO: not present on the ESP32-S3");
}

void test_flash_and_psram_pins_are_refused(void) {
    assertRefused(pins(true, 12, 11, 13, 30, 9, 14), "GPIO 30 cannot be used for CS: flash and PSRAM");
    assertRefused(pins(true, 12, 11, 13, 10, 35, 14), "GPIO 35 cannot be used for INT: octal PSRAM");
}

void test_serial_console_pins_are_refused(void) {
    assertRefused(pins(true, 12, 11, 13, 10, 9, 43), "GPIO 43 cannot be used for RST: serial console");
}

void test_status_led_pins_are_refused(void) {
    assertRefused(pins(true, 48, 11, 13, 10, 9, 14), "GPIO 48 cannot be used for SCK: status LED");
    assertRefused(pins(true, 12, 11, 13, 38, 9, 14), "GPIO 38 cannot be used for CS: status LED");
}

void test_gpio_beyond_the_chip_is_refused(void) {
    assertRefused(pins(true, 12, 11, 13, 10, 49, 14), "GPIO 49 does not exist on this board");
    assertRefused(pins(true, -5, 11, 13, 10, 9, 14), "GPIO -5 does not exist on this board");
}

void test_same_gpio_in_two_fields_is_refused(void) {
    assertRefused(pins(true, 10, 11, 13, 10, 9, 14), "GPIO 10 is used for both SCK and CS");
}

void test_required_fields_to_enable(void) {
    assertRefused(pins(true, 12, 11, -1, 10, 9, 14), "MISO is required to enable the W5500");
    assertRefused(pins(true, -1, -1, -1, -1, -1, -1), "SCK is required to enable the W5500");
}

void test_disabled_with_empty_fields_is_valid(void) {
    EthPinCheck check;
    TEST_ASSERT_TRUE(checkEthSpiSettings(board(), ethSpiDefaults(), check));
}

void test_disabled_with_a_forbidden_pin_is_refused(void) {
    // A forbidden GPIO is never saved, even with the module off
    assertRefused(pins(false, 19, -1, -1, -1, -1, -1), "GPIO 19 cannot be used for SCK: USB port of the UPS");
}

void test_int_and_rst_are_optional(void) {
    EthPinCheck check;
    TEST_ASSERT_TRUE(checkEthSpiSettings(board(), pins(true, 12, 11, 13, 10, -1, -1), check));
}

void test_strapping_pins_are_allowed_with_warnings(void) {
    const EthSpiSettings s = pins(true, 0, 3, 45, 46, -1, -1);
    EthPinCheck check;
    TEST_ASSERT_TRUE(checkEthSpiSettings(board(), s, check));
    int8_t warn[ETH_SPI_PIN_COUNT];
    TEST_ASSERT_EQUAL_UINT8(4, strappingWarnings(board(), s, warn));
    TEST_ASSERT_EQUAL_INT8(0, warn[0]);
    TEST_ASSERT_EQUAL_INT8(3, warn[1]);
    TEST_ASSERT_EQUAL_INT8(45, warn[2]);
    TEST_ASSERT_EQUAL_INT8(46, warn[3]);
}

void test_strapping_warnings_have_no_duplicates(void) {
    // Duplicates are refused by the check, but the warnings never list a pin twice
    int8_t warn[ETH_SPI_PIN_COUNT];
    TEST_ASSERT_EQUAL_UINT8(1, strappingWarnings(board(), pins(false, 3, 3, -1, -1, -1, -1), warn));
}

void test_sanitize_disables_invalid_settings_and_keeps_the_pins(void) {
    const EthSpiSettings bad = pins(true, 19, 11, 13, 10, 9, 14);
    const EthSpiSettings s = sanitizeEthSpi(board(), bad);
    TEST_ASSERT_FALSE(s.enabled);
    TEST_ASSERT_EQUAL_INT8(19, s.sck);
    TEST_ASSERT_EQUAL_INT8(14, s.rst);
}

void test_sanitize_keeps_valid_settings(void) {
    TEST_ASSERT_TRUE(sanitizeEthSpi(board(), example()) == example());
    TEST_ASSERT_TRUE(sanitizeEthSpi(board(), ethSpiDefaults()) == ethSpiDefaults());
}

void test_to_eth_config_copies_the_pins(void) {
    const EthConfig c = toEthConfig(example());
    TEST_ASSERT_TRUE(c.type == EthType::SPI);
    TEST_ASSERT_EQUAL_UINT8(EthPhy::W5500, c.phy_type);
    TEST_ASSERT_EQUAL_INT8(12, c.sck);
    TEST_ASSERT_EQUAL_INT8(11, c.mosi);
    TEST_ASSERT_EQUAL_INT8(13, c.miso);
    TEST_ASSERT_EQUAL_INT8(10, c.cs);
    TEST_ASSERT_EQUAL_INT8(9, c.irq);
    TEST_ASSERT_EQUAL_INT8(14, c.rst);
    TEST_ASSERT_EQUAL_INT8(-1, c.mdc);
    TEST_ASSERT_EQUAL_INT8(-1, c.mdio);
}

void test_board_without_w5500_refuses_enabling(void) {
    // A P4-like profile: its own Ethernet port, no W5500 offered
    BoardConfig p4 = board();
    p4.has_wifi = false;
    p4.has_ethernet = true;
    p4.spi_eth.available = false;
    p4.spi_eth.gpio_max = 0;
    p4.spi_eth.reserved_count = 0;
    p4.spi_eth.strapping_count = 0;
    EthPinCheck check;
    TEST_ASSERT_FALSE(checkEthSpiSettings(p4, example(), check));
    TEST_ASSERT_EQUAL_STRING("No W5500 support on this board", check.message);
    TEST_ASSERT_FALSE(sanitizeEthSpi(p4, example()).enabled);
}

// --- US-127: link mode ---

void test_network_mode_names(void) {
    TEST_ASSERT_EQUAL_STRING("auto", networkModeName(NetworkMode::AUTO));
    TEST_ASSERT_EQUAL_STRING("ethernet", networkModeName(NetworkMode::ETHERNET));
    TEST_ASSERT_EQUAL_STRING("wifi", networkModeName(NetworkMode::WIFI));
}

void test_parse_network_mode_accepts_the_three_names(void) {
    NetworkMode m = NetworkMode::WIFI;
    TEST_ASSERT_TRUE(parseNetworkMode("auto", m));
    TEST_ASSERT_TRUE(m == NetworkMode::AUTO);
    TEST_ASSERT_TRUE(parseNetworkMode("ethernet", m));
    TEST_ASSERT_TRUE(m == NetworkMode::ETHERNET);
    TEST_ASSERT_TRUE(parseNetworkMode("wifi", m));
    TEST_ASSERT_TRUE(m == NetworkMode::WIFI);
}

void test_parse_network_mode_refuses_other_text(void) {
    NetworkMode m = NetworkMode::ETHERNET;
    TEST_ASSERT_FALSE(parseNetworkMode("Auto", m));
    TEST_ASSERT_FALSE(parseNetworkMode("", m));
    TEST_ASSERT_FALSE(parseNetworkMode(nullptr, m));
    TEST_ASSERT_FALSE(parseNetworkMode("wi-fi", m));
    TEST_ASSERT_TRUE(m == NetworkMode::ETHERNET);  // untouched on failure
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_example_pins_are_valid_without_warnings);
    RUN_TEST(test_mickel4711_wiring_is_valid_with_a_strapping_warning);
    RUN_TEST(test_usb_pins_are_refused);
    RUN_TEST(test_missing_gpios_are_refused);
    RUN_TEST(test_flash_and_psram_pins_are_refused);
    RUN_TEST(test_serial_console_pins_are_refused);
    RUN_TEST(test_status_led_pins_are_refused);
    RUN_TEST(test_gpio_beyond_the_chip_is_refused);
    RUN_TEST(test_same_gpio_in_two_fields_is_refused);
    RUN_TEST(test_required_fields_to_enable);
    RUN_TEST(test_disabled_with_empty_fields_is_valid);
    RUN_TEST(test_disabled_with_a_forbidden_pin_is_refused);
    RUN_TEST(test_int_and_rst_are_optional);
    RUN_TEST(test_strapping_pins_are_allowed_with_warnings);
    RUN_TEST(test_strapping_warnings_have_no_duplicates);
    RUN_TEST(test_sanitize_disables_invalid_settings_and_keeps_the_pins);
    RUN_TEST(test_sanitize_keeps_valid_settings);
    RUN_TEST(test_to_eth_config_copies_the_pins);
    RUN_TEST(test_board_without_w5500_refuses_enabling);
    RUN_TEST(test_network_mode_names);
    RUN_TEST(test_parse_network_mode_accepts_the_three_names);
    RUN_TEST(test_parse_network_mode_refuses_other_text);
    return UNITY_END();
}
