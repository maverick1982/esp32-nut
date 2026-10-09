#ifndef COMMAND_CATALOG_H
#define COMMAND_CATALOG_H

#include <stdint.h>
#include <string.h>
#include <vector>
#include "HIDUsages.h"

// A NUT instant command the bridge knows about. Descriptions come verbatim from
// nut_repo/data/cmdvartab; destructive commands cut power to the load.
struct UPSCommandInfo {
    const char* name;
    const char* description;
    bool destructive;
};

enum class CommandResult { OK, NOT_SUPPORTED, NOT_CONNECTED, FAILED };

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
 * ups.beeper.status).
 */
inline std::vector<const UPSCommandInfo*> build(const std::vector<HIDUsageDef>& usages, bool beeper_available) {
    const bool test = hasFeature(usages, kTest);
    const bool panel = hasFeature(usages, kPanel);
    const bool shutdown = hasFeature(usages, kShutdown);
    const bool startup = hasFeature(usages, kStartup);
    const bool reboot = hasFeature(usages, kReboot);

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
        if (available) result.push_back(&commands[i]);
    }
    return result;
}

/**
 * Usage and value a test command writes: Test takes 1 = quick, 2 = deep, 3 = abort
 * (test_write_info in usbhid-ups.c), APCPanelTest 1 = start, 0 = stop (apc-hid.c),
 * beeper.mute 3 on the beeper field (mge-hid.c, apc-hid.c, cps-hid.c).
 * False for the other beeper commands (written by setBeeper()), the load and shutdown commands
 * (not executed yet) and unknown names or usages.
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

} // namespace CommandCatalog

#endif // COMMAND_CATALOG_H
