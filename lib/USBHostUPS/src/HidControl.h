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
 */

#ifndef HID_CONTROL_H
#define HID_CONTROL_H

#include <Arduino.h>
#include <vector>
#include "IUpsControl.h"
#include "IHidControlPort.h"
#include "CommandCatalog.h"

/**
 * @brief Commands and SET VAR of the HID drivers (US-105): the NUT commands built on the
 * usages of the report descriptor (CommandCatalog), the beeper, the shutdown sequences and
 * the SET VAR plan of the driver. Every HID driver returns it from IUPSDriver::control().
 * The HID part of the port comes from hidPort(): without it nothing is supported.
 *
 * No lock is held while the port talks to the device (setBeeper, writeUsage): lock order
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
        bool beeper = port.supportsBeeperToggle() && port.upsData().hasKey("ups.beeper.status");
        return CommandCatalog::build(hid->getUsages(), beeper, port.shutdownCommandsSupported(), port.beeperMuteSupported(),
                                     port.commandAllowList());
    }

    /**
     * Runs a NUT instant command (US-057, US-058). param is the optional INSTCMD value,
     * used only by load.off.delay, load.on.delay and shutdown.reboot. No lock is held
     * while setBeeper() or writeUsage() talk to the device: lock order is _op_mutex,
     * then _mutex (ADR 0008).
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

        if (strcmp(name, "beeper.enable") == 0 || strcmp(name, "beeper.disable") == 0) {
            return port.setBeeper(strcmp(name, "beeper.enable") == 0) ? CommandResult::OK : CommandResult::FAILED;
        }
        if (strcmp(name, "beeper.toggle") == 0) {
            // Read first: the data lock must not be held across setBeeper().
            // A muted beeper is not disabled: toggling it disables it.
            bool disabled;
            {
                PortLock lk(port);
                disabled = port.upsData().get("ups.beeper.status") == "disabled";
            }
            return port.setBeeper(disabled) ? CommandResult::OK : CommandResult::FAILED;
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
            std::vector<CommandCatalog::WriteStep> steps;
            CommandCatalog::StepsResult sr;
            {
                PortLock lk(port);  // guards the parsed usages too
                // Configured delays (set at connection with the driver defaults). Missing or
                // not a number: the commands that need them are not run (a fixed fallback
                // could be too short for CyberPower, which rounds delays down to 60 s)
                int32_t off_delay = configuredDelay(port.upsData(), "ups.delay.shutdown");
                int32_t on_delay = configuredDelay(port.upsData(), "ups.delay.start");
                sr = CommandCatalog::resolveShutdownSteps(name, param, hid->getUsages(), off_delay, on_delay, steps);
            }
            if (sr == CommandCatalog::StepsResult::INVALID_ARGUMENT) return CommandResult::INVALID_ARGUMENT;
            if (sr != CommandCatalog::StepsResult::OK) return CommandResult::NOT_SUPPORTED;
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
            if (!CommandCatalog::resolveWrite(name, hid->getUsages(), def, value)) {
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

    // Delay configured in ups.delay.*, CommandCatalog::kNoDelay when missing or not an integer >= 0
    static int32_t configuredDelay(const UPSData& data, const char* key) {
        if (!data.hasKey(key)) return CommandCatalog::kNoDelay;
        int32_t v;
        String s = data.get(key);
        if (!CommandCatalog::parseDelayParam(s.c_str(), v) || v < 0) return CommandCatalog::kNoDelay;
        return v;
    }
};

#endif // HID_CONTROL_H
