#ifndef I_USB_HOST_UPS_H
#define I_USB_HOST_UPS_H

#include <Arduino.h>
#include <vector>
#include "UPSData.h"
#include "HIDUsages.h"
#include "HIDParser.h"
#include "CommandCatalog.h"

class IUSBHostUPS {
public:
    virtual ~IUSBHostUPS() = default;
    virtual void end() {}
    virtual void lock() const = 0;
    virtual void unlock() const = 0;
    
    class UPSDataLock {
    private:
        const UPSData& _data;
        const IUSBHostUPS* _host;
    public:
        UPSDataLock(const UPSData& data, const IUSBHostUPS* host) : _data(data), _host(host) {
            if (_host) _host->lock();
        }
        ~UPSDataLock() {
            if (_host) _host->unlock();
        }
        UPSDataLock(const UPSDataLock&) = delete;
        UPSDataLock& operator=(const UPSDataLock&) = delete;
        UPSDataLock(UPSDataLock&& other) noexcept : _data(other._data), _host(other._host) {
            other._host = nullptr;
        }
        const UPSData* operator->() const { return &_data; }
        const UPSData& get() const { return _data; }
    };

    virtual UPSDataLock getUPSData() const = 0;
    virtual String getUPSStatusString() const = 0;
    virtual bool setBeeper(bool enable) = 0;
    virtual bool isConnected() const = 0;
    // True when the values can no longer be refreshed: control pipe not answering, or
    // no device attached (after the boot grace). Consumers must not serve them as current.
    virtual bool isDataStale() const { return false; }
    virtual bool supportsBeeperToggle() const { return true; }
    // False when the driver offers no load.* or shutdown.* command (US-058)
    virtual bool shutdownCommandsSupported() const { return true; }

    virtual const std::vector<HIDUsageDef>& getUsages() const = 0;
    virtual const HIDUsageDef* getUsageDef(uint32_t usage) const = 0;
    virtual const HIDParser* getHIDParser() const = 0;
    virtual String getActiveBeeperPath() const = 0;
    // NUT instant commands the attached device supports, empty when no UPS is connected
    virtual std::vector<const UPSCommandInfo*> getSupportedCommands() const {
        if (!isConnected()) return {};
        // Data lock released before build(), as the NUT server does around setBeeper()
        bool beeper = supportsBeeperToggle() && getUPSData()->hasKey("ups.beeper.status");
        return CommandCatalog::build(getUsages(), beeper, shutdownCommandsSupported());
    }
    // Writes value into the FEATURE field of def (read-modify-write of its report)
    virtual bool writeUsage(const HIDUsageDef& def, uint32_t value) { return false; }
    /**
     * Runs a NUT instant command (US-057, US-058). param is the optional INSTCMD value,
     * used only by load.off.delay, load.on.delay and shutdown.reboot. No lock is held
     * while setBeeper() or writeUsage() talk to the device: lock order is _op_mutex,
     * then _mutex (ADR 0008).
     */
    virtual CommandResult executeCommand(const char* name, const char* param = nullptr) {
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
    virtual uint32_t getQuirks() const = 0;
    // True while polling backs off after a link failure: drivers skip their poll steps
    virtual bool isPollingPaused() const = 0;
    virtual bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) = 0;
    // Fetches a string descriptor and hands it to the driver's parseStringDescriptor()
    virtual bool requestStringDescriptor(uint8_t string_index) = 0;
    // ms since the last complete INPUT report with this ID, UINT32_MAX if none yet (issue #60)
    virtual uint32_t inputReportAgeMs(uint8_t report_id, uint32_t now) const { return UINT32_MAX; }
    virtual uint16_t getVID() const { return 0; }
    virtual uint16_t getPID() const { return 0; }
    virtual void logDebug(const String& msg) const {}

    // String indices of the device descriptor, set when the interface is claimed
    uint8_t _iManufacturer = 0;
    uint8_t _iProduct = 0;
    uint8_t _iSerialNumber = 0;

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
