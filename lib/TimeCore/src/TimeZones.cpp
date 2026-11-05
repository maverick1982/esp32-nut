#include "TimeZones.h"
#include <string.h>

// UTC first, then by region and from west to east. Rules from posix_tz_db (tzdata)
static const TimeZone TIME_ZONES[] = {
    { "UTC", "UTC0" },
    // Europe
    { "Atlantic/Azores", "<-01>1<+00>,M3.5.0/0,M10.5.0/1" },
    { "Atlantic/Reykjavik", "GMT0" },
    { "Atlantic/Canary", "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0" },
    { "Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Zagreb", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Belgrade", "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Istanbul", "<+03>-3" },
    { "Europe/Minsk", "<+03>-3" },
    { "Europe/Moscow", "MSK-3" },
    // Africa
    { "Africa/Casablanca", "<+01>-1" },
    { "Africa/Lagos", "WAT-1" },
    { "Africa/Algiers", "CET-1" },
    { "Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    { "Africa/Johannesburg", "SAST-2" },
    { "Africa/Nairobi", "EAT-3" },
    // Asia
    { "Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0" },
    { "Asia/Baghdad", "<+03>-3" },
    { "Asia/Riyadh", "<+03>-3" },
    { "Asia/Tehran", "<+0330>-3:30" },
    { "Asia/Dubai", "<+04>-4" },
    { "Asia/Karachi", "PKT-5" },
    { "Asia/Tashkent", "<+05>-5" },
    { "Asia/Almaty", "<+05>-5" },
    { "Asia/Kolkata", "IST-5:30" },
    { "Asia/Kathmandu", "<+0545>-5:45" },
    { "Asia/Dhaka", "<+06>-6" },
    { "Asia/Bangkok", "<+07>-7" },
    { "Asia/Ho_Chi_Minh", "<+07>-7" },
    { "Asia/Jakarta", "WIB-7" },
    { "Asia/Singapore", "<+08>-8" },
    { "Asia/Kuala_Lumpur", "<+08>-8" },
    { "Asia/Manila", "PST-8" },
    { "Asia/Shanghai", "CST-8" },
    { "Asia/Hong_Kong", "HKT-8" },
    { "Asia/Taipei", "CST-8" },
    { "Asia/Seoul", "KST-9" },
    { "Asia/Tokyo", "JST-9" },
    // Oceania
    { "Australia/Perth", "AWST-8" },
    { "Australia/Darwin", "ACST-9:30" },
    { "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Australia/Brisbane", "AEST-10" },
    { "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Australia/Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Pacific/Fiji", "<+12>-12" },
    { "Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Pacific/Honolulu", "HST10" },
    // Americas
    { "America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0" },
    { "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0" },
    { "America/Vancouver", "PST8PDT,M3.2.0,M11.1.0" },
    { "America/Phoenix", "MST7" },
    { "America/Denver", "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Edmonton", "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Mexico_City", "CST6" },
    { "America/Chicago", "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Bogota", "<-05>5" },
    { "America/Lima", "<-05>5" },
    { "America/New_York", "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Toronto", "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Caracas", "<-04>4" },
    { "America/Puerto_Rico", "AST4" },
    { "America/Halifax", "AST4ADT,M3.2.0,M11.1.0" },
    { "America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0" },
    { "America/Argentina/Buenos_Aires", "<-03>3" },
    { "America/Sao_Paulo", "<-03>3" },
    { "America/Montevideo", "<-03>3" },
};

static const size_t TIME_ZONE_COUNT = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);

const TimeZone* findTimeZone(const char* name) {
    if (!name || !*name) return nullptr;
    for (size_t i = 0; i < TIME_ZONE_COUNT; i++) {
        if (strcmp(TIME_ZONES[i].name, name) == 0) return &TIME_ZONES[i];
    }
    return nullptr;
}

size_t timeZoneCount() {
    return TIME_ZONE_COUNT;
}

const TimeZone* timeZoneAt(size_t i) {
    return i < TIME_ZONE_COUNT ? &TIME_ZONES[i] : nullptr;
}
