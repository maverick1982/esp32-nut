#include <unity.h>
#include <string.h>
#include "LogLevel.h"
#include "LogOutputFilter.h"
#include "SyslogFormat.h"
#include "SyslogSettings.h"
#include "SyslogChange.h"

// Logging core (issue #69): levels, syslog format, per-output filter, syslog settings

void setUp(void) {}
void tearDown(void) {}

// --- Levels ---

void test_levels_from_the_logger_strings(void) {
    TEST_ASSERT_TRUE(logLevelOf("INFO") == LogLevel::INFO);
    TEST_ASSERT_TRUE(logLevelOf("WARN") == LogLevel::WARN);
    TEST_ASSERT_TRUE(logLevelOf("ERROR") == LogLevel::ERROR);
    TEST_ASSERT_TRUE(logLevelOf("warning") == LogLevel::WARN);
    // Unknown or missing levels count as INFO, so the line is still delivered
    TEST_ASSERT_TRUE(logLevelOf("DEBUG") == LogLevel::INFO);
    TEST_ASSERT_TRUE(logLevelOf(nullptr) == LogLevel::INFO);
}

void test_strict_level_parse_for_the_api(void) {
    LogLevel l = LogLevel::INFO;
    TEST_ASSERT_TRUE(parseLogLevel("error", l));
    TEST_ASSERT_TRUE(l == LogLevel::ERROR);
    TEST_ASSERT_FALSE(parseLogLevel("WARNING", l));
    TEST_ASSERT_FALSE(parseLogLevel("", l));
    TEST_ASSERT_FALSE(parseLogLevel(nullptr, l));
}

void test_syslog_severities(void) {
    TEST_ASSERT_EQUAL_UINT8(3, syslogSeverity(LogLevel::ERROR));
    TEST_ASSERT_EQUAL_UINT8(4, syslogSeverity(LogLevel::WARN));
    TEST_ASSERT_EQUAL_UINT8(6, syslogSeverity(LogLevel::INFO));
}

// --- Syslog format ---

void test_rfc3164_message(void) {
    char out[SYSLOG_MAX_MESSAGE];
    size_t n = formatSyslog(out, sizeof(out), SyslogProtocol::RFC3164, 16, LogLevel::WARN, "esp32-nut-a1b2c3",
                            "esp32-nut", "[NETWORK] Wi-Fi connection lost!");
    // local0 (16) * 8 + warning (4) = 132; no timestamp: the receiver stamps it
    TEST_ASSERT_EQUAL_STRING("<132>esp32-nut: [NETWORK] Wi-Fi connection lost!", out);
    TEST_ASSERT_EQUAL(strlen(out), n);
}

void test_rfc5424_message(void) {
    char out[SYSLOG_MAX_MESSAGE];
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC5424, 1, LogLevel::ERROR, "esp32-nut-a1b2c3", "esp32-nut",
                 "[USB] Recovery exhausted");
    // user (1) * 8 + error (3) = 11, NILVALUE for timestamp, procid, msgid and SD
    TEST_ASSERT_EQUAL_STRING("<11>1 - esp32-nut-a1b2c3 esp32-nut - - - [USB] Recovery exhausted", out);
}

void test_message_control_characters_and_trailing_newline(void) {
    char out[SYSLOG_MAX_MESSAGE];
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC3164, 16, LogLevel::INFO, "h", "esp32-nut",
                 "Connection established! IP Address: 10.0.0.5\n");
    TEST_ASSERT_EQUAL_STRING("<134>esp32-nut: Connection established! IP Address: 10.0.0.5", out);

    // A line with a newline inside stays one datagram
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC3164, 16, LogLevel::INFO, "h", "esp32-nut",
                 "\n--- ESP32 NUT Server Initialized ---\r\n\tok");
    TEST_ASSERT_EQUAL_STRING("<134>esp32-nut:  --- ESP32 NUT Server Initialized ---   ok", out);
}

