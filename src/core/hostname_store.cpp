#include "core/hostname_store.h"
#include <Preferences.h>
#include "core/nvs_namespace.h"
#include "NetHostname.h"
#include "core/app_logger.h"

static const char* kNamespace = "net";
static const char* kKeyHostname = "hostname";

String HostnameStore::load() {
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!NvsNamespace::beginRead(prefs, kNamespace)) return String();
    String name = prefs.getString(kKeyHostname, "");
    prefs.end();
    if (name.length() > 0 && !NetHostname::isValid(name.c_str())) {
        AppLogger::log("WARN", "[NETWORK] Saved hostname \"%s\" not valid: using the default", name.c_str());
        return String();
    }
    return name;
}

bool HostnameStore::save(const String& name) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    bool ok;
    if (name.length() == 0) {
        ok = !prefs.isKey(kKeyHostname) || prefs.remove(kKeyHostname);
    } else {
        ok = prefs.putString(kKeyHostname, name) > 0;
    }
    prefs.end();
    return ok;
}
