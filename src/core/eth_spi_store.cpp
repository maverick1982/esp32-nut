#include "core/eth_spi_store.h"
#include <Preferences.h>
#include "core/app_logger.h"

static const char* kNamespace = "eth";
static const char* kKeyOn = "on";
static const char* kKeySck = "sck";
static const char* kKeyMosi = "mosi";
static const char* kKeyMiso = "miso";
static const char* kKeyCs = "cs";
static const char* kKeyIrq = "irq";
static const char* kKeyRst = "rst";
static const char* kKeyMode = "mode";  // US-127

EthSpiSettings EthSpiStore::load(const BoardConfig& board) {
    EthSpiSettings s = ethSpiDefaults();
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!prefs.begin(kNamespace, true)) return s;
    s.enabled = prefs.getBool(kKeyOn, s.enabled);
    s.sck = prefs.getChar(kKeySck, s.sck);
    s.mosi = prefs.getChar(kKeyMosi, s.mosi);
    s.miso = prefs.getChar(kKeyMiso, s.miso);
    s.cs = prefs.getChar(kKeyCs, s.cs);
    s.irq = prefs.getChar(kKeyIrq, s.irq);
    s.rst = prefs.getChar(kKeyRst, s.rst);
    prefs.end();
    const EthSpiSettings clean = sanitizeEthSpi(board, s);
    if (s.enabled && !clean.enabled) {
        EthPinCheck check;
        checkEthSpiSettings(board, s, check);
        AppLogger::log("WARN", "[NETWORK] Saved W5500 pins not valid: W5500 disabled (%s)", check.message);
    }
    return clean;
}

bool EthSpiStore::save(const EthSpiSettings& settings) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    bool ok = prefs.putBool(kKeyOn, settings.enabled) > 0 &&
              prefs.putChar(kKeySck, settings.sck) > 0 &&
              prefs.putChar(kKeyMosi, settings.mosi) > 0 &&
              prefs.putChar(kKeyMiso, settings.miso) > 0 &&
              prefs.putChar(kKeyCs, settings.cs) > 0 &&
              prefs.putChar(kKeyIrq, settings.irq) > 0 &&
              prefs.putChar(kKeyRst, settings.rst) > 0;
    prefs.end();
    return ok;
}

NetworkMode EthSpiStore::loadMode() {
    NetworkMode mode = NetworkMode::AUTO;
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return mode;
    // isKey() first: getString() of a missing key logs an NVS error
    if (prefs.isKey(kKeyMode)) {
        const String text = prefs.getString(kKeyMode, "");
        if (!parseNetworkMode(text.c_str(), mode)) {
            mode = NetworkMode::AUTO;
            AppLogger::log("WARN", "[NETWORK] Saved link mode not valid: using auto");
        }
    }
    prefs.end();
    return mode;
}

bool EthSpiStore::saveMode(NetworkMode mode) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    const bool ok = prefs.putString(kKeyMode, networkModeName(mode)) > 0;
    prefs.end();
    return ok;
}
