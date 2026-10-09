#ifndef COMMAND_CATALOG_H
#define COMMAND_CATALOG_H

#include <stdint.h>
#include <string.h>
#include <vector>
#include <stdlib.h>
#include <limits.h>
#include "HIDUsages.h"
#include "UPSData.h"

// A NUT instant command the bridge knows about. Descriptions come verbatim from
// nut_repo/data/cmdvartab; destructive commands cut power to the load.
struct UPSCommandInfo {
    const char* name;
    const char* description;
    bool destructive;
};

enum class CommandResult { OK, NOT_SUPPORTED, NOT_CONNECTED, FAILED, INVALID_ARGUMENT };

namespace CommandCatalog {

inline const UPSCommandInfo* table(size_t& count) {
    static const UPSCommandInfo commands[] = {
        { "beeper.enable",            "Enable the UPS beeper",                             false },
        { "beeper.disable",           "Disable the UPS beeper",                            false },
        { "beeper.toggle",            "Toggle the UPS beeper",                             false },
        { "beeper.mute",              "Temporarily mute the UPS beeper",                   false },
        { "test.battery.start.quick", "Start a quick battery test",                        false },
        { "test.battery.start.deep",  "Start a deep battery test",                         false },
        { "test.battery.stop",        "Stop the battery test",                             false },
        { "test.panel.start",         "Start testing the UPS panel",                       false },
        { "test.panel.stop",          "Stop a UPS panel test",                             false },
        { "load.off",                 "Turn off the load immediately",                     true },
        { "load.on",                  "Turn on the load immediately",                      true },
        { "load.off.delay",           "Turn off the load with a delay (seconds)",          true },
        { "load.on.delay",            "Turn on the load with a delay (seconds)",           true },
        { "shutdown.return",          "Turn off the load and return when power is back",   true },
        { "shutdown.stayoff",         "Turn off the load and remain off",                  true },
        { "shutdown.stop",            "Stop a shutdown in progress",                       true },
        { "shutdown.reboot",          "Shut down the load briefly while rebooting the UPS", true },
        { "shutdown.default",         "Run the driver-defined UPS shutdown sequence (as opposed to user-configured 'sdcommands')", true },
    };
    count = sizeof(commands) / sizeof(commands[0]);
    return commands;
}

// Known command by name, nullptr otherwise. Independent of the attached device.
inline const UPSCommandInfo* find(const char* name) {
    if (!name) return nullptr;
    size_t count;
    const UPSCommandInfo* commands = table(count);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(commands[i].name, name) == 0) return &commands[i];
    }
    return nullptr;
}

// HID usages that realise a group of commands: standard paths, tried in order, and
// APC vendor usage codes (0xff86xxxx, not named by NUTUsages.h, so matched by code)
struct UsageGroup {
    const char* const* paths;
    size_t n_paths;
    const uint32_t* codes;
    size_t n_codes;
};

static const char* const kTestPaths[] = {
    "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test" };
static const uint32_t kPanelCodes[] = { 0xff860072 };     // APCPanelTest
static const char* const kShutdownPaths[] = {
    "UPS.PowerSummary.DelayBeforeShutdown", "UPS.Output.DelayBeforeShutdown" };
static const uint32_t kShutdownCodes[] = { 0xff86007d };  // APCDelayBeforeShutdown
static const char* const kStartupPaths[] = {
    "UPS.PowerSummary.DelayBeforeStartup", "UPS.Output.DelayBeforeStartup" };
static const uint32_t kStartupCodes[] = { 0xff86007e };   // APCDelayBeforeStartup
static const char* const kRebootPaths[] = {
    "UPS.PowerSummary.DelayBeforeReboot", "UPS.Output.DelayBeforeReboot" };
static const uint32_t kRebootCodes[] = { 0xff86007c };    // APCDelayBeforeReboot

static const UsageGroup kTest     = { kTestPaths, 3, nullptr, 0 };
static const UsageGroup kPanel    = { nullptr, 0, kPanelCodes, 1 };
static const UsageGroup kShutdown = { kShutdownPaths, 2, kShutdownCodes, 1 };
static const UsageGroup kStartup  = { kStartupPaths, 2, kStartupCodes, 1 };
static const UsageGroup kReboot   = { kRebootPaths, 2, kRebootCodes, 1 };

// First FEATURE usage of the group, paths in the group's order before the codes.
// Only FEATURE counts: some APC declare UPS.Battery.Test as INPUT as well.
inline const HIDUsageDef* findFeature(const std::vector<HIDUsageDef>& usages, const UsageGroup& g) {
    for (size_t i = 0; i < g.n_paths; i++) {
        for (const auto& u : usages) {
            if (u.report_type == 0x03 && strcmp(u.path, g.paths[i]) == 0) return &u;
        }
    }
    for (size_t i = 0; i < g.n_codes; i++) {
        for (const auto& u : usages) {
            if (u.report_type == 0x03 && u.usage == g.codes[i]) return &u;
        }
    }
    return nullptr;
}

