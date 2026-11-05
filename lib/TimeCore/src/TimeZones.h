#ifndef TIME_ZONES_H
#define TIME_ZONES_H

#include <stddef.h>

/**
 * @brief Time zones offered in Board Settings (US-117). Pure data, native tests.
 *
 * Each entry pairs an IANA name with its POSIX TZ rule, which newlib applies with
 * setenv("TZ") + tzset(), daylight saving time included. The rules come from the
 * zones.csv table of nayarsystems/posix_tz_db (tzdata). "UTC" is always the first entry
 * and the default.
 */
struct TimeZone {
    const char* name;   // IANA name, e.g. "Europe/Rome"
    const char* posix;  // POSIX TZ rule, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
};

static const char* const TIME_ZONE_DEFAULT = "UTC";

// The zone with this exact name, nullptr for an unknown name
const TimeZone* findTimeZone(const char* name);
size_t timeZoneCount();
// Entry i in display order, nullptr past the end
const TimeZone* timeZoneAt(size_t i);

#endif // TIME_ZONES_H
