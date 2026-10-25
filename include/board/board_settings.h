#ifndef BOARD_SETTINGS_H
#define BOARD_SETTINGS_H

#include <stdint.h>
#include "board/board.h"

/**
 * @brief User settings of the board: status LED and UPS USB port (US-094).
 *
 * Pure logic, so it runs in the native unit tests. The initial values come from the
 * board profile; the user changes them from the web UI and they are kept in NVS.
 * PDR 0002 D4: a wrong value must not lock the user out, so sanitize() brings any value
 * the profile does not allow (for example a corrupted NVS entry) back to the default.
 */
struct BoardSettings {
    bool led_on;
    uint8_t led_level;  // 1-5
    uint8_t usb_port;   // index in board().usb.ports
};

static const uint8_t LED_LEVELS = 5;
static const uint8_t LED_DEFAULT_LEVEL = 3;

// Logarithmic scale: perceived brightness follows roughly the logarithm of the
// intensity, so each level doubles the previous one (level 3 is the historical 10)
inline uint8_t ledBrightness(uint8_t level) {
    static const uint8_t TABLE[LED_LEVELS] = { 3, 5, 10, 20, 40 };
    if (level < 1 || level > LED_LEVELS) level = LED_DEFAULT_LEVEL;
    return TABLE[level - 1];
}

inline bool hasLed(const BoardConfig& b) {
    return b.led.pin >= 0;
}

inline bool hasUsbChoice(const BoardConfig& b) {
    return b.usb.port_count > 1;
}

inline BoardSettings defaultsFor(const BoardConfig& b) {
    BoardSettings s = { b.defaults.led_enabled, b.defaults.led_level, b.defaults.usb_port };
    return s;
}

inline BoardSettings sanitize(const BoardConfig& b, BoardSettings s) {
    const BoardSettings d = defaultsFor(b);
    if (s.led_level < 1 || s.led_level > LED_LEVELS) s.led_level = d.led_level;
    if (s.usb_port >= b.usb.port_count) s.usb_port = d.usb_port;
    return s;
}

#endif // BOARD_SETTINGS_H
