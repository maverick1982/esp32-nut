#ifndef NUT_OVERRIDES_H
#define NUT_OVERRIDES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <string.h>

/**
 * Custom variables of the NUT Server page (US-115), with the semantics of override.<var>
 * in ups.conf (nut_repo/drivers/main.c storeval): the user value replaces the one of the
 * UPS, or adds the variable when the UPS does not report it, and the variable becomes
 * immutable (no RW flag). They apply to the NUT server only: the dashboard and the board
 * logic keep the real values. Upstream checks nothing on the name; here the key must look
 * like a NUT variable and the keys the server builds itself are refused.
 */
struct NutOverride {
    String key;
    String value;
    String description;
};

namespace NutOverrides {

// GCC 5.1 of the native toolchain has no inline variables: limits as enumerators.
// 64 for the value: the STRING:64 the server declares for the variables that are not RW
enum Limits : size_t {
    MAX_COUNT = 16,
    MAX_KEY_LENGTH = 63,
    MAX_VALUE_LENGTH = 64,
    MAX_DESCRIPTION_LENGTH = 64,
};

enum class Check { OK, INVALID_KEY, PROTECTED_KEY, INVALID_VALUE, INVALID_DESCRIPTION, DUPLICATE_KEY, TOO_MANY };

inline char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// strcasecmp() of upsd, which looks variables up ignoring case (common/state.c)
inline bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (lowerAscii(*a) != lowerAscii(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

inline bool startsWithIgnoreCase(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        if (lowerAscii(*s) != lowerAscii(*prefix)) return false;
        s++;
        prefix++;
    }
    return true;
}

// Bytes 0x20-0x7E only, as the line reader of the NUT server, and at most max_len
inline bool isPrintableText(const char* s, size_t max_len) {
    if (!s) return false;
    size_t n = 0;
    for (; s[n]; n++) {
        unsigned char c = (unsigned char)s[n];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return n <= max_len;
}

// Variables the server builds itself: the status (and its internal ups.status.* keys),
// device.type and the driver.parameter.* published for the overrides
inline bool isProtectedKey(const char* key) {
    return equalsIgnoreCase(key, "ups.status") || startsWithIgnoreCase(key, "ups.status.") ||
           equalsIgnoreCase(key, "device.type") || startsWithIgnoreCase(key, "driver.parameter.");
}

// At least two dot-separated parts of letters, digits, '_' and '-'. Upper case is allowed:
// NUT has names such as input.L1-N.voltage
inline Check checkKey(const char* key) {
    if (!key) return Check::INVALID_KEY;
    size_t len = strlen(key);
    if (len == 0 || len > MAX_KEY_LENGTH) return Check::INVALID_KEY;
    size_t parts = 1;
    size_t part_len = 0;
    for (size_t i = 0; i < len; i++) {
        char c = key[i];
        if (c == '.') {
            if (part_len == 0) return Check::INVALID_KEY;
            parts++;
            part_len = 0;
            continue;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return Check::INVALID_KEY;
        part_len++;
    }
    if (part_len == 0 || parts < 2) return Check::INVALID_KEY;
    if (isProtectedKey(key)) return Check::PROTECTED_KEY;
    return Check::OK;
}

// Value and description may be empty
inline Check checkEntry(const NutOverride& o) {
    Check c = checkKey(o.key.c_str());
    if (c != Check::OK) return c;
    if (!isPrintableText(o.value.c_str(), MAX_VALUE_LENGTH)) return Check::INVALID_VALUE;
    if (!isPrintableText(o.description.c_str(), MAX_DESCRIPTION_LENGTH)) return Check::INVALID_DESCRIPTION;
    return Check::OK;
}

// The whole list: too many entries first (bad_index = MAX_COUNT), then entry by entry,
// then duplicate keys ignoring case (bad_index = the second one)
inline Check checkList(const std::vector<NutOverride>& list, size_t& bad_index) {
    bad_index = 0;
    if (list.size() > MAX_COUNT) {
        bad_index = MAX_COUNT;
        return Check::TOO_MANY;
    }
    for (size_t i = 0; i < list.size(); i++) {
        Check c = checkEntry(list[i]);
        if (c != Check::OK) {
            bad_index = i;
            return c;
        }
    }
    for (size_t i = 1; i < list.size(); i++) {
        for (size_t j = 0; j < i; j++) {
            if (equalsIgnoreCase(list[i].key.c_str(), list[j].key.c_str())) {
                bad_index = i;
                return Check::DUPLICATE_KEY;
            }
        }
    }
    return Check::OK;
}

// Message of the Web UI and of the API for a refused entry
inline String errorMessage(Check c, const String& key) {
    switch (c) {
    case Check::INVALID_KEY:
        return String("Invalid key \"") + key +
               "\": use dot-separated parts of letters, digits, '_' and '-' (e.g. battery.date)";
    case Check::PROTECTED_KEY:
        return key + " cannot be overridden";
    case Check::INVALID_VALUE:
        return String("Invalid value for ") + key + ": printable ASCII only, at most 64 characters";
    case Check::INVALID_DESCRIPTION:
        return String("Invalid description for ") + key + ": printable ASCII only, at most 64 characters";
    case Check::DUPLICATE_KEY:
        return String("Duplicate key: ") + key;
    case Check::TOO_MANY:
        return String("At most 16 custom variables");
    case Check::OK:
    default:
        return String();
    }
}

// The override of key, ignoring case; nullptr when there is none
inline const NutOverride* find(const std::vector<NutOverride>& list, const char* key) {
    for (const auto& o : list) {
        if (equalsIgnoreCase(o.key.c_str(), key)) return &o;
    }
    return nullptr;
}

inline const NutOverride* find(const std::vector<NutOverride>* list, const char* key) {
    return list ? find(*list, key) : nullptr;
}

// Saved form: [{"k": key, "v": value, "d": description}] in the order of the list
inline String toJson(const std::vector<NutOverride>& list) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& o : list) {
        JsonObject obj = arr.add<JsonObject>();
        obj["k"] = o.key.c_str();
        obj["v"] = o.value.c_str();
        obj["d"] = o.description.c_str();
    }
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

// Inverse of toJson(). Keeps the valid entries (checkEntry, no duplicates) up to MAX_COUNT;
// false when something was dropped or the JSON is not an array
inline bool fromJson(const String& json, std::vector<NutOverride>& out) {
    out.clear();
    JsonDocument doc;
    if (deserializeJson(doc, json.c_str()) != DeserializationError::Ok) return false;
    if (!doc.is<JsonArray>()) return false;
    bool all_kept = true;
    for (JsonVariantConst item : doc.as<JsonArrayConst>()) {
        if (!item["k"].is<const char*>() || !item["v"].is<const char*>()) {
            all_kept = false;
            continue;
        }
        NutOverride o;
        o.key = item["k"].as<const char*>();
        o.value = item["v"].as<const char*>();
        o.description = item["d"].is<const char*>() ? item["d"].as<const char*>() : "";
        if (out.size() >= MAX_COUNT || checkEntry(o) != Check::OK || find(out, o.key.c_str())) {
            all_kept = false;
            continue;
        }
        out.push_back(o);
    }
    return all_kept;
}

} // namespace NutOverrides

#endif // NUT_OVERRIDES_H
