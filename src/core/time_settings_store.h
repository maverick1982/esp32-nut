#ifndef CORE_TIME_SETTINGS_STORE_H
#define CORE_TIME_SETTINGS_STORE_H

#include "TimeSettings.h"

/**
 * Network time settings (US-117), kept in the NVS namespace "time", apart from
 * config_json like the remote logging settings: they survive an OTA update. load() gives
 * the defaults for missing keys and sanitizes the stored values.
 */
class TimeSettingsStore {
public:
    TimeSettings load();
    bool save(const TimeSettings& settings);
};

#endif // CORE_TIME_SETTINGS_STORE_H
