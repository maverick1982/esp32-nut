#ifndef CORE_NUT_OVERRIDE_STORE_H
#define CORE_NUT_OVERRIDE_STORE_H

#include <Arduino.h>
#include <vector>
#include "NutOverrides.h"

/**
 * Custom variables of the NUT Server page (US-115), in the NVS namespace "nutovr", key
 * "list": the JSON of NutOverrides::toJson saved as a blob. A string would not do: NVS
 * strings stop at 4000 bytes, and 16 entries with escaped quotes and backslashes can
 * pass it. Its own namespace, not the config_json of ConfigManager, as HostnameStore:
 * it survives OTA updates and is saved from the access point too.
 */
class NvsNutOverrideStore {
public:
    // The saved list, empty when none is saved; invalid entries are dropped with a WARN
    void load(std::vector<NutOverride>& out);
    // Replaces the saved list; an empty list removes the key. false when not written
    bool save(const std::vector<NutOverride>& list);
};

#endif // CORE_NUT_OVERRIDE_STORE_H
