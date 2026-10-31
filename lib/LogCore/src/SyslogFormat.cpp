#include "SyslogFormat.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

const char* syslogFacilityName(uint8_t code) {
    for (uint8_t i = 0; i < SYSLOG_FACILITY_COUNT; i++) {
        if (SYSLOG_FACILITIES[i].code == code) return SYSLOG_FACILITIES[i].name;
    }
    return nullptr;
}

bool parseSyslogFacility(const char* name, uint8_t& code) {
    if (!name) return false;
    for (uint8_t i = 0; i < SYSLOG_FACILITY_COUNT; i++) {
        if (strcasecmp(name, SYSLOG_FACILITIES[i].name) == 0) {
            code = SYSLOG_FACILITIES[i].code;
            return true;
        }
    }
    return false;
}

const char* syslogProtocolName(SyslogProtocol protocol) {
    return protocol == SyslogProtocol::RFC5424 ? "rfc5424" : "rfc3164";
}

bool parseSyslogProtocol(const char* name, SyslogProtocol& out) {
    if (!name) return false;
    if (strcasecmp(name, "rfc3164") == 0) {
        out = SyslogProtocol::RFC3164;
        return true;
    }
    if (strcasecmp(name, "rfc5424") == 0) {
        out = SyslogProtocol::RFC5424;
        return true;
    }
    return false;
}

// Copies the characters accepted by keep() (at most max), "-" when none is left
static size_t copyToken(char* out, size_t cap, const char* in, size_t max, bool (*keep)(char)) {
    size_t n = 0;
    if (in) {
        for (const char* p = in; *p && n < max && n + 1 < cap; p++) {
            if (keep(*p)) out[n++] = *p;
        }
    }
    if (n == 0 && cap > 1) out[n++] = '-';
    out[n] = '\0';
    return n;
}

// RFC 5424 PRINTUSASCII: %d33-126
static bool isPrintUsAscii(char c) {
    return c >= 33 && c <= 126;
}

// RFC 3164 §4.1.3: the TAG is alphanumeric; '-' and '_' are kept, as most senders do
static bool isTagChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

size_t formatSyslog(char* out, size_t cap, SyslogProtocol protocol, uint8_t facility, LogLevel level,
                    const char* hostname, const char* app, const char* msg) {
    return formatSyslogSeverity(out, cap, protocol, facility, syslogSeverity(level), hostname, app, msg);
}

size_t formatSyslogSeverity(char* out, size_t cap, SyslogProtocol protocol, uint8_t facility, uint8_t severity,
                            const char* hostname, const char* app, const char* msg) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (facility > 23) facility = 1;  // user
    if (severity > 7) severity = 6;   // informational
    unsigned pri = facility * 8u + severity;

    char host[64];
    char tag[49];
    int header;
    if (protocol == SyslogProtocol::RFC5424) {
        copyToken(host, sizeof(host), hostname, 63, isPrintUsAscii);
        copyToken(tag, sizeof(tag), app, 48, isPrintUsAscii);
        header = snprintf(out, cap, "<%u>1 - %s %s - - - ", pri, host, tag);
    } else {
        copyToken(tag, sizeof(tag), app, 32, isTagChar);
        header = snprintf(out, cap, "<%u>%s: ", pri, tag);
    }
    if (header < 0 || (size_t)header >= cap) {
        out[0] = '\0';
        return 0;
    }

    size_t n = (size_t)header;
    if (msg) {
        for (const char* p = msg; *p && n + 1 < cap; p++) {
            unsigned char c = (unsigned char)*p;
            out[n++] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
        }
    }
    // Trailing blanks (log lines often end with "\n") would show up in the receiver
    while (n > (size_t)header && out[n - 1] == ' ') n--;
    // No message text: no separator either (RFC 5424 "[SP MSG]")
    if (n == (size_t)header && n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return n;
}
