#include "core/nut_override_store.h"
#include <Preferences.h>
#include "core/app_logger.h"

static const char* kNamespace = "nutovr";
static const char* kKeyList = "list";

void NvsNutOverrideStore::load(std::vector<NutOverride>& out) {
    out.clear();
    Preferences prefs;
    // Read-only: begin() fails while the namespace does not exist, i.e. nothing saved yet
    if (!prefs.begin(kNamespace, true)) return;
    size_t len = prefs.isKey(kKeyList) ? prefs.getBytesLength(kKeyList) : 0;
    String json;
    if (len > 0) {
        std::vector<char> buf(len + 1, '\0');
        if (prefs.getBytes(kKeyList, buf.data(), len) == len) json = String(buf.data());
    }
    prefs.end();
    if (json.length() == 0) return;
    if (!NutOverrides::fromJson(json, out)) {
        AppLogger::log("WARN", "[NUT] Saved custom variables partly invalid: %u kept", (unsigned)out.size());
    }
}

bool NvsNutOverrideStore::save(const std::vector<NutOverride>& list) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    bool ok;
    if (list.empty()) {
        ok = !prefs.isKey(kKeyList) || prefs.remove(kKeyList);
    } else {
        // One key: NVS writes it atomically, the previous list stays until the new one is complete
        String json = NutOverrides::toJson(list);
        ok = prefs.putBytes(kKeyList, json.c_str(), json.length()) == json.length();
    }
    prefs.end();
    return ok;
}
