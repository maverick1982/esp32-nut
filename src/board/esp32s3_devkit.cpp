#include "board/board.h"

// ESP32-S3-DevKitC-1: Wi-Fi, no Ethernet, NeoPixel on GPIO 48 powered from GPIO 38
static const BoardConfig ESP32S3_DEVKIT = {
    "esp32s3_devkit",
    "ESP32-S3 DevKitC-1",
    true,   // has_wifi
    false,  // has_ethernet
    { EthType::NONE, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    { 48, 38 },
    { 1, { { "otg", "USB OTG", 0 } } },  // single USB OTG peripheral
    { "NUT_ESP32_Config", "12345678", true, 3, 0, NetworkMode::WIFI },
};

const BoardConfig& board() {
    return ESP32S3_DEVKIT;
}
