#include "BoardTime.h"
#include <stdio.h>

// Days from 1970-01-01 of a proleptic Gregorian date (month 1-12). Howard Hinnant's
// days_from_civil, valid for any year of struct tm
static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t secondsOf(const struct tm& t) {
    int64_t days = daysFromCivil((int64_t)t.tm_year + 1900, (unsigned)(t.tm_mon + 1), (unsigned)t.tm_mday);
    return days * 86400 + (int64_t)t.tm_hour * 3600 + (int64_t)t.tm_min * 60 + t.tm_sec;
}

int32_t utcOffsetSeconds(const struct tm& local, const struct tm& utc) {
    return (int32_t)(secondsOf(local) - secondsOf(utc));
}

size_t formatBoardTime(const struct tm& local, const struct tm& utc, uint16_t ms, char* out, size_t len) {
    if (!out || len < BOARD_TIME_LEN + 1) return 0;
    int32_t offset = utcOffsetSeconds(local, utc);
    char sign = offset < 0 ? '-' : '+';
    if (offset < 0) offset = -offset;
    int32_t off_min = offset / 60;
    int n = snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03u%c%02d:%02d", local.tm_year + 1900,
                     local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
                     (unsigned)(ms % 1000), sign, (int)(off_min / 60), (int)(off_min % 60));
    if (n != (int)BOARD_TIME_LEN) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}
