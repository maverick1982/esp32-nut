/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/usbhid-ups.c:
 *   Copyright (C) 2003-2022 Arnaud Quette <arnaud.quette@gmail.com>
 *                 2005 John Stamp <kinsayder@hotmail.com>
 *                 2005-2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2007-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2016 Eaton / Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2020-2026 Jim Klimov <jimklimov+nut@gmail.com>
 *
 * data/cmdvartab (variable and command descriptions): Network UPS Tools project.
 */

#ifndef HID_CONTROL_H
#define HID_CONTROL_H

#include <Arduino.h>
#include <vector>
#include <limits.h>
#include "IUpsControl.h"
#include "IHidControlPort.h"
#include "NutTable.h"
#include "UpsCommands.h"

/**
 * @brief Commands and SET VAR of the HID drivers (US-105): the NUT commands of the
 * HU_TYPE_CMD rows of the driver's table (IUPSDriver::commandTable, US-074; every HID driver
 * has one since US-079), the beeper, the shutdown sequences and the SET VAR plan of the driver.
 * Every HID driver returns it from IUPSDriver::control(). The HID part of the port comes from
 * hidPort(): without it, or without a command table, nothing is supported.
 *
 * No lock is held while the port talks to the device (setBeeperField, writeUsage): lock order
 * is _op_mutex, then _mutex (ADR 0008).
 */
class HidControl : public IUpsControl {
public:
    static const HidControl& instance() {
        static const HidControl control;
        return control;
    }

    std::vector<const UPSCommandInfo*> supportedCommands(const IUpsControlPort& port) const override {
        if (!port.isConnected()) return {};
        const IHidControlPort* hid = port.hidPort();
        if (!hid) return {};
        // The data lock guards the parsed usages too
        PortLock lk(port);
        // US-109: a beeper value outside the table (SPD-750U: 3, #36) leaves ups.beeper.status
        // missing, as in NUT; the field was read, so beeper.enable / beeper.disable stay offered
        bool known = port.upsData().hasKey("ups.beeper.status");
        bool beeper = port.supportsBeeperToggle() && (known || hid->beeperFieldRead());
        size_t n_table = 0;
        const NutTable::HidInfo* table = hid->commandTable(n_table); // US-074: the driver's command rows
        return buildCommands(hid->getUsages(), beeper, port.shutdownCommandsSupported(), port.beeperMuteSupported(),
                             table, n_table, known);
    }

