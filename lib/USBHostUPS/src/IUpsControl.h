#ifndef I_UPS_CONTROL_H
#define I_UPS_CONTROL_H

#include <Arduino.h>
#include <vector>
#include "IUpsControlPort.h"

/**
 * @brief Commands and writable variables of a driver (ADR 0010 step 4, US-105).
 *
 * IUPSDriver::control() returns it: the HID drivers HidControl, a driver of another
 * protocol (Q*) its own. Every method gets the protocol-neutral port; only HidControl takes
 * the HID part from it. Implementations are stateless with static lifetime: the host
 * calls them without its data lock, while the poll task may delete the driver.
 */
class IUpsControl {
public:
    virtual ~IUpsControl() = default;

    // NUT instant commands the attached device supports, empty when no UPS is connected
    virtual std::vector<const UPSCommandInfo*> supportedCommands(const IUpsControlPort& port) const = 0;
    // Runs a NUT instant command (US-057, US-058). param is the optional INSTCMD value
    virtual CommandResult executeCommand(IUpsControlPort& port, const char* name, const char* param) const = 0;
    // Runs a NUT SET VAR (US-067); name and value are already checked by the NUT server
    virtual NutTable::SetStatus setVariable(IUpsControlPort& port, const char* name, const char* value) const = 0;

    /**
     * Drops the saved value of a driver-side variable (US-068). value is the default now in
     * force, empty when only the saved value was erased.
     */
    virtual NutTable::SetStatus resetVariable(IUpsControlPort& port, const char* name, String& value) const {
        value = "";
        if (!port.isConnected()) return NutTable::SetStatus::NOT_CONNECTED;
        if (!name) return NutTable::SetStatus::UNKNOWN;
        return port.resetDriverSideValue(name, value);
    }
};

#endif // I_UPS_CONTROL_H
