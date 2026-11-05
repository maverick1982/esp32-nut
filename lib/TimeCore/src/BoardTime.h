#ifndef BOARD_TIME_H
#define BOARD_TIME_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Board time of a System Log line, for GET /api/logs (US-118). Pure logic, native
 * tests.
 *
 * The time of a line is counted back from the moment the list is built, with an
 * unsigned difference of the two uptimes, as the Web UI does with the browser clock
 * (issue #60). So it holds for any line of the buffer: written before the first NTP
 * synchronization, across the millis() wrap, and older than 24.8 days (the limit of
 * WallClock::epochMsAt, which takes the difference as int32_t). Lines older than 49.7
 * days are ambiguous, as in the browser.
 *
 * The fields are formatted from the struct tm given: no localtime() or mktime() here,
 * so the native tests do not depend on the time zone of the host.
 */

// Unix time in ms of a line written at row_uptime_ms, given the Unix time of the moment
// the list is built (list_uptime_ms). Requires row_uptime_ms <= list_uptime_ms (modulo
// the wrap): every line is written before the list
inline int64_t boardEpochMs(int64_t list_epoch_ms, uint32_t list_uptime_ms, uint32_t row_uptime_ms) {
    return list_epoch_ms - (int64_t)(uint32_t)(list_uptime_ms - row_uptime_ms);
}

// Offset of the local time from UTC in seconds (+3600 for CET), from the same instant
// broken down twice
int32_t utcOffsetSeconds(const struct tm& local, const struct tm& utc);

// Length of "2026-11-05T09:12:03.007+01:00"
static const size_t BOARD_TIME_LEN = 29;

// ISO 8601 local time with milliseconds and the offset of the time zone. UTC gives
// "+00:00". Returns the length written, 0 when len < BOARD_TIME_LEN + 1
size_t formatBoardTime(const struct tm& local, const struct tm& utc, uint16_t ms, char* out, size_t len);

#endif // BOARD_TIME_H
