#ifndef SYSLOG_SETTINGS_H
#define SYSLOG_SETTINGS_H

#include <Arduino.h>
#include "LogLevel.h"
#include "LogOutputFilter.h"
#include "SyslogFormat.h"

/**
 * @brief User settings of the syslog output (issue #69). Pure logic, native tests.
 *
 * Kept in NVS and changed from the Web UI (behind the login). A wrong value cannot
 * lock the user out (PDR 0002 D4): at worst the logs do not arrive, so these are user
 * settings with safe defaults (disabled, UDP 514, RFC 3164, local0, INFO, 10 lines/s).
 */
struct SyslogSettings {
    bool enabled;
    String host;  // IPv4 address or host name
    uint16_t port;
    SyslogProtocol protocol;
    uint8_t facility;
    LogLevel min_level;
    uint16_t rate;  // lines per second, see LogOutputFilter
};

static const uint16_t SYSLOG_DEFAULT_PORT = 514;
static const uint8_t SYSLOG_DEFAULT_FACILITY = 16;  // local0
static const size_t SYSLOG_HOST_MAX = 63;

inline SyslogSettings syslogDefaults() {
    SyslogSettings s = { false, String(""), SYSLOG_DEFAULT_PORT, SyslogProtocol::RFC3164,
                         SYSLOG_DEFAULT_FACILITY, LogLevel::INFO, LogOutputFilter::DEFAULT_RATE };
    return s;
}

// A host name (letters, digits, '-', '.') or an IPv4 address, 1-63 characters
inline bool validSyslogHost(const String& host) {
    if (host.length() == 0 || host.length() > SYSLOG_HOST_MAX) return false;
    for (size_t i = 0; i < host.length(); i++) {
        char c = host[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok) return false;
    }
    return host[0] != '.' && host[0] != '-' && host[host.length() - 1] != '.';
}

// Values a corrupted NVS entry could hold go back to the defaults; enabled without a
// valid host would only fail, so it is turned off
inline SyslogSettings sanitize(SyslogSettings s) {
    const SyslogSettings d = syslogDefaults();
    if (s.port == 0) s.port = d.port;
    if (s.protocol != SyslogProtocol::RFC3164 && s.protocol != SyslogProtocol::RFC5424) s.protocol = d.protocol;
    if (!syslogFacilityName(s.facility)) s.facility = d.facility;
    if ((uint8_t)s.min_level >= LOG_LEVEL_COUNT) s.min_level = d.min_level;
    if (s.rate < LogOutputFilter::MIN_RATE || s.rate > LogOutputFilter::MAX_RATE) s.rate = d.rate;
    if (s.host.length() > 0 && !validSyslogHost(s.host)) s.host = "";
    if (s.host.length() == 0) s.enabled = false;
    return s;
}

#endif // SYSLOG_SETTINGS_H
