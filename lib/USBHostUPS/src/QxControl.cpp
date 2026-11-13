/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *                 2016 Eaton
 *   Based on:
 *    usbhid-ups.c - Copyright (C)
 *      2003-2012 Arnaud Quette <arnaud.quette@gmail.com>
 *      2005      John Stamp <kinsayder@hotmail.com>
 *      2005-2006 Peter Selinger <selinger@users.sourceforge.net>
 *      2007-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *    blazer.c - Copyright (C)
 *      2008-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *      2012      Arnaud Quette <ArnaudQuette@Eaton.com>
 *    blazer_ser.c - Copyright (C)
 *      2008      Arjen de Korte <adkorte-guest@alioth.debian.org>
 *    blazer_usb.c - Copyright (C)
 *      2003-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *      2011-2012 Arnaud Quette <arnaud.quette@free.fr>
 *    Masterguard additions
 *      2020-2021 Edgar Fuß, Mathematisches Institut der Universität Bonn <ef@math.uni-bonn.de>
 *    Armac (Richcomm-variant) additions
 *      2021      Tomasz Fortuna <bla@thera.be>
 */

#include "QxControl.h"
#include <string.h>
#include "QxEngine.h"

// Commands and SET VAR of the Q* drivers (US-135). shutdown.default ports upsdrv_shutdown()
// of nut_repo/drivers/nutdrv_qx.c:3259-3369; the rest forwards to QxEngine::instcmd() and
// QxEngine::setvar() (QxEngineControl.cpp), run by the host in the poll task.

namespace {

bool listed(const std::vector<const UPSCommandInfo*>& commands, const char* name) {
    for (const auto* c : commands) {
        if (strcmp(c->name, name) == 0) return true;
    }
    return false;
}

} // namespace

const QxControl& QxControl::instance() {
    static const QxControl control;
    return control;
}

std::vector<const UPSCommandInfo*> QxControl::supportedCommands(const IUpsControlPort& port) const {
    if (!port.isConnected()) return {};
    return port.driverCommands();
}

CommandResult QxControl::executeCommand(IUpsControlPort& port, const char* name, const char* param) const {
    if (!name) return CommandResult::NOT_SUPPORTED;
    if (!port.isConnected()) return CommandResult::NOT_CONNECTED;
    std::vector<const UPSCommandInfo*> available = port.driverCommands();
    // A command the protocol does not offer never reaches the UPS
    if (!listed(available, name)) return CommandResult::NOT_SUPPORTED;

    if (strcmp(name, "shutdown.default") == 0) {
        // upsdrv_shutdown(): stop pending shutdowns, then shutdown.return, each up to MAXTRIES
        // times. Every attempt goes through the port's executeCommand(), so it is logged like
        // a direct INSTCMD (as HidControl does). Deviations: the delays in force are used
        // (ups.delay.*, also after a SET VAR) instead of resetting them to the table's dfl and
        // reading ondelay/offdelay of ups.conf, since upstream runs this in a new driver
        // process (-k); the ups.conf flag stayoff does not exist, so always shutdown.return.
        port.noteDefaultShutdown("");
        if (listed(available, "shutdown.stop")) {
            for (int retry = 1; retry <= QX_MAXTRIES; retry++) {
                if (port.executeCommand("shutdown.stop") == CommandResult::OK) break;
                // After MAXTRIES failures: "No shutdown pending", go on
            }
        }
        for (int retry = 1; retry <= QX_MAXTRIES; retry++) {
            if (port.executeCommand("shutdown.return") != CommandResult::OK) continue;
            port.noteDefaultShutdown("shutdown.return");
            return CommandResult::OK;
        }
        return CommandResult::FAILED; // "Shutdown failed!"
    }

    return port.runDriverCommand(name, param);
}

NutTable::SetStatus QxControl::setVariable(IUpsControlPort& port, const char* name, const char* value) const {
    if (!port.isConnected()) return NutTable::SetStatus::NOT_CONNECTED;
    if (!name) return NutTable::SetStatus::UNKNOWN;
    if (!value) return NutTable::SetStatus::CONVERSION_FAILED;
    return port.runDriverSetVariable(name, value);
}

std::vector<const UPSCommandInfo*> QxControl::commandsOf(const QxEngine& qx) {
    // The fallbacks of instcmd() for shutdown.return / shutdown.stayoff
    const bool fallback = qx.findNutInfo("load.on.delay", QX_FLAG_CMD, QX_FLAG_SKIP) >= 0 &&
                          qx.findNutInfo("load.off.delay", QX_FLAG_CMD, QX_FLAG_SKIP) >= 0;
    // upsdrv_shutdown() stops at "Unable to set start/shutdown delay" without these rows
    const bool delays = qx.findNutInfo("ups.delay.start", 0, QX_FLAG_SKIP) >= 0 &&
                        qx.findNutInfo("ups.delay.shutdown", 0, QX_FLAG_SKIP) >= 0;
    const bool shutdown_return = qx.commandOffered("shutdown.return") || fallback;

    std::vector<const UPSCommandInfo*> result;
    size_t count;
    const UPSCommandInfo* commands = UpsCommands::table(count);
    for (size_t i = 0; i < count; i++) {
        const char* n = commands[i].name;
        bool available;
        if (strcmp(n, "shutdown.default") == 0) {
            available = delays && shutdown_return;
        } else if (strcmp(n, "shutdown.return") == 0) {
            available = shutdown_return;
        } else if (strcmp(n, "shutdown.stayoff") == 0) {
            available = qx.commandOffered(n) || fallback;
        } else {
            available = qx.commandOffered(n);
        }
        if (available) result.push_back(&commands[i]);
    }
    return result;
}

CommandResult QxControl::runCommand(QxEngine& qx, UPSData& data, const char* name, const char* param) {
    if (param && !*param) param = nullptr;
    switch (qx.instcmd(data, name, param)) {
    case QxEngine::Instcmd::Handled:          return CommandResult::OK;
    case QxEngine::Instcmd::Invalid:          return CommandResult::NOT_SUPPORTED;
    case QxEngine::Instcmd::ConversionFailed: return CommandResult::INVALID_ARGUMENT;
    case QxEngine::Instcmd::Failed:           break;
    }
    return CommandResult::FAILED;
}

NutTable::SetStatus QxControl::runSetVariable(QxEngine& qx, UPSData& data, const char* name, const char* value) {
    switch (qx.setvar(data, name, value)) {
    case QxEngine::Setvar::Handled:          return NutTable::SetStatus::OK;
    case QxEngine::Setvar::ConversionFailed: return NutTable::SetStatus::CONVERSION_FAILED;
    case QxEngine::Setvar::Unknown:          break;
    }
    return NutTable::SetStatus::UNKNOWN;
}
