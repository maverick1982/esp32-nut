#ifndef LOG_LEVEL_H
#define LOG_LEVEL_H

#include <stdint.h>
#include <string.h>
#include <strings.h>

/**
 * @brief Log levels of the firmware and their syslog severities (issue #69).
 *
 * The firmware logs with the strings "INFO", "WARN" and "ERROR" (AppLogger::log). The
 * outputs (syslog today, MQTT later) compare levels to their minimum, so the strings
 * are turned into an ordered enum once, when the line enters the logging core.
 */
enum class LogLevel : uint8_t { INFO = 0, WARN = 1, ERROR = 2 };

static const uint8_t LOG_LEVEL_COUNT = 3;

inline const char* logLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::WARN: return "WARN";
    case LogLevel::ERROR: return "ERROR";
    default: return "INFO";
    }
}

// Exact names only ("INFO", "WARN", "ERROR", any case): for settings coming from the API
inline bool parseLogLevel(const char* name, LogLevel& out) {
    if (!name) return false;
    for (uint8_t i = 0; i < LOG_LEVEL_COUNT; i++) {
        LogLevel l = (LogLevel)i;
        if (strcasecmp(name, logLevelName(l)) == 0) {
            out = l;
            return true;
        }
    }
    return false;
}

// Level of a log line: unknown strings count as INFO, "WARNING" as WARN
inline LogLevel logLevelOf(const char* name) {
    LogLevel l;
    if (parseLogLevel(name, l)) return l;
    if (name && strcasecmp(name, "WARNING") == 0) return LogLevel::WARN;
    return LogLevel::INFO;
}

// RFC 5424 §6.2.1 severity: 3 Error, 4 Warning, 6 Informational
inline uint8_t syslogSeverity(LogLevel level) {
    switch (level) {
    case LogLevel::ERROR: return 3;
    case LogLevel::WARN: return 4;
    default: return 6;
    }
}

#endif // LOG_LEVEL_H
