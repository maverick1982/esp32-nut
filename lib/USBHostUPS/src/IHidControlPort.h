#ifndef I_HID_CONTROL_PORT_H
#define I_HID_CONTROL_PORT_H

#include <Arduino.h>
#include "IUpsControlPort.h"
#include "IHidTransport.h"
#include "NutTable.h"

/**
 * @brief The HID part of the control port (US-105): the HID transport and the steps of a
 * SET VAR planned on the usages (store, USB transfers). HidControl reaches it through
 * IUpsControlPort::hidPort().
 *
 * No lock may be held around writeUsage() and the SET VAR steps.
 */
class IHidControlPort : public IUpsControlPort, public IHidTransport {
public:
    // Declared by both bases: one override implements each for both
    void lock() const override = 0;
    void unlock() const override = 0;

    IHidControlPort* hidPort() override { return this; }
    const IHidControlPort* hidPort() const override { return this; }

    /**
     * Steps of a SET VAR (US-067) that touch the host, in USBHostUPS:
     * planSet: IUPSDriver::planSet() of the attached driver, under the data lock.
     * saveDriverSideValue: a driver-side value saved first (flash), then applied.
     * writePlannedValue: the HID write of the plan and the read back of its report, under
     * _op_mutex with no data lock held during the transfers (ADR 0008).
     */
    virtual NutTable::SetStatus planSet(const char* name, const char* value, NutTable::SetPlan& plan) const {
        return NutTable::SetStatus::NOT_CONNECTED;
    }
    virtual NutTable::SetStatus saveDriverSideValue(const NutTable::SetPlan& plan) { return NutTable::SetStatus::FAILED; }
    virtual NutTable::SetStatus writePlannedValue(const NutTable::SetPlan& plan) { return NutTable::SetStatus::FAILED; }
};

#endif // I_HID_CONTROL_PORT_H
