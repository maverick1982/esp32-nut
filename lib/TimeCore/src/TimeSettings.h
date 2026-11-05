#ifndef TIME_SETTINGS_H
#define TIME_SETTINGS_H

#include <Arduino.h>
#include "TimeZones.h"

/**
 * @brief Network time settings of Board Settings (US-117). Pure logic, native tests.
 *
 * Kept in NVS and changed from the Web UI (behind the login). The time is only shown and
 * written in the logs (ADR 0009): a wrong value cannot lock anyone out, so these are user
 * settings with safe defaults (NTP on, pool.ntp.org, UTC).
 */
struct TimeSettings {
    bool ntp;
    String server;  // IPv4 address or host name
    String zone;    // IANA name of TimeZones.h
};

static const size_t NTP_SERVER_MAX = 63;
static const char* const NTP_DEFAULT_SERVER = "pool.ntp.org";

inline TimeSettings timeDefaults() {
    TimeSettings s = { true, String(NTP_DEFAULT_SERVER), String(TIME_ZONE_DEFAULT) };
    return s;
}

// A host name (letters, digits, '-', '.') or an IPv4 address, 1-63 characters: the rules
// of validSyslogHost (SyslogSettings.h)
inline bool validNtpServer(const String& host) {
    if (host.length() == 0 || host.length() > NTP_SERVER_MAX) return false;
    for (size_t i = 0; i < host.length(); i++) {
        char c = host[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok) return false;
    }
    return host[0] != '.' && host[0] != '-' && host[host.length() - 1] != '.';
}

// Values a corrupted NVS entry could hold: an unknown zone goes back to UTC, an invalid
// server is dropped, and NTP without a server would only fail, so it is turned off
inline TimeSettings sanitize(TimeSettings s) {
    if (!findTimeZone(s.zone.c_str())) s.zone = TIME_ZONE_DEFAULT;
    if (s.server.length() > 0 && !validNtpServer(s.server)) s.server = "";
    if (s.server.length() == 0) s.ntp = false;
    return s;
}

#endif // TIME_SETTINGS_H