void test_rfc3164_tag_and_rfc5424_tokens_are_cleaned(void) {
    char out[SYSLOG_MAX_MESSAGE];
    // TAG: alphanumeric, '-' and '_', at most 32 characters
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC3164, 16, LogLevel::INFO, "h",
                 "esp32 nut:app/0123456789012345678901234567890123", "m");
    TEST_ASSERT_EQUAL_STRING("<134>esp32nutapp012345678901234567890: m", out);

    // HOSTNAME and APP-NAME: printable ASCII without spaces, "-" when empty
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC5424, 16, LogLevel::INFO, "my host", "", "m");
    TEST_ASSERT_EQUAL_STRING("<134>1 - myhost - - - - m", out);
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC5424, 16, LogLevel::INFO, nullptr, nullptr, nullptr);
    TEST_ASSERT_EQUAL_STRING("<134>1 - - - - - -", out);
}

void test_long_message_is_cut_to_the_buffer(void) {
    char longmsg[600];
    memset(longmsg, 'x', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';
    char out[SYSLOG_MAX_MESSAGE];
    size_t n = formatSyslog(out, sizeof(out), SyslogProtocol::RFC3164, 16, LogLevel::INFO, "h", "esp32-nut", longmsg);
    TEST_ASSERT_EQUAL(SYSLOG_MAX_MESSAGE - 1, n);
    TEST_ASSERT_EQUAL(SYSLOG_MAX_MESSAGE - 1, strlen(out));

    // Not even room for the header: nothing written
    char tiny[8];
    TEST_ASSERT_EQUAL(0, formatSyslog(tiny, sizeof(tiny), SyslogProtocol::RFC5424, 16, LogLevel::INFO, "h",
                                      "esp32-nut", "m"));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_invalid_facility_falls_back_to_user(void) {
    char out[SYSLOG_MAX_MESSAGE];
    formatSyslog(out, sizeof(out), SyslogProtocol::RFC3164, 99, LogLevel::INFO, "h", "esp32-nut", "m");
    TEST_ASSERT_EQUAL_STRING("<14>esp32-nut: m", out);
}

void test_facility_and_protocol_names(void) {
    uint8_t code = 0;
    TEST_ASSERT_TRUE(parseSyslogFacility("local7", code));
    TEST_ASSERT_EQUAL_UINT8(23, code);
    TEST_ASSERT_TRUE(parseSyslogFacility("DAEMON", code));
    TEST_ASSERT_EQUAL_UINT8(3, code);
    TEST_ASSERT_FALSE(parseSyslogFacility("kern", code));  // not offered
    TEST_ASSERT_EQUAL_STRING("local0", syslogFacilityName(16));
    TEST_ASSERT_NULL(syslogFacilityName(0));

    SyslogProtocol p = SyslogProtocol::RFC3164;
    TEST_ASSERT_TRUE(parseSyslogProtocol("RFC5424", p));
    TEST_ASSERT_TRUE(p == SyslogProtocol::RFC5424);
    TEST_ASSERT_FALSE(parseSyslogProtocol("rfc6587", p));
    TEST_ASSERT_EQUAL_STRING("rfc3164", syslogProtocolName(SyslogProtocol::RFC3164));
}

// --- Output filter ---

void test_filter_minimum_level(void) {
    LogOutputFilter f;
    f.configure(LogLevel::WARN, 10);
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, 0) == LogOutputFilter::Decision::BELOW_LEVEL);
    TEST_ASSERT_TRUE(f.check(LogLevel::WARN, 0) == LogOutputFilter::Decision::SEND);
    TEST_ASSERT_TRUE(f.check(LogLevel::ERROR, 0) == LogOutputFilter::Decision::SEND);
    // Lines below the level are not counted as suppressed
    TEST_ASSERT_EQUAL_UINT32(0, f.takeSuppressed());
}

