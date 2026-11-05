#include <unity.h>
#include <string.h>
#include "TimeZones.h"
#include "TimeSettings.h"
#include "WallClock.h"
#include "TimeStatus.h"
#include "BoardTime.h"

// Network time core (US-117): time zones, settings, time from the uptime, System Log lines

void setUp(void) {}
void tearDown(void) {}

// --- Time zones ---

void test_utc_is_first_and_the_default(void) {
    const TimeZone* first = timeZoneAt(0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("UTC", first->name);
    TEST_ASSERT_EQUAL_STRING("UTC0", first->posix);
    TEST_ASSERT_EQUAL_STRING("UTC", TIME_ZONE_DEFAULT);
}

void test_find_zone_with_daylight_saving(void) {
    const TimeZone* rome = findTimeZone("Europe/Rome");
    TEST_ASSERT_NOT_NULL(rome);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", rome->posix);
    const TimeZone* ny = findTimeZone("America/New_York");
    TEST_ASSERT_NOT_NULL(ny);
    TEST_ASSERT_EQUAL_STRING("EST5EDT,M3.2.0,M11.1.0", ny->posix);
}

void test_unknown_or_empty_zone(void) {
    TEST_ASSERT_NULL(findTimeZone("Europe/Atlantis"));
    TEST_ASSERT_NULL(findTimeZone("europe/rome"));  // exact IANA name only
    TEST_ASSERT_NULL(findTimeZone(""));
    TEST_ASSERT_NULL(findTimeZone(nullptr));
}

void test_zone_table_has_no_duplicates_and_no_empty_rules(void) {
    size_t n = timeZoneCount();
    TEST_ASSERT_TRUE(n > 50);
    TEST_ASSERT_NULL(timeZoneAt(n));
    for (size_t i = 0; i < n; i++) {
        const TimeZone* a = timeZoneAt(i);
        TEST_ASSERT_TRUE(strlen(a->name) > 0);
        TEST_ASSERT_TRUE(strlen(a->posix) > 0);
        TEST_ASSERT_TRUE(findTimeZone(a->name) == a);  // the first entry with that name
        for (size_t j = i + 1; j < n; j++) TEST_ASSERT_TRUE(strcmp(a->name, timeZoneAt(j)->name) != 0);
    }
}

// --- Settings ---

void test_time_defaults(void) {
    TimeSettings d = timeDefaults();
    TEST_ASSERT_TRUE(d.ntp);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", d.server.c_str());
    TEST_ASSERT_EQUAL_STRING("UTC", d.zone.c_str());
}

void test_valid_ntp_server(void) {
    TEST_ASSERT_TRUE(validNtpServer("pool.ntp.org"));
    TEST_ASSERT_TRUE(validNtpServer("192.168.1.1"));
    TEST_ASSERT_TRUE(validNtpServer("ntp-1.lan"));
    TEST_ASSERT_FALSE(validNtpServer(""));
    TEST_ASSERT_FALSE(validNtpServer("-ntp.lan"));
    TEST_ASSERT_FALSE(validNtpServer(".ntp.lan"));
    TEST_ASSERT_FALSE(validNtpServer("ntp.lan."));
    TEST_ASSERT_FALSE(validNtpServer("bad host"));
    TEST_ASSERT_FALSE(validNtpServer("ntp_1.lan"));
    String max(""), over("");
    for (int i = 0; i < 63; i++) max += "a";
    over = max + "a";
    TEST_ASSERT_TRUE(validNtpServer(max));
    TEST_ASSERT_FALSE(validNtpServer(over));
}

void test_sanitize_stored_values(void) {
    TimeSettings s = { true, String("pool.ntp.org"), String("Mars/Olympus") };
    TimeSettings out = sanitize(s);
    TEST_ASSERT_EQUAL_STRING("UTC", out.zone.c_str());
    TEST_ASSERT_TRUE(out.ntp);

    s = { true, String("bad host!"), String("Europe/Rome") };
    out = sanitize(s);
    TEST_ASSERT_EQUAL_STRING("", out.server.c_str());
    TEST_ASSERT_FALSE(out.ntp);  // NTP without a server would only fail
    TEST_ASSERT_EQUAL_STRING("Europe/Rome", out.zone.c_str());

    s = { false, String(""), String("Europe/Rome") };
    out = sanitize(s);
    TEST_ASSERT_FALSE(out.ntp);
    TEST_ASSERT_EQUAL_STRING("Europe/Rome", out.zone.c_str());
}

// --- Wall clock ---

void test_clock_invalid_until_synchronized(void) {
    WallClock c;
    int64_t t = 0;
    TEST_ASSERT_FALSE(c.valid());
    TEST_ASSERT_FALSE(c.epochMsAt(1000, t));
}

void test_clock_time_after_and_before_the_reference(void) {
    WallClock c;
    // 2026-11-04 18:44:03.000 UTC at 50 s of uptime
    const int64_t ref = 1793817843000LL;
    c.setSynced(ref, 50000);
    int64_t t = 0;
    TEST_ASSERT_TRUE(c.epochMsAt(50000, t));
    TEST_ASSERT_TRUE(t == ref);
    TEST_ASSERT_TRUE(c.epochMsAt(51234, t));
    TEST_ASSERT_TRUE(t == ref + 1234);
    // A line written at 2 s of uptime, before the first synchronization
    TEST_ASSERT_TRUE(c.epochMsAt(2000, t));
    TEST_ASSERT_TRUE(t == ref - 48000);
    TEST_ASSERT_EQUAL_UINT32(50000, c.syncedAt());
}

void test_clock_across_the_millis_wrap(void) {
    WallClock c;
    const int64_t ref = 1793817843000LL;
    c.setSynced(ref, 0xFFFFF000u);  // 4096 ms before the wrap
    int64_t t = 0;
    TEST_ASSERT_TRUE(c.epochMsAt(0x00000100u, t));  // 256 ms after the wrap
    TEST_ASSERT_TRUE(t == ref + 4096 + 256);
    TEST_ASSERT_TRUE(c.epochMsAt(0xFFFFE000u, t));
    TEST_ASSERT_TRUE(t == ref - 4096);
}

void test_clock_clear(void) {
    WallClock c;
    c.setSynced(1793817843000LL, 1000);
    c.clear();
    int64_t t = 0;
    TEST_ASSERT_FALSE(c.valid());
    TEST_ASSERT_FALSE(c.epochMsAt(2000, t));
}

// --- State and System Log lines ---

void test_state_names(void) {
    TEST_ASSERT_EQUAL_STRING("disabled", timeStateName(TimeState::Disabled));
    TEST_ASSERT_EQUAL_STRING("waiting", timeStateName(TimeState::Waiting));
    TEST_ASSERT_EQUAL_STRING("synchronized", timeStateName(TimeState::Synchronized));
    TEST_ASSERT_EQUAL_STRING("no_answer", timeStateName(TimeState::NoAnswer));
}

void test_one_line_at_the_first_synchronization(void) {
    TimeLogPolicy p;
    p.restart(1000);
    TEST_ASSERT_TRUE(p.tick(5000, true) == TimeLogPolicy::Line::None);
    TEST_ASSERT_TRUE(p.onSynced() == TimeLogPolicy::Line::Synchronized);
    // The periodic synchronizations of SNTP write nothing
    TEST_ASSERT_TRUE(p.onSynced() == TimeLogPolicy::Line::None);
    TEST_ASSERT_TRUE(p.tick(1000000, true) == TimeLogPolicy::Line::None);
}

void test_one_line_per_failure_period(void) {
    TimeLogPolicy p;
    p.restart(1000);
    TEST_ASSERT_TRUE(p.tick(1000 + TimeLogPolicy::NO_ANSWER_MS - 1, true) == TimeLogPolicy::Line::None);
    TEST_ASSERT_FALSE(p.noAnswer());
    TEST_ASSERT_TRUE(p.tick(1000 + TimeLogPolicy::NO_ANSWER_MS, true) == TimeLogPolicy::Line::NoAnswer);
    TEST_ASSERT_TRUE(p.noAnswer());
    TEST_ASSERT_TRUE(p.tick(500000, true) == TimeLogPolicy::Line::None);
    // The answer that ends the period gets its line
    TEST_ASSERT_TRUE(p.onSynced() == TimeLogPolicy::Line::Synchronized);
    TEST_ASSERT_FALSE(p.noAnswer());
}

void test_no_line_without_network(void) {
    TimeLogPolicy p;
    p.restart(0);
    TEST_ASSERT_TRUE(p.tick(TimeLogPolicy::NO_ANSWER_MS * 3, false) == TimeLogPolicy::Line::None);
    // The wait starts again when the network comes back
    TEST_ASSERT_TRUE(p.tick(TimeLogPolicy::NO_ANSWER_MS * 3 + 1000, true) == TimeLogPolicy::Line::None);
    TEST_ASSERT_TRUE(p.tick(TimeLogPolicy::NO_ANSWER_MS * 4, true) == TimeLogPolicy::Line::NoAnswer);
}

void test_new_line_after_new_settings_and_none_when_stopped(void) {
    TimeLogPolicy p;
    p.restart(0);
    TEST_ASSERT_TRUE(p.onSynced() == TimeLogPolicy::Line::Synchronized);
    p.restart(10000);  // new server or time zone
    TEST_ASSERT_TRUE(p.onSynced() == TimeLogPolicy::Line::Synchronized);
    p.stop();  // NTP turned off
    TEST_ASSERT_TRUE(p.tick(10000 + TimeLogPolicy::NO_ANSWER_MS * 2, true) == TimeLogPolicy::Line::None);
    TEST_ASSERT_FALSE(p.noAnswer());
}

// --- Board time of a System Log line (US-118) ---

static struct tm tmOf(int year, int mon, int day, int hour, int min, int sec) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    return t;
}