inline bool hasFeature(const std::vector<HIDUsageDef>& usages, const UsageGroup& g) {
    return findFeature(usages, g) != nullptr;
}

// APC Back-UPS CS: shutdown.return is a single write of 1 on UPS.Output.APCDelayBeforeReboot
// (apc-hid.c, found by find_nut_info() before the generic entries). HIDParser names vendor
// usages by code ("UPS.Output.0xFF86007C"), so both the named path and the code directly
// under the Output collection match; UPS.APCGeneralCollection's field (0xFF860005) does not.
inline const HIDUsageDef* apcCsReturnField(const std::vector<HIDUsageDef>& usages) {
    static const char kOutput[] = "UPS.Output.";
    const size_t n = sizeof(kOutput) - 1;
    for (const auto& u : usages) {
        if (u.report_type != 0x03) continue;
        if (strcmp(u.path, "UPS.Output.APCDelayBeforeReboot") == 0) return &u;
        if (u.usage == 0xff86007c && strncmp(u.path, kOutput, n) == 0 && strchr(u.path + n, '.') == nullptr)
            return &u;
    }
    return nullptr;
}

// Reboot group without the APC Back-UPS CS field: in apc-hid.c UPS.Output.APCDelayBeforeReboot
// serves shutdown.return only (value 1); shutdown.reboot uses the standard DelayBeforeReboot
// paths and UPS.APCGeneralCollection.APCDelayBeforeReboot. findFeature() matches the vendor
// code on any path, so the CS field would otherwise receive the reboot delay.
inline const HIDUsageDef* rebootField(const std::vector<HIDUsageDef>& usages) {
    const HIDUsageDef* cs = apcCsReturnField(usages);
    for (size_t i = 0; i < kReboot.n_paths; i++) {
        for (const auto& u : usages) {
            if (u.report_type == 0x03 && strcmp(u.path, kReboot.paths[i]) == 0) return &u;
        }
    }
    for (size_t i = 0; i < kReboot.n_codes; i++) {
        for (const auto& u : usages) {
            if (&u != cs && u.report_type == 0x03 && u.usage == kReboot.codes[i]) return &u;
        }
    }
    return nullptr;
}

// Beeper field the host drives: first AudibleAlarmControl in descriptor order, as
// USBHostUPS::getActiveBeeperPath() picks it
inline const HIDUsageDef* activeBeeper(const std::vector<HIDUsageDef>& usages) {
    for (const auto& u : usages) {
        if (strcmp(u.path, "UPS.PowerSummary.AudibleAlarmControl") == 0 ||
            strcmp(u.path, "UPS.BatterySystem.Battery.AudibleAlarmControl") == 0 ||
            strcmp(u.path, "UPS.AudibleAlarmControl") == 0) {
            return &u;
        }
    }
    return nullptr;
}

// beeper.mute writes 3 (beeper_info "muted" in usbhid-ups): it needs a FEATURE field
// of at least 2 bits whose declared logical range, if any, reaches 3. A 1-bit beeper
// only knows on and off.
inline const HIDUsageDef* muteField(const std::vector<HIDUsageDef>& usages) {
    const HIDUsageDef* b = activeBeeper(usages);
    if (!b || b->report_type != 0x03 || b->bit_size < 2) return nullptr;
    bool range_declared = b->logical_max > b->logical_min;
    return (!range_declared || b->logical_max >= 3) ? b : nullptr;
}

/**
 * Commands the device supports, in table order. The command -> HID path mapping
 * follows usbhid-ups (the nut_repo/drivers/<brand>-hid.c tables) and the semantics of its
 * instcmd(): shutdown.return/stayoff need both DelayBeforeStartup and
 * DelayBeforeShutdown. The beeper commands follow beeper_available, the
 * condition LIST CMD has always used (supportsBeeperToggle() and a reported
 * ups.beeper.status). shutdown_cmds is IUPSDriver::shutdownCommandsSupported():
 * false hides every load.* and shutdown.* command (US-058). shutdown.default is
 * listed when at least one of the commands upsdrv_shutdown() tries is available.
 */
