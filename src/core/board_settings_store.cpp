#include "core/board_settings_store.h"
#include <Preferences.h>
#include "core/nvs_namespace.h"

static const char* kNamespace = "board";
static const char* kKeyLedOn = "led_on";
static const char* kKeyLedLevel = "led_level";
static const char* kKeyUsbPort = "usb_port";
static const char* kKeyAutoShutdown = "auto_sd";      // US-114
static const char* kKeyQxProtocol = "qx_proto";       // US-138
static const char* kKeyQxNovendor = "qx_novendor";    // US-138

BoardSettings BoardSettingsStore::load(const BoardConfig& board) {
    BoardSettings s = defaultsFor(board);
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!NvsNamespace::beginRead(prefs, kNamespace)) return s;
    s.led_on = prefs.getBool(kKeyLedOn, s.led_on);
    s.led_level = prefs.getUChar(kKeyLedLevel, s.led_level);
    s.usb_port = prefs.getUChar(kKeyUsbPort, s.usb_port);
    s.auto_shutdown = prefs.getBool(kKeyAutoShutdown, s.auto_shutdown);
    String proto = prefs.getString(kKeyQxProtocol, "");
    snprintf(s.qx_protocol, sizeof(s.qx_protocol), "%s", proto.c_str());
    s.qx_novendor = prefs.getBool(kKeyQxNovendor, s.qx_novendor);
    prefs.end();
    return sanitize(board, s);
}

bool BoardSettingsStore::save(const BoardSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    bool ok = prefs.putBool(kKeyLedOn, settings.led_on) > 0 &&
              prefs.putUChar(kKeyLedLevel, settings.led_level) > 0 &&
              prefs.putUChar(kKeyUsbPort, settings.usb_port) > 0 &&
              prefs.putBool(kKeyAutoShutdown, settings.auto_shutdown) > 0 &&
              // putString() returns the length written: 0 for "" (automatic) also on success
              (prefs.putString(kKeyQxProtocol, settings.qx_protocol) > 0 || settings.qx_protocol[0] == '\0') &&
              prefs.putBool(kKeyQxNovendor, settings.qx_novendor) > 0;
    prefs.end();
    return ok;
}
