/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * server/netset.c (checks of a SET VAR):
 *   Copyright (C) 2003 Russell Kroll <rkroll@exploits.org>
 *
 * server/desc.c (lookup of the variable descriptions):
 *   Copyright (C) 2003 Russell Kroll <rkroll@exploits.org>
 *
 * data/cmdvartab (variable descriptions): Network UPS Tools project.
 */

#ifndef UPS_VARIABLES_H
#define UPS_VARIABLES_H

// Writable variables and variable descriptions as the NUT server and the web API see
// them, without HID (US-103). The HID -> NUT tables stay in NutTable.h.

#include <stdint.h>
#include <string.h>
#include <vector>

namespace NutTable {

// Case-insensitive equality, as strcasecmp() in upstream lookups
inline bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}

inline int compareIgnoreCase(const char* a, const char* b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// A writable variable as the NUT server reports it
struct RWVariable {
    const char* name;
    uint8_t aux;
    std::vector<const char*> enums;
    bool driver_side; // value kept by the firmware (HU_FLAG_ABSENT rows, ups.delay.*), US-068
};

// Outcome of a SET VAR (US-067): the STAT_SET_* results of upstream's setvar(), plus the
// host without a device. HANDLED is OK.
enum class SetStatus { OK, UNKNOWN, CONVERSION_FAILED, FAILED, NOT_CONNECTED };

// Outcome of the value checks of a SET VAR, before anything reaches the UPS (US-068)
enum class SetCheck { OK, NOT_WRITABLE, INVALID_AUX, TOO_LONG, INVALID_VALUE };

// Steps 5-8 of set_var() in upstream's server/netset.c, shared by the NUT server and the
// Web UI: the variable must be in the LIST RW catalog (strcasecmp, else READONLY), aux < 1
// is the "insanity from the driver" (SET-FAILED), then the length (TOO-LONG) and the ENUM
// values compared with strcmp (INVALID-VALUE). Every writable variable is ST_FLAG_STRING
// (US-066) and usbhid-ups declares no ranges. var points to the matching entry whenever
// the lookup succeeds, also when a later check fails; nullptr for NOT_WRITABLE.
inline SetCheck checkSetValue(const std::vector<RWVariable>& rw, const char* name, const char* value,
                              const RWVariable*& var) {
    var = nullptr;
    for (const auto& v : rw) {
        if (equalsIgnoreCase(v.name, name)) {
            var = &v;
            break;
        }
    }
    if (!var) return SetCheck::NOT_WRITABLE;
    if (var->aux < 1) return SetCheck::INVALID_AUX;
    if (!value) value = "";
    if (strlen(value) > var->aux) return SetCheck::TOO_LONG;
    if (!var->enums.empty()) {
        for (const char* e : var->enums) {
            if (strcmp(e, value) == 0) return SetCheck::OK;
        }
        return SetCheck::INVALID_VALUE;
    }
    return SetCheck::OK;
}

// VARDESC lines of nut_repo/data/cmdvartab for the variables of the tables, matched
// case-insensitively as desc.c:list_get(). nullptr when cmdvartab has none.
inline const char* describe(const char* name) {
    struct Desc { const char* name; const char* desc; };
    static const Desc descs[] = {
        { "battery.charge.low", "Remaining battery level when UPS switches to LB (percent)" },
        { "battery.charge.restart", "Minimum battery level for restart after power off (percent)" },
        { "battery.energysave", "Switch off when running on battery and no/low load" },
        { "battery.energysave.delay", "Delay before switch off UPS if on battery and load level low (min)" },
        { "battery.energysave.load", "Switch off UPS if on battery and load level lower (percent)" },
        { "battery.energysave.realpower", "Switch off UPS if on battery and load level lower (Watts)" },
        { "battery.mfr.date", "Battery manufacturing date" },
        { "battery.protection", "Prevent deep discharge of battery" },
        { "battery.runtime.low", "Remaining battery runtime when UPS switches to LB (seconds)" },
        { "input.bypass.switchable", "Input auto transfer on Bypass when overload or out of tolerance (enabled or disabled)" },
        { "input.frequency.extended", "Extended input frequency range" },
        { "input.sensitivity", "Input power sensitivity" },
        { "input.transfer.boost.high", "High voltage boosting transfer point (V)" },
        { "input.transfer.boost.low", "Low voltage boosting transfer point (V)" },
        { "input.transfer.bypass.forced", "Rule for allow auto Bypass switch (on/off) transfer modes (enabled or disabled)" },
        { "input.transfer.bypass.high", "High voltage Bypass transfer point (V)" },
        { "input.transfer.bypass.low", "Low voltage Bypass transfer point (V)" },
        { "input.transfer.bypass.outlimits", "Rule for auto transfer on Bypass when out of tolerance (enabled or disabled)" },
        { "input.transfer.bypass.overload", "Rule for auto transfer on Bypass when overload (enabled or disabled)" },
        { "input.transfer.eco.high", "High voltage ECO transfer point (V)" },
        { "input.transfer.eco.low", "Low voltage ECO transfer point (V)" },
        { "input.transfer.frequency.bypass.range", "Frequency range Bypass transfer point (percent of nominal Hz)" },
        { "input.transfer.frequency.eco.range", "Frequency range ECO transfer point (percent of nominal Hz)" },
        { "input.transfer.high", "High voltage transfer point (V)" },
        { "input.transfer.hysteresis", "Threshold of switching protection modes, voltage transfer point (V)" },
        { "input.transfer.low", "Low voltage transfer point (V)" },
        { "input.transfer.trim.high", "High voltage trimming transfer point (V)" },
        { "input.transfer.trim.low", "Low voltage trimming transfer point (V)" },
        { "input.voltage.extended", "Extended input voltage range" },
        { "outlet.1.autoswitch.charge.low", "Remaining battery level to power off this outlet (percent)" },
        { "outlet.1.delay.shutdown", "Interval to wait before shutting down this outlet (seconds)" },
        { "outlet.1.delay.start", "Interval to wait before restarting this outlet (seconds)" },
        { "outlet.1.desc", "Outlet description" },
        { "outlet.2.autoswitch.charge.low", "Remaining battery level to power off this outlet (percent)" },
        { "outlet.2.delay.shutdown", "Interval to wait before shutting down this outlet (seconds)" },
        { "outlet.2.delay.start", "Interval to wait before restarting this outlet (seconds)" },
        { "outlet.2.desc", "Outlet description" },
        { "outlet.2.switchable", "Outlet switch ability" },
        { "outlet.3.autoswitch.charge.low", nullptr /* not in cmdvartab */ },
        { "outlet.3.delay.shutdown", nullptr /* not in cmdvartab */ },
        { "outlet.3.delay.start", nullptr /* not in cmdvartab */ },
        { "outlet.3.desc", nullptr /* not in cmdvartab */ },
        { "outlet.3.switchable", nullptr /* not in cmdvartab */ },
        { "outlet.desc", "Outlet description" },
        { "outlet.power", nullptr /* not in cmdvartab */ },
        { "output.voltage.nominal", "Nominal output voltage (V)" },
        { "ups.date", "Internal UPS clock date" },
        { "ups.delay.shutdown", "Interval to wait after shutdown with delay command (seconds)" },
        { "ups.delay.start", "Interval to wait before (re)starting the load (seconds)" },
        { "ups.load.high", "Load when UPS switches to overload condition (percent)" },
        { "ups.shutdown", "Enable or disable UPS shutdown ability (poweroff)" },
        { "ups.start.auto", "UPS starts when mains is (re)applied" },
        { "ups.start.battery", "Allow to start UPS from battery" },
        { "ups.start.reboot", "UPS reboots when power returns during shutdown delay" },
        { "ups.test.interval", "Interval between self tests (seconds)" },
        { "ups.time", "Internal UPS clock time" },
    };
    if (!name) return nullptr;
    for (const Desc& d : descs) {
        if (equalsIgnoreCase(d.name, name)) return d.desc;
    }
    return nullptr;
}

} // namespace NutTable

#endif // UPS_VARIABLES_H
