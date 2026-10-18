#ifndef CORE_DRIVER_VAR_STORE_H
#define CORE_DRIVER_VAR_STORE_H

#include <Arduino.h>
#include "DriverVarStore.h"

/**
 * Driver-side variables written with SET VAR (US-067), kept in the NVS namespace "nutrw"
 * as one JSON key "vars" ({name: value}, DriverVarStore::toJson). They survive restarts
 * and OTA updates; erase() drops one, back to the driver default (US-068).
 */
class NvsDriverVarStore : public IDriverVarStore {
public:
    bool load(std::vector<std::pair<String, String>>& out) override;
    bool save(const char* name, const char* value) override;
    bool erase(const char* name) override;
};

#endif // CORE_DRIVER_VAR_STORE_H
