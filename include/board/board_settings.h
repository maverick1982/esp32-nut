#ifndef BOARD_SETTINGS_H
#define BOARD_SETTINGS_H

#include <stdint.h>
#include <string.h>
#include "board/board.h"
#include "QxProtocols.h"

/**
 * @brief User settings of the board: status LED, UPS USB port (US-094), automatic
 * shutdown on low battery (US-114) and the protocol/novendor options of the Q* driver
 * (US-138, protocol and novendor of ups.conf).
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
    // US-114: shutdown.default on OB+LB, off by default on every board (not from the profile)
    bool auto_shutdown;
    // US-138: protocol of ups.conf for the Q* driver, "" = automatic detection; lower-case
    // name of QX_SUBDRIVER_LIST after sanitize()
    char qx_protocol[QX_PROTOCOL_NAME_MAX];
    // US-138: novendor of ups.conf, off by default
    bool qx_novendor;
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
    BoardSettings s = {};
    s.led_on = b.defaults.led_enabled;
    s.led_level = b.defaults.led_level;
    s.usb_port = b.defaults.usb_port;
    s.auto_shutdown = false;
    s.qx_protocol[0] = '\0'; // automatic
    s.qx_novendor = false;
    return s;
}

inline BoardSettings sanitize(const BoardConfig& b, BoardSettings s) {
    const BoardSettings d = defaultsFor(b);
    if (s.led_level < 1 || s.led_level > LED_LEVELS) s.led_level = d.led_level;
    if (s.usb_port >= b.usb.port_count) s.usb_port = d.usb_port;
    // US-138: a protocol the firmware does not know becomes automatic (ADR 0010), a known one
    // takes its canonical lower-case name
    s.qx_protocol[QX_PROTOCOL_NAME_MAX - 1] = '\0';
    const QxSubdriver* p = qxFindProtocol(s.qx_protocol);
    if (p) qxProtocolName(*p, s.qx_protocol, sizeof(s.qx_protocol));
    else s.qx_protocol[0] = '\0';
    return s;
}

#endif // BOARD_SETTINGS_H
