#include "board/board.h"

// ESP32-S3-DevKitC-1: Wi-Fi, no Ethernet, NeoPixel on GPIO 48 powered from GPIO 38.
// US-126: an external W5500 can be wired to the free GPIOs; the pins are set by the user,
// these are the ones the S3 can never give to it (the LED pins come from `led`).
static const BoardConfig ESP32S3_DEVKIT = {
    "esp32s3_devkit",
    "ESP32-S3 DevKitC-1",
    true,   // has_wifi
    false,  // has_ethernet
    { EthType::NONE, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    { true, 48, 5,
      { { 19, 20, "USB port of the UPS" },
        { 22, 25, "not present on the ESP32-S3" },
        { 26, 32, "flash and PSRAM" },
        { 33, 37, "octal PSRAM" },
        { 43, 44, "serial console" } },
      4, { 0, 3, 45, 46 } },
    { 48, 38 },
    { 1, { { "otg", "USB OTG", 0 } } },  // single USB OTG peripheral
    { "NUT_ESP32_Config", "12345678", true, 3, 0, NetworkMode::WIFI },
};

const BoardConfig& board() {
    return ESP32S3_DEVKIT;
}
