#include "core/time_settings_store.h"
#include <Preferences.h>
#include "core/nvs_namespace.h"

static const char* kNamespace = "time";
static const char* kKeyNtp = "ntp";
static const char* kKeyServer = "server";
static const char* kKeyZone = "zone";

TimeSettings TimeSettingsStore::load() {
    TimeSettings s = timeDefaults();
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!NvsNamespace::beginRead(prefs, kNamespace)) return s;
    s.ntp = prefs.getBool(kKeyNtp, s.ntp);
    s.server = prefs.getString(kKeyServer, s.server);
    s.zone = prefs.getString(kKeyZone, s.zone);
    prefs.end();
    return sanitize(s);
}

bool TimeSettingsStore::save(const TimeSettings& s) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    // putString() returns 0 for an empty string: check the stored value instead
    prefs.putString(kKeyServer, s.server);
    prefs.putString(kKeyZone, s.zone);
    bool ok = prefs.getString(kKeyServer, "") == s.server && prefs.getString(kKeyZone, "") == s.zone &&
              prefs.putBool(kKeyNtp, s.ntp) > 0;
    prefs.end();
    return ok;
}
