#include "core/log_settings_store.h"
#include <Preferences.h>

static const char* kNamespace = "logout";
static const char* kKeyOn = "sl_on";
static const char* kKeyHost = "sl_host";
static const char* kKeyPort = "sl_port";
static const char* kKeyProto = "sl_proto";
static const char* kKeyFacility = "sl_fac";
static const char* kKeyLevel = "sl_lvl";
static const char* kKeyRate = "sl_rate";

SyslogSettings LogSettingsStore::loadSyslog() {
    SyslogSettings s = syslogDefaults();
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!prefs.begin(kNamespace, true)) return s;
    s.enabled = prefs.getBool(kKeyOn, s.enabled);
    s.host = prefs.getString(kKeyHost, s.host);
    s.port = prefs.getUShort(kKeyPort, s.port);
    s.protocol = (SyslogProtocol)prefs.getUChar(kKeyProto, (uint8_t)s.protocol);
    s.facility = prefs.getUChar(kKeyFacility, s.facility);
    s.min_level = (LogLevel)prefs.getUChar(kKeyLevel, (uint8_t)s.min_level);
    s.rate = prefs.getUShort(kKeyRate, s.rate);
    prefs.end();
    return sanitize(s);
}

bool LogSettingsStore::saveSyslog(const SyslogSettings& s) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    // putString() returns 0 for an empty string: check the stored value instead
    prefs.putString(kKeyHost, s.host);
    bool ok = prefs.getString(kKeyHost, "") == s.host &&
              prefs.putBool(kKeyOn, s.enabled) > 0 &&
              prefs.putUShort(kKeyPort, s.port) > 0 &&
              prefs.putUChar(kKeyProto, (uint8_t)s.protocol) > 0 &&
              prefs.putUChar(kKeyFacility, s.facility) > 0 &&
              prefs.putUChar(kKeyLevel, (uint8_t)s.min_level) > 0 &&
              prefs.putUShort(kKeyRate, s.rate) > 0;
    prefs.end();
    return ok;
}
