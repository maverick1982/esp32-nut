#include "board/board.h"

// Arduino Nano ESP32: ESP32-S3 with 16 MB flash and octal PSRAM. Its one USB-C
// connector belongs exclusively to the UPS host; the RGB LED is intentionally
// not driven because its pins are strapping pins.
static const BoardConfig NANO_ESP32_BOARD = {
    "arduino_nano_esp32",
    "Arduino Nano ESP32",
    true,   // has_wifi
    false,  // has_ethernet
    { EthType::NONE, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
    { true, 48, 4,
      { { 19, 20, "USB port of the UPS" },
        { 26, 32, "flash and PSRAM" },
        { 33, 37, "octal PSRAM" },
        { 43, 44, "serial console" } },
      4, { 0, 3, 45, 46 } },
    { -1, -1 },  // no status LED: RGB pins are strapping pins
    { 1, { { "otg", "USB OTG", 0 } } },
    { "NUT_ESP32_Config", "12345678", false, 3, 0, NetworkMode::WIFI },
    { false, -1, -1, -1, -1, -1, -1, -1 },  // Wi-Fi in the chip, no coprocessor (ADR 0015)
};

const BoardConfig& board() {
    return NANO_ESP32_BOARD;
}