static const int64_t NOW_MS = 1793866323007LL;  // 2026-11-05T08:12:03.007Z

void test_board_epoch_of_the_newest_and_older_lines(void) {
    TEST_ASSERT_TRUE(boardEpochMs(NOW_MS, 50000, 50000) == NOW_MS);
    TEST_ASSERT_TRUE(boardEpochMs(NOW_MS, 50000, 45000) == NOW_MS - 5000);
    // Written at boot, long before the first synchronization
    TEST_ASSERT_TRUE(boardEpochMs(NOW_MS, 50000, 0) == NOW_MS - 50000);
}

void test_board_epoch_across_the_millis_wrap(void) {
    // List at 1000 ms after the wrap, line 1000 ms before it
    TEST_ASSERT_TRUE(boardEpochMs(NOW_MS, 1000, 0xFFFFFC18u) == NOW_MS - 2000);
}

void test_board_epoch_of_a_line_older_than_24_days(void) {
    // 30 days: beyond the int32_t difference of WallClock::epochMsAt
    const uint32_t age = 30u * 86400u * 1000u;
    TEST_ASSERT_TRUE(boardEpochMs(NOW_MS, age + 7000, 7000) == NOW_MS - (int64_t)age);
}

void test_utc_offset_of_common_zones(void) {
    struct tm utc = tmOf(2026, 11, 5, 8, 12, 3);
    TEST_ASSERT_EQUAL_INT32(0, utcOffsetSeconds(utc, utc));
    TEST_ASSERT_EQUAL_INT32(3600, utcOffsetSeconds(tmOf(2026, 11, 5, 9, 12, 3), utc));    // Rome, winter
    TEST_ASSERT_EQUAL_INT32(-18000, utcOffsetSeconds(tmOf(2026, 11, 5, 3, 12, 3), utc));  // New York
    TEST_ASSERT_EQUAL_INT32(19800, utcOffsetSeconds(tmOf(2026, 11, 5, 13, 42, 3), utc));  // India
    TEST_ASSERT_EQUAL_INT32(-12600, utcOffsetSeconds(tmOf(2026, 11, 5, 4, 42, 3), utc));  // Newfoundland
    struct tm utc_summer = tmOf(2026, 7, 1, 10, 0, 0);
    TEST_ASSERT_EQUAL_INT32(7200, utcOffsetSeconds(tmOf(2026, 7, 1, 12, 0, 0), utc_summer));  // Rome, summer
}