void test_filter_burst_then_rate(void) {
    LogOutputFilter f;
    f.configure(LogLevel::INFO, 10);
    TEST_ASSERT_EQUAL_UINT16(40, f.burst());
    uint32_t t = 1000;
    for (int i = 0; i < 40; i++) TEST_ASSERT_TRUE(f.check(LogLevel::INFO, t) == LogOutputFilter::Decision::SEND);
    // Burst used up: the next lines in the same millisecond are refused and counted
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(f.check(LogLevel::ERROR, t) == LogOutputFilter::Decision::RATE_LIMITED);
    }
    TEST_ASSERT_EQUAL_UINT32(5, f.takeSuppressed());
    TEST_ASSERT_EQUAL_UINT32(0, f.takeSuppressed());  // reported once

    // 10 lines per second: one more line every 100 ms
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, t + 99) == LogOutputFilter::Decision::RATE_LIMITED);
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, t + 101) == LogOutputFilter::Decision::SEND);
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, t + 150) == LogOutputFilter::Decision::RATE_LIMITED);
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, t + 205) == LogOutputFilter::Decision::SEND);
}

void test_filter_refill_stops_at_the_burst(void) {
    LogOutputFilter f;
    f.configure(LogLevel::INFO, 1);
    TEST_ASSERT_EQUAL_UINT16(20, f.burst());  // at least 20 lines
    uint32_t t = 0;
    for (int i = 0; i < 20; i++) f.check(LogLevel::INFO, t);
    // An hour later the bucket holds the burst again, not 3600 lines
    t += 3600000;
    int sent = 0;
    for (int i = 0; i < 100; i++) {
        if (f.check(LogLevel::INFO, t) == LogOutputFilter::Decision::SEND) sent++;
    }
    TEST_ASSERT_EQUAL(20, sent);
}

void test_filter_millis_wraparound(void) {
    LogOutputFilter f;
    f.configure(LogLevel::INFO, 10);
    uint32_t t = 0xFFFFFF00u;
    for (int i = 0; i < 40; i++) f.check(LogLevel::INFO, t);
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, t) == LogOutputFilter::Decision::RATE_LIMITED);
    // 0x100 + 0x64 ms later, across the wrap: about 3 lines refilled
    TEST_ASSERT_TRUE(f.check(LogLevel::INFO, 0x64u) == LogOutputFilter::Decision::SEND);
}

void test_filter_rate_is_clamped(void) {
    LogOutputFilter f;
    f.configure(LogLevel::INFO, 0);
    TEST_ASSERT_EQUAL_UINT16(LogOutputFilter::MIN_RATE, f.rate());
    f.configure(LogLevel::INFO, 5000);
    TEST_ASSERT_EQUAL_UINT16(LogOutputFilter::MAX_RATE, f.rate());
}

// --- Syslog settings ---

void test_settings_defaults(void) {
    SyslogSettings s = syslogDefaults();
    TEST_ASSERT_FALSE(s.enabled);
    TEST_ASSERT_EQUAL_UINT16(514, s.port);
    TEST_ASSERT_TRUE(s.protocol == SyslogProtocol::RFC3164);
    TEST_ASSERT_EQUAL_UINT8(16, s.facility);
    TEST_ASSERT_TRUE(s.min_level == LogLevel::INFO);
    TEST_ASSERT_EQUAL_UINT16(10, s.rate);
}

void test_settings_host_validation(void) {
    TEST_ASSERT_TRUE(validSyslogHost("192.168.1.10"));
    TEST_ASSERT_TRUE(validSyslogHost("rsyslog-01.home.lan"));
    TEST_ASSERT_FALSE(validSyslogHost(""));
    TEST_ASSERT_FALSE(validSyslogHost("host name"));
    TEST_ASSERT_FALSE(validSyslogHost("udp://host"));
    TEST_ASSERT_FALSE(validSyslogHost(".lan"));
    TEST_ASSERT_FALSE(validSyslogHost("host."));
    TEST_ASSERT_FALSE(validSyslogHost(String(std::string(64, 'a').c_str())));
}

