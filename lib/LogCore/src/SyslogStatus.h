#ifndef SYSLOG_STATUS_H
#define SYSLOG_STATUS_H

#include <Arduino.h>
#include "LogOutput.h"

/**
 * @brief State of the syslog output for the Web UI (issue #69).
 * Disabled: turned off. Waiting: no network yet, or the host name is not resolved.
 * Active: lines are sent. Error: the last send failed (detail in `error`).
 */
enum class SyslogState : uint8_t { Disabled, Waiting, Active, Error };

inline const char* syslogStateName(SyslogState s) {
    switch (s) {
    case SyslogState::Waiting: return "waiting";
    case SyslogState::Active: return "active";
    case SyslogState::Error: return "error";
    default: return "disabled";
    }
}

struct SyslogStatus {
    SyslogState state;
    String target;  // "host:port", or "host (10.0.0.5):port" for a resolved name
    String error;   // last problem, empty when none
    LogOutputStats stats;
};

#endif // SYSLOG_STATUS_H
