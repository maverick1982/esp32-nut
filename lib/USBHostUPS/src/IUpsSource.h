#ifndef I_UPS_SOURCE_H
#define I_UPS_SOURCE_H

#include <Arduino.h>
#include <vector>
#include "UPSData.h"
#include "UpsCommands.h"
#include "UpsVariables.h"

/**
 * @brief Source of the UPS data as the NUT server and the web API see it, without HID
 * types (ADR 0010 step 4, US-103).
 *
 * IUSBHostUPS joins it with IHidTransport, the part the drivers use (US-104).
 */
class IUpsSource {
public:
    virtual ~IUpsSource() = default;
    virtual void end() {}
    virtual void lock() const = 0;
    virtual void unlock() const = 0;

    class UPSDataLock {
    private:
        const UPSData& _data;
        const IUpsSource* _host;
    public:
        UPSDataLock(const UPSData& data, const IUpsSource* host) : _data(data), _host(host) {
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
    virtual bool isConnected() const = 0;
    // True when the values can no longer be refreshed: control pipe not answering, or
    // no device attached (after the boot grace). Consumers must not serve them as current.
    virtual bool isDataStale() const { return false; }
    // True while a device is attached but its first full poll has not completed yet
    virtual bool isWaitingFirstData() const { return false; }

    // Commands, SET VAR and reset go through the control of the attached driver (US-105).
    // NUT instant commands the attached device supports, empty when no UPS is connected
    virtual std::vector<const UPSCommandInfo*> getSupportedCommands() const = 0;
    // Runs a NUT instant command (US-057, US-058). param is the optional INSTCMD value.
    virtual CommandResult executeCommand(const char* name, const char* param = nullptr) = 0;
    // Variables a NUT client may write (LIST RW), sorted as upsd; empty without a UPS (US-066)
    virtual std::vector<NutTable::RWVariable> getWritableVariables() const { return {}; }
    /**
     * Runs a NUT SET VAR (US-067) with the plan of the driver (IUPSDriver::planSet): a HID
     * write followed by a read back of the report, or a driver-side value saved first. Name
     * and value are already checked by the NUT server. No lock is held while the device
     * is written: lock order is _op_mutex, then _mutex (ADR 0008), as executeCommand().
     */
    virtual NutTable::SetStatus setVariable(const char* name, const char* value) { return NutTable::SetStatus::NOT_CONNECTED; }
    /**
     * Drops the saved value of a driver-side variable (US-068): ups.delay.* and outlet.*.desc
     * go back to the default of the attached driver, now and at the next connection. Only the
     * variables of getWritableVariables() with driver_side; UNKNOWN for the others. The store
     * is written before anything changes and with no lock held: lock order is _op_mutex, then
     * _mutex (ADR 0008), as setVariable().
     */
    virtual NutTable::SetStatus resetVariable(const char* name) { return NutTable::SetStatus::NOT_CONNECTED; }
};

#endif // I_UPS_SOURCE_H