    /**
     * Runs a NUT instant command (US-057, US-058). param is the optional INSTCMD value,
     * used only by load.off.delay, load.on.delay, shutdown.reboot and a shutdown.return row.
     * No lock is held while setBeeperField() or writeUsage() talk to the device: lock order is
     * _op_mutex, then _mutex (ADR 0008).
     */
    CommandResult executeCommand(IUpsControlPort& port, const char* name, const char* param) const override {
        if (!name) return CommandResult::NOT_SUPPORTED;
        if (!port.isConnected()) return CommandResult::NOT_CONNECTED;
        IHidControlPort* hid = port.hidPort();
        if (!hid) return CommandResult::NOT_SUPPORTED;
        bool supported = false;
        std::vector<const UPSCommandInfo*> available = supportedCommands(port);
        for (const auto* c : available) {
            if (strcmp(c->name, name) == 0) { supported = true; break; }
        }
        if (!supported) return CommandResult::NOT_SUPPORTED;
        size_t n_table = 0;
        const NutTable::HidInfo* table = hid->commandTable(n_table); // static data, outlives the lock

        if (strcmp(name, "beeper.enable") == 0 || strcmp(name, "beeper.disable") == 0 ||
            strcmp(name, "beeper.toggle") == 0) {
            bool enable = strcmp(name, "beeper.enable") == 0;
            HIDUsageDef field;
            {
                // Read first: the data lock must not be held across the beeper write.
                // A muted beeper is not disabled: toggling it disables it.
                PortLock lk(port);
                if (strcmp(name, "beeper.toggle") == 0) enable = port.upsData().get("ups.beeper.status") == "disabled";
                // US-074: the field of the beeper.enable / beeper.disable row (find_nut_info)
                const HIDUsageDef* f = cmdField(table, n_table, hid->getUsages(), enable ? "beeper.enable" : "beeper.disable");
                // A beeper.toggle row of its own (liebert-hid.c:141, US-106): its field, with the
                // value the project computes from ups.beeper.status (US-097 deviation 1)
                if (!f && strcmp(name, "beeper.toggle") == 0) {
                    f = cmdField(table, n_table, hid->getUsages(), "beeper.toggle");
                }
                if (!f) return CommandResult::NOT_SUPPORTED;
                field = *f;
            }
            return hid->setBeeperField(field, enable) ? CommandResult::OK : CommandResult::FAILED;
        }
        if (strcmp(name, "shutdown.default") == 0) {
            // upsdrv_shutdown() order; each attempt goes through the port's executeCommand(),
            // so it is logged like a direct INSTCMD
            static const char* const kFallback[] = {
                "shutdown.return", "shutdown.reboot", "load.off.delay", "shutdown.stayoff" };
            port.noteDefaultShutdown("");
            for (const char* sub : kFallback) {
                bool sub_supported = false;
                for (const auto* c : available) {
                    if (strcmp(c->name, sub) == 0) { sub_supported = true; break; }
                }
                if (!sub_supported) continue;
                if (port.executeCommand(sub) == CommandResult::OK) {
                    port.noteDefaultShutdown(sub);
                    return CommandResult::OK;
                }
            }
            return CommandResult::FAILED;
        }
        if (strncmp(name, "load.", 5) == 0 || strncmp(name, "shutdown.", 9) == 0) {
            bool ret = strcmp(name, "shutdown.return") == 0;
            bool start_auto = false;
            std::vector<WriteStep> steps;
            StepsResult sr;
            {
                PortLock lk(port);  // guards the parsed usages too
                // Configured delays (set at connection with the driver defaults). Missing or
                // not a number: the commands that need them are not run (a fixed fallback
                // could be too short for CyberPower, which rounds delays down to 60 s)
                start_auto = (ret || strcmp(name, "shutdown.stayoff") == 0) && port.upsData().hasKey("ups.start.auto");
                int32_t off_delay = configuredDelay(port.upsData(), "ups.delay.shutdown");
                int32_t on_delay = configuredDelay(port.upsData(), "ups.delay.start");
                sr = resolveShutdownSteps(name, param, hid->getUsages(), off_delay, on_delay, steps, table, n_table);
            }
            if (sr == StepsResult::INVALID_ARGUMENT) return CommandResult::INVALID_ARGUMENT;
            if (sr != StepsResult::OK) return CommandResult::NOT_SUPPORTED;
            // instcmd() of usbhid-ups: ups.start.auto set to "yes" (return) or "no" (stayoff)
            // first, when supported, whatever the result (decided 2026-10-29, US-074). Only once
            // the sequence can run, so a refused command leaves the setting alone, and only for
            // the composite command: a shutdown.return row of the table (one step, apc-hid.c:461)
            // is run as it is, as upstream does when find_nut_info() finds it (US-076)
            if (start_auto && steps.size() == 2) setVariable(port, "ups.start.auto", ret ? "yes" : "no");
            for (size_t i = 0; i < steps.size(); i++) {
                if (i > 0) delay(125);  // usbhid-ups pause: some UPS reject commands sent too close
                if (!hid->writeUsage(steps[i].def, (uint32_t)steps[i].value)) return CommandResult::FAILED;
            }
            return CommandResult::OK;
        }

        HIDUsageDef def;
        uint32_t value = 0;
        {
            PortLock lk(port);  // guards the parsed usages too
            if (!resolveWrite(name, hid->getUsages(), def, value, table, n_table)) {
                return CommandResult::NOT_SUPPORTED;
            }
        }
        return hid->writeUsage(def, value) ? CommandResult::OK : CommandResult::FAILED;
    }

    /**
     * Runs a NUT SET VAR (US-067) with the plan of the driver (IUPSDriver::planSet): a HID
     * write followed by a read back of the report, or a driver-side value saved first.
     */
    NutTable::SetStatus setVariable(IUpsControlPort& port, const char* name, const char* value) const override {
        if (!port.isConnected()) return NutTable::SetStatus::NOT_CONNECTED;
        if (!name) return NutTable::SetStatus::UNKNOWN;
        if (!value) return NutTable::SetStatus::CONVERSION_FAILED;
        IHidControlPort* hid = port.hidPort();
        if (!hid) return NutTable::SetStatus::UNKNOWN;

        // The driver decides what to write, without I/O (setvar() of usbhid-ups)
        NutTable::SetPlan plan;
        NutTable::SetStatus st = hid->planSet(name, value, plan);
        if (st != NutTable::SetStatus::OK) return st;
        if (plan.kind == NutTable::SetPlan::Kind::DRIVER_SIDE) return hid->saveDriverSideValue(plan);
        return hid->writePlannedValue(plan);
    }

