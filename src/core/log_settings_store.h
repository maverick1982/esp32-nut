#ifndef CORE_LOG_SETTINGS_STORE_H
#define CORE_LOG_SETTINGS_STORE_H

#include "SyslogSettings.h"

/**
 * Remote logging settings (issue #69), kept in the NVS namespace "logout". load() gives
 * the defaults for missing keys and sanitizes the stored values.
 */
class LogSettingsStore {
public:
    SyslogSettings loadSyslog();
    bool saveSyslog(const SyslogSettings& settings);
};

#endif // CORE_LOG_SETTINGS_STORE_H