void test_utc_offset_across_days_months_and_years(void) {
    // Local 00:30 of 1 January, UTC still 23:30 of 31 December
    TEST_ASSERT_EQUAL_INT32(3600, utcOffsetSeconds(tmOf(2027, 1, 1, 0, 30, 0), tmOf(2026, 12, 31, 23, 30, 0)));
    // Local 31 October 20:00 in New York, UTC 1 November 01:00
    TEST_ASSERT_EQUAL_INT32(-18000, utcOffsetSeconds(tmOf(2026, 10, 31, 20, 0, 0), tmOf(2026, 11, 1, 1, 0, 0)));
    // Leap day
    TEST_ASSERT_EQUAL_INT32(3600, utcOffsetSeconds(tmOf(2028, 3, 1, 0, 30, 0), tmOf(2028, 2, 29, 23, 30, 0)));
}

void test_format_board_time(void) {
    char out[32];
    struct tm utc = tmOf(2026, 11, 5, 8, 12, 3);
    TEST_ASSERT_EQUAL_UINT32(29, formatBoardTime(tmOf(2026, 11, 5, 9, 12, 3), utc, 7, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("2026-11-05T09:12:03.007+01:00", out);
    formatBoardTime(utc, utc, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2026-11-05T08:12:03.000+00:00", out);
    formatBoardTime(tmOf(2026, 11, 5, 3, 12, 3), utc, 999, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2026-11-05T03:12:03.999-05:00", out);
    formatBoardTime(tmOf(2026, 11, 5, 13, 42, 3), utc, 500, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2026-11-05T13:42:03.500+05:30", out);
    formatBoardTime(tmOf(2026, 11, 5, 4, 42, 3), utc, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("2026-11-05T04:42:03.001-03:30", out);
}

void test_format_board_time_needs_room(void) {
    char out[29];
    struct tm utc = tmOf(2026, 11, 5, 8, 12, 3);
    TEST_ASSERT_EQUAL_UINT32(0, formatBoardTime(utc, utc, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, formatBoardTime(utc, utc, 0, nullptr, 64));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_utc_is_first_and_the_default);
    RUN_TEST(test_find_zone_with_daylight_saving);
    RUN_TEST(test_unknown_or_empty_zone);
    RUN_TEST(test_zone_table_has_no_duplicates_and_no_empty_rules);
    RUN_TEST(test_time_defaults);
    RUN_TEST(test_valid_ntp_server);
    RUN_TEST(test_sanitize_stored_values);
    RUN_TEST(test_clock_invalid_until_synchronized);
    RUN_TEST(test_clock_time_after_and_before_the_reference);
    RUN_TEST(test_clock_across_the_millis_wrap);
    RUN_TEST(test_clock_clear);
    RUN_TEST(test_state_names);
    RUN_TEST(test_one_line_at_the_first_synchronization);
    RUN_TEST(test_one_line_per_failure_period);
    RUN_TEST(test_no_line_without_network);
    RUN_TEST(test_new_line_after_new_settings_and_none_when_stopped);
    RUN_TEST(test_board_epoch_of_the_newest_and_older_lines);
    RUN_TEST(test_board_epoch_across_the_millis_wrap);
    RUN_TEST(test_board_epoch_of_a_line_older_than_24_days);
    RUN_TEST(test_utc_offset_of_common_zones);
    RUN_TEST(test_utc_offset_across_days_months_and_years);
    RUN_TEST(test_format_board_time);
    RUN_TEST(test_format_board_time_needs_room);
    return UNITY_END();
}
