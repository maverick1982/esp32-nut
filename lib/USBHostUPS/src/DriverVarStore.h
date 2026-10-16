#ifndef DRIVER_VAR_STORE_H
#define DRIVER_VAR_STORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include <utility>
#include <vector>
#include <string.h>

/**
 * Persistent store of the driver-side variables written with SET VAR (US-067):
 * ups.delay.shutdown, ups.delay.start and outlet.*.desc, which never reach the UPS.
 * They apply to any UPS connected, as offdelay/ondelay in ups.conf. The firmware
 * implementation keeps them in NVS (src/core/driver_var_store.h); the host loads them
 * once and saves each SET before changing its cache.
 */
class IDriverVarStore {
public:
    virtual ~IDriverVarStore() = default;
    // Saved (name, value) pairs; false when they cannot be read (out is then empty)
    virtual bool load(std::vector<std::pair<String, String>>& out) = 0;
    // Adds or replaces one value; false when it was not written
    virtual bool save(const char* name, const char* value) = 0;
    // Drops one value, so the driver default applies again (US-068); a name that is not
    // saved is no error. false when the store was not written
    virtual bool erase(const char* name) = 0;
};

// JSON form of the saved values, {name: value}: one NVS key holds them all, since an
// NVS key has at most 15 characters and "ups.delay.shutdown" cannot be one
namespace DriverVarStore {

// Replaces the value of name, or appends it: one entry per name
inline void upsert(std::vector<std::pair<String, String>>& vars, const char* name, const char* value) {
    if (!name) return;
    for (auto& kv : vars) {
        if (strcmp(kv.first.c_str(), name) == 0) {
            kv.second = value ? value : "";
            return;
        }
    }
    vars.push_back(std::make_pair(String(name), String(value ? value : "")));
}

// Drops the entry of name, keeping the order of the others; true when one was there
inline bool remove(std::vector<std::pair<String, String>>& vars, const char* name) {
    if (!name) return false;
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        if (strcmp(it->first.c_str(), name) == 0) {
            vars.erase(it);
            return true;
        }
    }
    return false;
}

// {name: value} in the order of vars
inline String toJson(const std::vector<std::pair<String, String>>& vars) {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    for (const auto& kv : vars) obj[kv.first.c_str()] = kv.second.c_str();
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

// Inverse of toJson(). Anything but an object of strings (corrupt or foreign data) gives
// false and an empty list: the driver defaults apply
inline bool fromJson(const String& json, std::vector<std::pair<String, String>>& out) {
    out.clear();
    JsonDocument doc;
    if (deserializeJson(doc, json.c_str()) != DeserializationError::Ok) return false;
    if (!doc.is<JsonObject>()) return false;
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (!kv.value().is<const char*>()) {
            out.clear();
            return false;
        }
        upsert(out, kv.key().c_str(), kv.value().as<const char*>());
    }
    return true;
}

} // namespace DriverVarStore

#endif // DRIVER_VAR_STORE_H
