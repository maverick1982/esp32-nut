#ifndef I_USB_HOST_UPS_H
#define I_USB_HOST_UPS_H

#include <Arduino.h>
#include <vector>
#include "IUpsSource.h"
#include "IHidTransport.h"
#include "UPSData.h"
#include "HIDUsages.h"
#include "HIDParser.h"
#include "CommandCatalog.h"
#include "NutTable.h"

// The USB HID host: the consumer-facing IUpsSource (US-103) and the driver-facing
// IHidTransport (US-104), plus the HID side of commands and beeper (US-105 moves it).
class IUSBHostUPS : public IUpsSource, public IHidTransport {
public:
    // Both bases declare the data lock: one override implements it for both
    void lock() const override = 0;
    void unlock() const override = 0;

    virtual bool setBeeper(bool enable) = 0;
    virtual bool supportsBeeperToggle() const { return true; }
    // False when the driver offers no load.* or shutdown.* command (US-058)
    virtual bool shutdownCommandsSupported() const { return true; }
    // False when the driver does not offer beeper.mute (US-087, Legrand)
    virtual bool beeperMuteSupported() const { return true; }
    // Commands of the driver's upstream subdriver, nullptr for the whole catalog (US-097)
    virtual const char* const* commandAllowList() const { return nullptr; }

    virtual const HIDUsageDef* getUsageDef(uint32_t usage) const = 0;
    // NUT instant commands the attached device supports, empty when no UPS is connected
    std::vector<const UPSCommandInfo*> getSupportedCommands() const override {
        if (!isConnected()) return {};
        // Data lock released before build(), as the NUT server does around setBeeper()
        bool beeper = supportsBeeperToggle() && getUPSData()->hasKey("ups.beeper.status");
        return CommandCatalog::build(getUsages(), beeper, shutdownCommandsSupported(), beeperMuteSupported(),
                                     commandAllowList());
    }
    /**
     * Runs a NUT instant command (US-057, US-058). param is the optional INSTCMD value,
     * used only by load.off.delay, load.on.delay and shutdown.reboot. No lock is held
     * while setBeeper() or writeUsage() talk to the device: lock order is _op_mutex,
     * then _mutex (ADR 0008).
     */
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        if (!name) return CommandResult::NOT_SUPPORTED;
        if (!isConnected()) return CommandResult::NOT_CONNECTED;
        bool supported = false;
        std::vector<const UPSCommandInfo*> available = getSupportedCommands();
        for (const auto* c : available) {
            if (strcmp(c->name, name) == 0) { supported = true; break; }
        }
        if (!supported) return CommandResult::NOT_SUPPORTED;

        if (strcmp(name, "beeper.enable") == 0 || strcmp(name, "beeper.disable") == 0) {
            return setBeeper(strcmp(name, "beeper.enable") == 0) ? CommandResult::OK : CommandResult::FAILED;
        }
        if (strcmp(name, "beeper.toggle") == 0) {
            // Read first: the data lock must not be held across setBeeper().
            // A muted beeper is not disabled: toggling it disables it.
            bool disabled = getUPSData()->get("ups.beeper.status") == "disabled";
            return setBeeper(disabled) ? CommandResult::OK : CommandResult::FAILED;
        }
        if (strcmp(name, "shutdown.default") == 0) {
            // upsdrv_shutdown() order; each attempt goes through the virtual
            // executeCommand(), so it is logged like a direct INSTCMD
            static const char* const kFallback[] = {
                "shutdown.return", "shutdown.reboot", "load.off.delay", "shutdown.stayoff" };
            _last_default_shutdown = "";
            for (const char* sub : kFallback) {
                bool sub_supported = false;
                for (const auto* c : available) {
                    if (strcmp(c->name, sub) == 0) { sub_supported = true; break; }
                }
                if (!sub_supported) continue;
                if (executeCommand(sub) == CommandResult::OK) {
                    _last_default_shutdown = sub;
                    return CommandResult::OK;
                }
            }
            return CommandResult::FAILED;
        }
        if (strncmp(name, "load.", 5) == 0 || strncmp(name, "shutdown.", 9) == 0) {
            std::vector<CommandCatalog::WriteStep> steps;
            CommandCatalog::StepsResult sr;
            {
                UPSDataLock lk = getUPSData();  // guards the parsed usages too
                // Configured delays (set at connection with the driver defaults). Missing or
                // not a number: the commands that need them are not run (a fixed fallback
                // could be too short for CyberPower, which rounds delays down to 60 s)
                int32_t off_delay = configuredDelay(lk.get(), "ups.delay.shutdown");
                int32_t on_delay = configuredDelay(lk.get(), "ups.delay.start");
                sr = CommandCatalog::resolveShutdownSteps(name, param, getUsages(), off_delay, on_delay, steps);
            }
            if (sr == CommandCatalog::StepsResult::INVALID_ARGUMENT) return CommandResult::INVALID_ARGUMENT;
            if (sr != CommandCatalog::StepsResult::OK) return CommandResult::NOT_SUPPORTED;
            for (size_t i = 0; i < steps.size(); i++) {
                if (i > 0) delay(125);  // usbhid-ups pause: some UPS reject commands sent too close
                if (!writeUsage(steps[i].def, (uint32_t)steps[i].value)) return CommandResult::FAILED;
            }
            return CommandResult::OK;
        }

        HIDUsageDef def;
        uint32_t value = 0;
        {
            UPSDataLock lk = getUPSData();  // guards the parsed usages too
            if (!CommandCatalog::resolveWrite(name, getUsages(), def, value)) {
                return CommandResult::NOT_SUPPORTED;
            }
        }
        return writeUsage(def, value) ? CommandResult::OK : CommandResult::FAILED;
    }
    // Command shutdown.default ran with its last success, empty if none (US-058)
    const String& lastDefaultShutdown() const { return _last_default_shutdown; }

protected:
    String _last_default_shutdown;

    // Delay configured in ups.delay.*, CommandCatalog::kNoDelay when missing or not an integer >= 0
    static int32_t configuredDelay(const UPSData& data, const char* key) {
        if (!data.hasKey(key)) return CommandCatalog::kNoDelay;
        int32_t v;
        String s = data.get(key);
        if (!CommandCatalog::parseDelayParam(s.c_str(), v) || v < 0) return CommandCatalog::kNoDelay;
        return v;
    }
};

#endif // I_USB_HOST_UPS_H