    // --- Commands of the HU_TYPE_CMD rows (US-074, US-079) ---

    /**
     * Field of an instant command row (US-074): the first HU_TYPE_CMD row with this name whose
     * path has a FEATURE usage, as find_nut_info() in instcmd() (the first row with hiddata;
     * HIDGetItemData() finds FEATURE items only, libhid.c:502). Deviation: a row whose path
     * exists only as INPUT is skipped, since it could not be written. beeper.enable,
     * beeper.disable and beeper.toggle take an OUTPUT field too: setBeeperField() writes it
     * with its own report type, as the beeper commands always did (OUTPUT items are typed
     * OUTPUT since US-109). dfl receives the row's default value (the value written without a
     * parameter, atol() upstream).
     */
    static const HIDUsageDef* cmdField(const NutTable::HidInfo* table, size_t n, const std::vector<HIDUsageDef>& usages,
                                       const char* name, const char** dfl = nullptr) {
        if (!table || !name) return nullptr;
        const bool output_ok = strcmp(name, "beeper.enable") == 0 || strcmp(name, "beeper.disable") == 0 ||
                               strcmp(name, "beeper.toggle") == 0;
        for (size_t i = 0; i < n; i++) {
            const NutTable::HidInfo& row = table[i];
            if (!NutTable::isCommandRow(row) || strcmp(row.info_type, name) != 0) continue;
            for (const auto& u : usages) {
                if ((u.report_type == 0x03 || (output_ok && u.report_type == 0x02)) && NutTable::rowMatches(u, row)) {
                    if (dfl) *dfl = row.dfl;
                    return &u;
                }
            }
        }
        return nullptr;
    }

    // Value written by a command row without a parameter (atol() of its dfl in instcmd())
    static int32_t cmdDefault(const char* dfl) {
        return dfl ? (int32_t)atol(dfl) : 0;
    }

    /**
     * True when value can be written into field without being truncated by
     * BeeperLogic::writeField(): -1 (cancel, all ones as in usbhid-ups) always fits,
     * the other values must stay within the field's bit_size (signed when logical_min < 0).
     * A truncated delay could become 0, i.e. an immediate shutdown.
     */
    static bool fitsField(const HIDUsageDef& field, int32_t value) {
        if (value == -1) return true;
        if (value < 0) return false;
        if (field.bit_size == 0 || field.bit_size >= 32) return true;
        int64_t max = field.logical_min < 0 ? ((int64_t)1 << (field.bit_size - 1)) - 1
                                            : ((int64_t)1 << field.bit_size) - 1;
        return value <= max;
    }

    // The value fits the field and, when the field declares a logical range, lies within it
    // (deviation: upstream writes whatever the row says). beeper.mute needs 3 to fit.
    static bool cmdValueFits(const HIDUsageDef& field, int32_t value) {
        if (!fitsField(field, value)) return false;
        bool range_declared = field.logical_max > field.logical_min;
        return !range_declared || value == -1 || (value >= field.logical_min && value <= field.logical_max);
    }