inline std::vector<const UPSCommandInfo*> build(const std::vector<HIDUsageDef>& usages, bool beeper_available,
                                                bool shutdown_cmds = true) {
    const bool test = hasFeature(usages, kTest);
    const bool panel = hasFeature(usages, kPanel);
    const bool shutdown = shutdown_cmds && hasFeature(usages, kShutdown);
    const bool startup = shutdown_cmds && hasFeature(usages, kStartup);
    const bool reboot = shutdown_cmds && rebootField(usages) != nullptr;

    std::vector<const UPSCommandInfo*> result;
    size_t count;
    const UPSCommandInfo* commands = table(count);
    for (size_t i = 0; i < count; i++) {
        const char* n = commands[i].name;
        bool available = false;
        if (strcmp(n, "beeper.mute") == 0) available = beeper_available && muteField(usages);
        else if (strncmp(n, "beeper.", 7) == 0) available = beeper_available;
        else if (strncmp(n, "test.battery.", 13) == 0) available = test;
        else if (strncmp(n, "test.panel.", 11) == 0) available = panel;
        else if (strcmp(n, "load.off") == 0 || strcmp(n, "load.off.delay") == 0 ||
                 strcmp(n, "shutdown.stop") == 0) available = shutdown;
        else if (strcmp(n, "load.on") == 0 || strcmp(n, "load.on.delay") == 0) available = startup;
        else if (strcmp(n, "shutdown.return") == 0 || strcmp(n, "shutdown.stayoff") == 0)
            available = shutdown && startup;
        else if (strcmp(n, "shutdown.reboot") == 0) available = reboot;
        // At least one of shutdown.return, shutdown.reboot, load.off.delay, shutdown.stayoff:
        // load.off.delay needs only Shutdown, which return and stayoff need as well
        else if (strcmp(n, "shutdown.default") == 0) available = reboot || shutdown;
        if (available) result.push_back(&commands[i]);
    }
    return result;
}

/**
 * Usage and value a test command writes: Test takes 1 = quick, 2 = deep, 3 = abort
 * (test_write_info in usbhid-ups.c), APCPanelTest 1 = start, 0 = stop (apc-hid.c),
 * beeper.mute 3 on the beeper field (mge-hid.c, apc-hid.c, cps-hid.c).
 * False for the other beeper commands (written by setBeeper()), the load and shutdown commands
 * (sequences of writes, resolved by resolveShutdownSteps()) and unknown names or usages.
 */
inline bool resolveWrite(const char* name, const std::vector<HIDUsageDef>& usages, HIDUsageDef& def, uint32_t& value) {
    if (!name) return false;
    if (strcmp(name, "beeper.mute") == 0) {
        const HIDUsageDef* b = muteField(usages);
        if (!b) return false;
        def = *b;
        value = 3;
        return true;
    }
    const UsageGroup* group = nullptr;
    if (strcmp(name, "test.battery.start.quick") == 0) { group = &kTest; value = 1; }
    else if (strcmp(name, "test.battery.start.deep") == 0) { group = &kTest; value = 2; }
    else if (strcmp(name, "test.battery.stop") == 0) { group = &kTest; value = 3; }
    else if (strcmp(name, "test.panel.start") == 0) { group = &kPanel; value = 1; }
    else if (strcmp(name, "test.panel.stop") == 0) { group = &kPanel; value = 0; }
    if (!group) return false;
    const HIDUsageDef* found = findFeature(usages, *group);
    if (!found) return false;
    def = *found;
    return true;
}

// US-058: one HID write of a load/shutdown sequence. The value is signed: -1 (cancel)
// reaches writeUsage() as (uint32_t)value and is masked to the field's bit_size.
struct WriteStep {
    HIDUsageDef def;
    int32_t value;
};

enum class StepsResult { OK, NOT_SUPPORTED, INVALID_ARGUMENT };

// Delay given as INSTCMD parameter: decimal integer, optional '-', >= -1. usbhid-ups
// uses atol(), which turns "abc" into 0 (immediate shutdown): rejected here on purpose.
inline bool parseDelayParam(const char* text, int32_t& out) {
    if (!text || !*text) return false;
    const char* p = text;
    if (*p == '-') p++;
    if (*p < '0' || *p > '9') return false;
    char* end = nullptr;
    long long v = strtoll(text, &end, 10);
    if (*end != '\0' || v < -1 || v > INT32_MAX) return false;
    out = (int32_t)v;
    return true;
}

// Delay not configured (ups.delay.* missing or not a number): the commands that need it
// are not run rather than guessing a value
static const int32_t kNoDelay = INT32_MIN;

/**
 * True when value can be written into field without being truncated by
 * BeeperLogic::writeField(): -1 (cancel, all ones as in usbhid-ups) always fits,
 * the other values must stay within the field's bit_size (signed when logical_min < 0).
 * A truncated delay could become 0, i.e. an immediate shutdown.
 */
inline bool fitsField(const HIDUsageDef& field, int32_t value) {
    if (value == -1) return true;
    if (value < 0) return false;
    if (field.bit_size == 0 || field.bit_size >= 32) return true;
    int64_t max = field.logical_min < 0 ? ((int64_t)1 << (field.bit_size - 1)) - 1
                                        : ((int64_t)1 << field.bit_size) - 1;
    return value <= max;
}