void test_settings_sanitize(void) {
    SyslogSettings s = { true, String("bad host"), 0, (SyslogProtocol)7, 2, (LogLevel)9, 0 };
    SyslogSettings c = sanitize(s);
    TEST_ASSERT_FALSE(c.enabled);  // enabled without a valid host would only fail
    TEST_ASSERT_EQUAL_STRING("", c.host.c_str());
    TEST_ASSERT_EQUAL_UINT16(514, c.port);
    TEST_ASSERT_TRUE(c.protocol == SyslogProtocol::RFC3164);
    TEST_ASSERT_EQUAL_UINT8(16, c.facility);
    TEST_ASSERT_TRUE(c.min_level == LogLevel::INFO);
    TEST_ASSERT_EQUAL_UINT16(10, c.rate);

    SyslogSettings ok = { true, String("10.0.0.5"), 1514, SyslogProtocol::RFC5424, 23, LogLevel::ERROR, 50 };
    SyslogSettings same = sanitize(ok);
    TEST_ASSERT_TRUE(same.enabled);
    TEST_ASSERT_EQUAL_UINT16(1514, same.port);
    TEST_ASSERT_EQUAL_UINT16(50, same.rate);
}

// --- Notices and change log ---

void test_notice_severity(void) {
    char out[SYSLOG_MAX_MESSAGE];
    // local0 (16) * 8 + notice (5) = 133
    formatSyslogSeverity(out, sizeof(out), SyslogProtocol::RFC3164, 16, SYSLOG_SEVERITY_NOTICE, "h", "esp32-nut",
                         "[SYSLOG] Remote logging disabled by 'admin'");
    TEST_ASSERT_EQUAL_STRING("<133>esp32-nut: [SYSLOG] Remote logging disabled by 'admin'", out);
    // Out of range severity: informational
    formatSyslogSeverity(out, sizeof(out), SyslogProtocol::RFC3164, 16, 9, "h", "esp32-nut", "m");
    TEST_ASSERT_EQUAL_STRING("<134>esp32-nut: m", out);
}

static SyslogSettings on(const char* host, LogLevel level) {
    SyslogSettings s = { true, String(host), 514, SyslogProtocol::RFC3164, 16, level, 10 };
    return s;
}

