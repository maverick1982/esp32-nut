#ifndef QX_CONTROL_H
#define QX_CONTROL_H

#include <vector>
#include "IUpsControl.h"
#include "UpsCommands.h"
#include "UpsVariables.h"
#include "UPSData.h"

class QxEngine;

/**
 * @brief Commands and SET VAR of the Q* drivers (ADR 0013, section 4; US-135).
 *
 * Like HidControl, a static object without state: the host calls it from the task of the
 * NUT server or the Web UI, without its data lock, while the poll task may delete the driver.
 * So it never touches the engine: it goes through the protocol-neutral port only
 * (driverCommands, runDriverCommand, runDriverSetVariable), never through its HID accessor.
 * It handles itself what does not need the UPS: not connected, command not offered, and
 * shutdown.default, whose attempts go through port.executeCommand() to be logged one by one.
 *
 * The static functions taking a QxEngine are what the host runs in the poll task, on the
 * engine of the attached driver (US-136): instcmd()/setvar() of nutdrv_qx.c and the list of
 * commands, with the results mapped onto the project types.
 */
class QxControl : public IUpsControl {
public:
    static const QxControl& instance();

    std::vector<const UPSCommandInfo*> supportedCommands(const IUpsControlPort& port) const override;
    CommandResult executeCommand(IUpsControlPort& port, const char* name, const char* param) const override;
    NutTable::SetStatus setVariable(IUpsControlPort& port, const char* name, const char* value) const override;

    /**
     * @brief The commands of the protocol, in the order of the UpsCommands.h catalog.
     *
     * The dstate_addcmd() of nutdrv_qx (QxEngine::commandOffered), plus two project rules:
     * shutdown.return and shutdown.stayoff also when only their fallbacks on the
     * load.*.delay rows exist (nutdrv_qx runs them without listing them, usbhid-ups lists
     * them; here a command not listed is not run), and shutdown.default when
     * upsdrv_shutdown() can work: both ups.delay.* rows and shutdown.return.
     */
    static std::vector<const UPSCommandInfo*> commandsOf(const QxEngine& qx);
    // QxEngine::instcmd(); param nullptr or "" = no value
    static CommandResult runCommand(QxEngine& qx, UPSData& data, const char* name, const char* param);
    // QxEngine::setvar()
    static NutTable::SetStatus runSetVariable(QxEngine& qx, UPSData& data, const char* name, const char* value);
};

#endif // QX_CONTROL_H