    /**
     * Commands the device supports (US-074): the HU_TYPE_CMD rows found in the descriptor,
     * plus the commands upsdrv_initinfo() derives (usbhid-ups.c:1642-1656): load.off from
     * load.off.delay, load.on from load.on.delay, shutdown.return and shutdown.stayoff from both.
     * Only names of the UpsCommands.h catalog, in its order: beeper.on/off (aliases upstream),
     * outlet.N.load.*, bypass.* and experimental.* stay out. Project rules kept: beeper commands
     * only with beeper_available (supportsBeeperToggle(), and ups.beeper.status reported or the
     * beeper field read, US-109); beeper.toggle also needs beeper_state_known (ups.beeper.status
     * reported: it writes from the current state); beeper.toggle from
     * beeper.enable and beeper.disable or from a row of its own, beeper.mute only when 3 fits the
     * field and mute_allowed (IUPSDriver::beeperMuteSupported, US-087),
     * shutdown.default when one of the commands upsdrv_shutdown() tries is available, and
     * shutdown_cmds (IUPSDriver::shutdownCommandsSupported, US-058): false hides every load.*
     * and shutdown.* command. Without a table, no command.
     */
    static std::vector<const UPSCommandInfo*> buildCommands(const std::vector<HIDUsageDef>& usages, bool beeper_available,
                                                            bool shutdown_cmds, bool mute_allowed,
                                                            const NutTable::HidInfo* cmd_table, size_t n_cmd_table,
                                                            bool beeper_state_known = true) {
        std::vector<const UPSCommandInfo*> result;
        if (!cmd_table) return result;
        auto has = [&](const char* name) { return cmdField(cmd_table, n_cmd_table, usages, name) != nullptr; };
        const bool shutdown = shutdown_cmds && has("load.off.delay");
        const bool startup = shutdown_cmds && has("load.on.delay");
        const bool stop = shutdown_cmds && has("shutdown.stop");
        const bool reboot = shutdown_cmds && has("shutdown.reboot");
        // A shutdown.return row of its own (apc-hid.c:461, US-076), besides the composite command
        const bool return_row = shutdown_cmds && has("shutdown.return");

        size_t count;
        const UPSCommandInfo* commands = UpsCommands::table(count);
        for (size_t i = 0; i < count; i++) {
            const char* n = commands[i].name;
            bool available = false;
            if (strcmp(n, "beeper.mute") == 0) {
                const char* dfl = nullptr;
                const HIDUsageDef* f = cmdField(cmd_table, n_cmd_table, usages, n, &dfl);
                available = mute_allowed && beeper_available && f && cmdValueFits(*f, cmdDefault(dfl));
            } else if (strcmp(n, "beeper.toggle") == 0) {
                // A beeper.toggle row of its own (liebert-hid.c:141, US-106), or the enable/disable pair
                available = beeper_available && beeper_state_known &&
                            (has("beeper.toggle") || (has("beeper.enable") && has("beeper.disable")));
            } else if (strncmp(n, "beeper.", 7) == 0) {
                available = beeper_available && has(n);
            } else if (strncmp(n, "test.", 5) == 0) {
                available = has(n);
            } else if (strcmp(n, "load.off") == 0 || strcmp(n, "load.off.delay") == 0) {
                available = shutdown;
            } else if (strcmp(n, "load.on") == 0 || strcmp(n, "load.on.delay") == 0) {
                available = startup;
            } else if (strcmp(n, "shutdown.stop") == 0) {
                available = stop;
            } else if (strcmp(n, "shutdown.return") == 0) {
                available = (shutdown && startup) || return_row;
            } else if (strcmp(n, "shutdown.stayoff") == 0) {
                available = shutdown && startup;
            } else if (strcmp(n, "shutdown.reboot") == 0) {
                available = reboot;
            } else if (strcmp(n, "shutdown.default") == 0) {
                available = reboot || shutdown || return_row;
            }
            if (available) result.push_back(&commands[i]);
        }
        return result;
    }

    /**
     * Usage and value a test or beeper.mute command writes: the command row and the value of
     * its dfl (instcmd(), atol()): Test 1 = quick, 2 = deep, 3 = abort (test_write_info in
     * usbhid-ups.c), APCPanelTest 1 = start, 0 = stop (apc-hid.c), beeper.mute 3. False for the
     * other beeper commands (written by setBeeperField()), the load and shutdown commands
     * (sequences of writes, resolveShutdownSteps()) and unknown names or usages.
     */
    static bool resolveWrite(const char* name, const std::vector<HIDUsageDef>& usages, HIDUsageDef& def, uint32_t& value,
                             const NutTable::HidInfo* cmd_table, size_t n_cmd_table) {
        if (!name || !cmd_table) return false;
        if (strcmp(name, "beeper.mute") != 0 && strncmp(name, "test.", 5) != 0) return false;
        const char* dfl = nullptr;
        const HIDUsageDef* f = cmdField(cmd_table, n_cmd_table, usages, name, &dfl);
        if (!f || !cmdValueFits(*f, cmdDefault(dfl))) return false;
        def = *f;
        value = (uint32_t)cmdDefault(dfl);
        return true;
    }

