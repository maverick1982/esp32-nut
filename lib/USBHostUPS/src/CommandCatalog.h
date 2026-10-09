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

namespace CommandCatalog {

inline const UPSCommandInfo* table(size_t& count) {
    static const UPSCommandInfo commands[] = {
        { "beeper.enable",            "Enable the UPS beeper",                             false },
        { "beeper.disable",           "Disable the UPS beeper",                            false },
        { "beeper.toggle",            "Toggle the UPS beeper",                             false },
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

// True when a FEATURE usage matches one of the standard paths or, for the APC
// vendor usages (0xff86xxxx, not named by NUTUsages.h), one of the usage codes.
inline bool hasFeature(const std::vector<HIDUsageDef>& usages,
                       const char* const* paths, size_t n_paths,
                       const uint32_t* codes, size_t n_codes) {
    for (const auto& u : usages) {
        if (u.report_type != 0x03) continue;
        for (size_t i = 0; i < n_paths; i++) {
            if (strcmp(u.path, paths[i]) == 0) return true;
        }
        for (size_t i = 0; i < n_codes; i++) {
            if (u.usage == codes[i]) return true;
        }
    }
    return false;
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
    static const char* const test_paths[] = {
        "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test" };
    static const uint32_t panel_codes[] = { 0xff860072 };  // APCPanelTest
    static const char* const shutdown_paths[] = {
        "UPS.PowerSummary.DelayBeforeShutdown", "UPS.Output.DelayBeforeShutdown" };
    static const uint32_t shutdown_codes[] = { 0xff86007d };  // APCDelayBeforeShutdown
    static const char* const startup_paths[] = {
        "UPS.PowerSummary.DelayBeforeStartup", "UPS.Output.DelayBeforeStartup" };
    static const uint32_t startup_codes[] = { 0xff86007e };  // APCDelayBeforeStartup
    static const char* const reboot_paths[] = {
        "UPS.PowerSummary.DelayBeforeReboot", "UPS.Output.DelayBeforeReboot" };
    static const uint32_t reboot_codes[] = { 0xff86007c };  // APCDelayBeforeReboot

    const bool test = hasFeature(usages, test_paths, 3, nullptr, 0);
    const bool panel = hasFeature(usages, nullptr, 0, panel_codes, 1);
    const bool shutdown = hasFeature(usages, shutdown_paths, 2, shutdown_codes, 1);
    const bool startup = hasFeature(usages, startup_paths, 2, startup_codes, 1);
    const bool reboot = hasFeature(usages, reboot_paths, 2, reboot_codes, 1);

    std::vector<const UPSCommandInfo*> result;
    size_t count;
    const UPSCommandInfo* commands = table(count);
    for (size_t i = 0; i < count; i++) {
        const char* n = commands[i].name;
        bool available = false;
        if (strncmp(n, "beeper.", 7) == 0) available = beeper_available;
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

} // namespace CommandCatalog

#endif // COMMAND_CATALOG_H
