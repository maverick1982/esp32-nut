#ifndef SYSLOG_CHANGE_H
#define SYSLOG_CHANGE_H

#include <Arduino.h>
#include "SyslogSettings.h"

/**
 * @brief What to log when the syslog settings change (issue #69). Pure logic, native
 * tests.
 *
 * The syslog server is told when the remote logging is turned on or off, moved, or its
 * minimum level (or another setting) changes, whatever the minimum level: these lines
 * are notices (LogRecord::notice, syslog severity 5) and skip the level and the rate
 * limit.
 * - to_old: sent straight to the previous server before the change, because after it
 *   that server gets nothing more (turned off, or moved to another server).
 * - to_new: logged as a notice once the new settings apply: it reaches the System Log
 *   and the server of the new settings.
 * - local: an ordinary line (System Log, and the server only if the level allows it),
 *   used when there is no notice: at boot, or when the output stays off.
 * who is the Web UI user; empty at boot, when the saved settings are loaded.
 */
struct SyslogChangeLog {
    String to_old;
    String to_new;
    String local;
};

inline String syslogSummary(const SyslogSettings& s) {
    return s.host + ":" + String(s.port) + ", " + (s.protocol == SyslogProtocol::RFC5424 ? "RFC 5424" : "RFC 3164") +
           ", facility " + syslogFacilityName(s.facility) + ", " + logLevelName(s.min_level) + " and above, max " +
           String(s.rate) + " lines/s";
}

inline SyslogChangeLog planSyslogChange(const SyslogSettings& old_s, const SyslogSettings& new_s, const String& who) {
    SyslogChangeLog out;
    const bool boot = who.length() == 0;
    const String by = boot ? String("") : String(" by '") + who + "'";

    if (boot) {
        if (new_s.enabled) out.local = String("[SYSLOG] Remote logging on: ") + syslogSummary(new_s);
        return out;
    }
    if (!old_s.enabled && !new_s.enabled) {
        out.local = String("[SYSLOG] Settings saved") + by + " (remote logging off)";
        return out;
    }
    if (old_s.enabled && !new_s.enabled) {
        out.to_old = String("[SYSLOG] Remote logging disabled") + by;
        out.local = out.to_old;
        return out;
    }
    if (!old_s.enabled) {
        out.to_new = String("[SYSLOG] Remote logging enabled") + by + ": " + syslogSummary(new_s);
        return out;
    }
    // Enabled before and after
    if (old_s.host != new_s.host || old_s.port != new_s.port) {
        out.to_old = String("[SYSLOG] Remote logging moved to ") + new_s.host + ":" + String(new_s.port) + by;
        out.to_new = String("[SYSLOG] Remote logging moved here from ") + old_s.host + ":" + String(old_s.port) + by +
                     ": " + syslogSummary(new_s);
        return out;
    }
    String changes;
    auto add = [&changes](const String& item) {
        if (changes.length() > 0) changes += ", ";
        changes += item;
    };
    if (old_s.min_level != new_s.min_level) {
        add(String("minimum level ") + logLevelName(old_s.min_level) + " -> " + logLevelName(new_s.min_level));
    }
    if (old_s.protocol != new_s.protocol) {
        add(String("format ") + syslogProtocolName(old_s.protocol) + " -> " + syslogProtocolName(new_s.protocol));
    }
    if (old_s.facility != new_s.facility) {
        add(String("facility ") + syslogFacilityName(old_s.facility) + " -> " + syslogFacilityName(new_s.facility));
    }
    if (old_s.rate != new_s.rate) {
        add(String("max lines/s ") + String(old_s.rate) + " -> " + String(new_s.rate));
    }
    if (changes.length() > 0) out.to_new = String("[SYSLOG] Settings changed") + by + ": " + changes;
    return out;
}

#endif // SYSLOG_CHANGE_H
