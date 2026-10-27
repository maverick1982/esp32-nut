#ifndef I_USB_HOST_UPS_H
#define I_USB_HOST_UPS_H

#include <Arduino.h>
#include <vector>
#include "IUpsSource.h"
#include "IHidControlPort.h"
#include "HidControl.h"
#include "UPSData.h"
#include "HIDUsages.h"
#include "HIDParser.h"
#include "NutTable.h"

// The USB HID host: the consumer-facing IUpsSource (US-103) and the port of the driver's
// control (IUpsControlPort with its HID part IHidControlPort, on top of the driver-facing
// IHidTransport of US-104). Commands and writable variables go through the control of the
// attached driver (US-105).
class IUSBHostUPS : public IUpsSource, public IHidControlPort {
public:
    // Declared by both bases: one override implements each for both
    void lock() const override = 0;
    void unlock() const override = 0;
    bool isConnected() const override = 0;

    virtual const HIDUsageDef* getUsageDef(uint32_t usage) const = 0;

    // Control of the attached driver; the USB host reads it from the driver
    virtual const IUpsControl& activeControl() const { return HidControl::instance(); }
    // The host's data, still valid once the temporary lock is released: read it with lock() held
    const UPSData& upsData() const override { return getUPSData().get(); }

    std::vector<const UPSCommandInfo*> getSupportedCommands() const override {
        return activeControl().supportedCommands(*this);
    }
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        return activeControl().executeCommand(*this, name, param);
    }
    NutTable::SetStatus setVariable(const char* name, const char* value) override {
        return activeControl().setVariable(*this, name, value);
    }
    NutTable::SetStatus resetVariable(const char* name) override {
        String value;
        return activeControl().resetVariable(*this, name, value);
    }

    void noteDefaultShutdown(const char* via) override { _last_default_shutdown = via ? via : ""; }
    // Command shutdown.default ran with its last success, empty if none (US-058)
    const String& lastDefaultShutdown() const { return _last_default_shutdown; }

protected:
    String _last_default_shutdown;
};

#endif // I_USB_HOST_UPS_H
