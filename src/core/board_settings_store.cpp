#include "core/board_settings_store.h"
#include <Preferences.h>

static const char* kNamespace = "board";
static const char* kKeyLedOn = "led_on";
static const char* kKeyLedLevel = "led_level";
static const char* kKeyUsbPort = "usb_port";

BoardSettings BoardSettingsStore::load(const BoardConfig& board) {
    BoardSettings s = defaultsFor(board);
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!prefs.begin(kNamespace, true)) return s;
    s.led_on = prefs.getBool(kKeyLedOn, s.led_on);
    s.led_level = prefs.getUChar(kKeyLedLevel, s.led_level);
    s.usb_port = prefs.getUChar(kKeyUsbPort, s.usb_port);
    prefs.end();
    return sanitize(board, s);
}

bool BoardSettingsStore::save(const BoardSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    bool ok = prefs.putBool(kKeyLedOn, settings.led_on) > 0 &&
              prefs.putUChar(kKeyLedLevel, settings.led_level) > 0 &&
              prefs.putUChar(kKeyUsbPort, settings.usb_port) > 0;
    prefs.end();
    return ok;
}