/**
 * HID writes of a load.* or shutdown.* command, with the semantics of instcmd() in
 * usbhid-ups.c and of the <brand>-hid.c tables. off_delay and on_delay are
 * ups.delay.shutdown and ups.delay.start. param is used only by load.off.delay,
 * load.on.delay and shutdown.reboot (nullptr or "" = default) and ignored by the others;
 * when not an integer >= -1, or outside the field's declared logical range,
 * INVALID_ARGUMENT with no steps. Missing usage: NOT_SUPPORTED. Two steps are written in
 * order, with the 125 ms pause of usbhid-ups between them. shutdown.default is not a
 * sequence (the host tries the commands one by one): NOT_SUPPORTED here.
 */
inline StepsResult resolveShutdownSteps(const char* name, const char* param, const std::vector<HIDUsageDef>& usages,
                                        int32_t off_delay, int32_t on_delay, std::vector<WriteStep>& steps) {
    steps.clear();
    if (!name) return StepsResult::NOT_SUPPORTED;
    const HIDUsageDef* sd = findFeature(usages, kShutdown);
    const HIDUsageDef* st = findFeature(usages, kStartup);

    // One write on a field, with the parameter when the command takes one
    auto single = [&](const HIDUsageDef* field, int32_t dfl, bool takes_param) -> StepsResult {
        if (!field) return StepsResult::NOT_SUPPORTED;
        int32_t value = dfl;
        bool from_param = false;
        if (takes_param && param && *param) {
            if (!parseDelayParam(param, value)) return StepsResult::INVALID_ARGUMENT;
            bool range_declared = field->logical_max > field->logical_min;
            if (range_declared && (value < field->logical_min || value > field->logical_max))
                return StepsResult::INVALID_ARGUMENT;
            from_param = true;
        }
        if (value == kNoDelay) return StepsResult::NOT_SUPPORTED;
        if (!fitsField(*field, value))
            return from_param ? StepsResult::INVALID_ARGUMENT : StepsResult::NOT_SUPPORTED;
        steps.push_back({ *field, value });
        return StepsResult::OK;
    };

    if (strcmp(name, "load.off") == 0) return single(sd, 0, false);
    if (strcmp(name, "load.on") == 0) return single(st, 0, false);
    if (strcmp(name, "load.off.delay") == 0) return single(sd, off_delay, true);
    if (strcmp(name, "load.on.delay") == 0) return single(st, on_delay, true);
    if (strcmp(name, "shutdown.stop") == 0) return single(sd, -1, false);
    if (strcmp(name, "shutdown.reboot") == 0) return single(rebootField(usages), 10, true);
    if (strcmp(name, "shutdown.return") == 0) {
        if (const HIDUsageDef* cs = apcCsReturnField(usages)) {
            steps.push_back({ *cs, 1 });
            return StepsResult::OK;
        }
        if (!sd || !st || on_delay == kNoDelay || off_delay == kNoDelay) return StepsResult::NOT_SUPPORTED;
        if (!fitsField(*st, on_delay) || !fitsField(*sd, off_delay)) return StepsResult::NOT_SUPPORTED;
        steps.push_back({ *st, on_delay });
        steps.push_back({ *sd, off_delay });
        return StepsResult::OK;
    }
    if (strcmp(name, "shutdown.stayoff") == 0) {
        if (!sd || !st || off_delay == kNoDelay) return StepsResult::NOT_SUPPORTED;
        if (!fitsField(*sd, off_delay)) return StepsResult::NOT_SUPPORTED;
        steps.push_back({ *st, -1 });
        steps.push_back({ *sd, off_delay });
        return StepsResult::OK;
    }
    return StepsResult::NOT_SUPPORTED;
}

/**
 * ups.delay.shutdown and ups.delay.start with NUT semantics: configured delays, never read
 * from the device (HU_FLAG_ABSENT in mge-hid.c, apc-hid.c, cps-hid.c), set by the host at
 * connection with the driver defaults (IUPSDriver::defaultOffDelay/defaultOnDelay). Only
 * when the device has the group and the driver supports the shutdown commands; what the
 * device reports in DelayBeforeShutdown/Startup is ups.timer.*.
 */
inline void applyDefaultDelays(UPSData& data, const std::vector<HIDUsageDef>& usages,
                               int32_t off_delay, int32_t on_delay, bool shutdown_cmds) {
    if (!shutdown_cmds) return;
    if (hasFeature(usages, kShutdown)) data.set("ups.delay.shutdown", String((int)off_delay));
    if (hasFeature(usages, kStartup)) data.set("ups.delay.start", String((int)on_delay));
}

} // namespace CommandCatalog

#endif // COMMAND_CATALOG_H
