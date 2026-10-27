#ifndef I_UPS_CONTROL_PORT_H
#define I_UPS_CONTROL_PORT_H

#include <Arduino.h>
#include "UPSData.h"
#include "UpsCommands.h"
#include "UpsVariables.h"

class IHidControlPort;

/**
 * @brief The host as the control of a driver sees it (IUpsControl, US-105), whatever the
 * protocol: the UPS data, the beeper, the flags of the attached driver, the commands with
 * their System Log line and the driver-side values of SET VAR. The HID part (transport,
 * usages, SET VAR plan) is reached only through hidPort(), which is reserved to HidControl:
 * the control of another protocol uses this interface alone (checked by test_hid_transport).
 *
 * Every method takes the locks it needs. lock() must be recursive (the data lock, _mutex):
 * the control holds it around upsData() and calls the flags, which take it again. No lock
 * may be held around setBeeper() and the SET VAR steps.
 */
class IUpsControlPort {
public:
    virtual ~IUpsControlPort() = default;

    // Lock of the UPS data shared with the host (_mutex, ADR 0008)
    virtual void lock() const = 0;
    virtual void unlock() const = 0;

    virtual bool isConnected() const = 0;
    // UPS data of the host: read it only with lock() held
    virtual const UPSData& upsData() const = 0;

    virtual bool setBeeper(bool enable) = 0;
    virtual bool supportsBeeperToggle() const { return true; }
    // False when the driver offers no load.* or shutdown.* command (US-058)
    virtual bool shutdownCommandsSupported() const { return true; }
    // False when the driver does not offer beeper.mute (US-087, Legrand)
    virtual bool beeperMuteSupported() const { return true; }
    // Commands of the driver's upstream subdriver, nullptr for the whole catalog (US-097)
    virtual const char* const* commandAllowList() const { return nullptr; }

    // Runs a command as an INSTCMD does, with its System Log line (shutdown.default attempts)
    virtual CommandResult executeCommand(const char* name, const char* param = nullptr) = 0;
    // Command shutdown.default ran with its last success, empty when it starts (US-058)
    virtual void noteDefaultShutdown(const char* via) {}

    // A saved driver-side value erased, the default applied (US-068)
    virtual NutTable::SetStatus resetDriverSideValue(const char* name, String& value) {
        return NutTable::SetStatus::NOT_CONNECTED;
    }

    // The HID part of the port, nullptr for a host of another protocol
    virtual IHidControlPort* hidPort() { return nullptr; }
    virtual const IHidControlPort* hidPort() const { return nullptr; }
};

#endif // I_UPS_CONTROL_PORT_H
