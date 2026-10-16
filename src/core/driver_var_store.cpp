#include "core/driver_var_store.h"
#include <Preferences.h>

static const char* kNamespace = "nutrw";
static const char* kKeyVars = "vars";

// Saved JSON, "" when there is none
static String readJson(Preferences& prefs) {
    return prefs.isKey(kKeyVars) ? prefs.getString(kKeyVars, "") : String("");
}

bool NvsDriverVarStore::load(std::vector<std::pair<String, String>>& out) {
    out.clear();
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!prefs.begin(kNamespace, true)) return true;
    String json = readJson(prefs);
    prefs.end();
    if (json.length() == 0) return true;
    return DriverVarStore::fromJson(json, out);
}

bool NvsDriverVarStore::save(const char* name, const char* value) {
    if (!name || !value) return false;
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    // Corrupt data is dropped: the new value is saved with nothing else
    std::vector<std::pair<String, String>> vars;
    DriverVarStore::fromJson(readJson(prefs), vars);
    DriverVarStore::upsert(vars, name, value);
    // One key: NVS writes it atomically, the previous JSON stays until the new one is complete
    bool ok = prefs.putString(kKeyVars, DriverVarStore::toJson(vars)) > 0;
    prefs.end();
    return ok;
}
