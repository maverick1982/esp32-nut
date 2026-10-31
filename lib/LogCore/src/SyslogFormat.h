#ifndef SYSLOG_FORMAT_H
#define SYSLOG_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include "LogLevel.h"

/**
 * @brief Syslog message format, RFC 3164 (BSD) and RFC 5424 (issue #69).
 *
 * The board has no wall clock (no RTC, no SNTP: ADR 0009), so no message carries a
 * timestamp: the receiver stamps it on arrival.
 * - RFC 3164: "<PRI>TAG: MSG". Without a TIMESTAMP, RFC 3164 §4.3.2 has the relay add
 *   the time and the sender's host name; rsyslog uses the arrival time and the source
 *   address. TAG is the app name, cut to 32 alphanumeric characters.
 * - RFC 5424: "<PRI>1 - HOSTNAME APP-NAME - - - MSG", with NILVALUE "-" for the
 *   timestamp, the process id, the message id and the structured data.
 * Control characters in the message become spaces and trailing blanks are removed:
 * one log line is one datagram.
 */
enum class SyslogProtocol : uint8_t { RFC3164 = 0, RFC5424 = 1 };

// RFC 5424 §6.1: every receiver must accept 480 octets; the log lines are shorter
static const size_t SYSLOG_MAX_MESSAGE = 480;

struct SyslogFacility {
    const char* name;
    uint8_t code;
};

// Facilities offered in the settings: the usual ones for an appliance
static const SyslogFacility SYSLOG_FACILITIES[] = {
    { "user", 1 },    { "daemon", 3 },  { "local0", 16 }, { "local1", 17 }, { "local2", 18 },
    { "local3", 19 }, { "local4", 20 }, { "local5", 21 }, { "local6", 22 }, { "local7", 23 },
};
static const uint8_t SYSLOG_FACILITY_COUNT = sizeof(SYSLOG_FACILITIES) / sizeof(SYSLOG_FACILITIES[0]);

// Name of an offered facility code, nullptr for any other code
const char* syslogFacilityName(uint8_t code);
// Code of an offered facility name; false for any other name
bool parseSyslogFacility(const char* name, uint8_t& code);

const char* syslogProtocolName(SyslogProtocol protocol);  // "rfc3164", "rfc5424"
bool parseSyslogProtocol(const char* name, SyslogProtocol& out);

/**
 * Writes one syslog message into out (NUL terminated, at most cap - 1 characters, cut
 * if longer). Returns the length written, 0 when cap is too small for the header.
 */
size_t formatSyslog(char* out, size_t cap, SyslogProtocol protocol, uint8_t facility, LogLevel level,
                    const char* hostname, const char* app, const char* msg);

// RFC 5424 severity 5: the changes of the remote logging itself (LogRecord::notice)
static const uint8_t SYSLOG_SEVERITY_NOTICE = 5;

// Same, with the severity given directly (0-7)
size_t formatSyslogSeverity(char* out, size_t cap, SyslogProtocol protocol, uint8_t facility, uint8_t severity,
                            const char* hostname, const char* app, const char* msg);

#endif // SYSLOG_FORMAT_H