    // US-058: one HID write of a load/shutdown sequence. The value is signed: -1 (cancel)
    // reaches writeUsage() as (uint32_t)value and is masked to the field's bit_size.
    struct WriteStep {
        HIDUsageDef def;
        int32_t value;
    };

    enum class StepsResult { OK, NOT_SUPPORTED, INVALID_ARGUMENT };

    // Delay not configured (ups.delay.* missing or not a number): the commands that need it
    // are not run rather than guessing a value
    static const int32_t kNoDelay = INT32_MIN;

    /**
     * HID writes of a load.* or shutdown.* command, with the semantics of instcmd() in
     * usbhid-ups.c: the fields of the table's load.off.delay / load.on.delay rows, with the
     * configured delays (US-058) instead of the rows' dfl. off_delay and on_delay are
     * ups.delay.shutdown and ups.delay.start. param is used only by load.off.delay,
     * load.on.delay, shutdown.reboot and a shutdown.return row (nullptr or "" = default) and
     * ignored by the others; when not an integer >= -1, or outside the field's declared logical
     * range, INVALID_ARGUMENT with no steps. Missing row: NOT_SUPPORTED. Two steps are written in
     * order, with the 125 ms pause of usbhid-ups between them. shutdown.default is not a
     * sequence (the host tries the commands one by one): NOT_SUPPORTED here.
     */
    static StepsResult resolveShutdownSteps(const char* name, const char* param, const std::vector<HIDUsageDef>& usages,
                                            int32_t off_delay, int32_t on_delay, std::vector<WriteStep>& steps,
                                            const NutTable::HidInfo* cmd_table, size_t n_cmd_table) {
        steps.clear();
        if (!name) return StepsResult::NOT_SUPPORTED;
        const HIDUsageDef* sd = cmdField(cmd_table, n_cmd_table, usages, "load.off.delay");
        const HIDUsageDef* st = cmdField(cmd_table, n_cmd_table, usages, "load.on.delay");

        // One write on a field, with the parameter when the command takes one
        auto single = [&](const HIDUsageDef* field, int32_t dfl, bool takes_param) -> StepsResult {
            if (!field) return StepsResult::NOT_SUPPORTED;
            int32_t value = dfl;
            bool from_param = false;
            if (takes_param && param && *param) {
                if (!NutTable::parseDelayParam(param, value)) return StepsResult::INVALID_ARGUMENT;
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
        if (strcmp(name, "shutdown.stop") == 0 || strcmp(name, "shutdown.reboot") == 0) {
            // The row's field and dfl: -1 for shutdown.stop, 10 for shutdown.reboot (mge-hid.c:2331-2332)
            const char* dfl = nullptr;
            const HIDUsageDef* f = cmdField(cmd_table, n_cmd_table, usages, name, &dfl);
            return single(f, cmdDefault(dfl), strcmp(name, "shutdown.reboot") == 0);
        }
        if (strcmp(name, "shutdown.return") == 0) {
            // A shutdown.return row of the table is found by find_nut_info() before the composite
            // command: one write on its field of the parameter, or of the row's dfl (instcmd(),
            // usbhid-ups.c:1112; the Back-UPS CS row of apc-hid.c:461, US-076)
            const char* dfl = nullptr;
            const HIDUsageDef* row = cmdField(cmd_table, n_cmd_table, usages, "shutdown.return", &dfl);
            if (row) return single(row, cmdDefault(dfl), true);
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

private:
    // Data lock of the port for one scope
    class PortLock {
    public:
        explicit PortLock(const IUpsControlPort& port) : _port(port) { _port.lock(); }
        ~PortLock() { _port.unlock(); }
        PortLock(const PortLock&) = delete;
        PortLock& operator=(const PortLock&) = delete;
    private:
        const IUpsControlPort& _port;
    };

    // Delay configured in ups.delay.*, kNoDelay when missing or not an integer >= 0
    static int32_t configuredDelay(const UPSData& data, const char* key) {
        if (!data.hasKey(key)) return kNoDelay;
        int32_t v;
        String s = data.get(key);
        if (!NutTable::parseDelayParam(s.c_str(), v) || v < 0) return kNoDelay;
        return v;
    }
};

#endif // HID_CONTROL_H
