/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * data/cmdvartab (command descriptions): Network UPS Tools project.
 */

#ifndef UPS_COMMANDS_H
#define UPS_COMMANDS_H

// NUT instant commands known to the bridge, without HID: what the NUT server and the
// web API need to name, describe and run a command (US-103). The HID side that maps
// commands to usages stays in CommandCatalog.h.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
        { "test.battery.start",       "Start a battery test",                              false },
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

} // namespace CommandCatalog

#endif // UPS_COMMANDS_H