void test_change_boot(void) {
    SyslogSettings off = syslogDefaults();
    SyslogChangeLog c = planSyslogChange(off, on("10.0.0.5", LogLevel::WARN), String(""));
    // Loading the saved settings is not a change: an ordinary line, no notice
    TEST_ASSERT_EQUAL_STRING("", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("", c.to_new.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "[SYSLOG] Remote logging on: 10.0.0.5:514, RFC 3164, facility local0, WARN and above, max 10 lines/s",
        c.local.c_str());
    // Off at boot: nothing to say
    c = planSyslogChange(off, off, String(""));
    TEST_ASSERT_EQUAL_STRING("", c.local.c_str());
}

void test_change_enable(void) {
    SyslogChangeLog c = planSyslogChange(syslogDefaults(), on("rsyslog.lan", LogLevel::ERROR), String("admin"));
    TEST_ASSERT_EQUAL_STRING("", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Remote logging enabled by 'admin': rsyslog.lan:514, RFC 3164, facility local0, "
                             "ERROR and above, max 10 lines/s",
                             c.to_new.c_str());
    TEST_ASSERT_EQUAL_STRING("", c.local.c_str());
}

void test_change_disable_goes_to_the_old_server(void) {
    SyslogSettings off = on("10.0.0.5", LogLevel::INFO);
    off.enabled = false;
    SyslogChangeLog c = planSyslogChange(on("10.0.0.5", LogLevel::INFO), off, String("admin"));
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Remote logging disabled by 'admin'", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("", c.to_new.c_str());
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Remote logging disabled by 'admin'", c.local.c_str());
}

void test_change_level(void) {
    SyslogChangeLog c = planSyslogChange(on("10.0.0.5", LogLevel::INFO), on("10.0.0.5", LogLevel::ERROR), String("admin"));
    TEST_ASSERT_EQUAL_STRING("", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Settings changed by 'admin': minimum level INFO -> ERROR", c.to_new.c_str());
}

void test_change_other_settings_and_no_change(void) {
    SyslogSettings a = on("10.0.0.5", LogLevel::INFO);
    SyslogSettings b = a;
    b.protocol = SyslogProtocol::RFC5424;
    b.facility = 3;
    b.rate = 25;
    SyslogChangeLog c = planSyslogChange(a, b, String("admin"));
    TEST_ASSERT_EQUAL_STRING(
        "[SYSLOG] Settings changed by 'admin': format rfc3164 -> rfc5424, facility local0 -> daemon, max lines/s 10 -> 25",
        c.to_new.c_str());
    // Saved without changes: nothing
    c = planSyslogChange(a, a, String("admin"));
    TEST_ASSERT_EQUAL_STRING("", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("", c.to_new.c_str());
    TEST_ASSERT_EQUAL_STRING("", c.local.c_str());
}

void test_change_server_moved(void) {
    SyslogSettings b = on("rsyslog2.lan", LogLevel::INFO);
    b.port = 1514;
    SyslogChangeLog c = planSyslogChange(on("rsyslog1.lan", LogLevel::INFO), b, String("admin"));
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Remote logging moved to rsyslog2.lan:1514 by 'admin'", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Remote logging moved here from rsyslog1.lan:514 by 'admin': rsyslog2.lan:1514, "
                             "RFC 3164, facility local0, INFO and above, max 10 lines/s",
                             c.to_new.c_str());
}

void test_change_while_off_is_local_only(void) {
    SyslogSettings a = syslogDefaults();
    SyslogSettings b = a;
    b.host = "10.0.0.5";
    SyslogChangeLog c = planSyslogChange(a, b, String("admin"));
    TEST_ASSERT_EQUAL_STRING("", c.to_old.c_str());
    TEST_ASSERT_EQUAL_STRING("", c.to_new.c_str());
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Settings saved by 'admin' (remote logging off)", c.local.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_levels_from_the_logger_strings);
    RUN_TEST(test_strict_level_parse_for_the_api);
    RUN_TEST(test_syslog_severities);
    RUN_TEST(test_rfc3164_message);
    RUN_TEST(test_rfc5424_message);
    RUN_TEST(test_message_control_characters_and_trailing_newline);
    RUN_TEST(test_rfc3164_tag_and_rfc5424_tokens_are_cleaned);
    RUN_TEST(test_long_message_is_cut_to_the_buffer);
    RUN_TEST(test_invalid_facility_falls_back_to_user);
    RUN_TEST(test_facility_and_protocol_names);
    RUN_TEST(test_filter_minimum_level);
    RUN_TEST(test_filter_burst_then_rate);
    RUN_TEST(test_filter_refill_stops_at_the_burst);
    RUN_TEST(test_filter_millis_wraparound);
    RUN_TEST(test_filter_rate_is_clamped);
    RUN_TEST(test_settings_defaults);
    RUN_TEST(test_settings_host_validation);
    RUN_TEST(test_settings_sanitize);
    RUN_TEST(test_notice_severity);
    RUN_TEST(test_change_boot);
    RUN_TEST(test_change_enable);
    RUN_TEST(test_change_disable_goes_to_the_old_server);
    RUN_TEST(test_change_level);
    RUN_TEST(test_change_other_settings_and_no_change);
    RUN_TEST(test_change_server_moved);
    RUN_TEST(test_change_while_off_is_local_only);
    return UNITY_END();
}
