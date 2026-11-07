#ifndef I_UPS_DRIVER_H
#define I_UPS_DRIVER_H

#include <Arduino.h>
#include <stddef.h>
#include <vector>
#include "UpsVariables.h" // NutTable::RWVariable, without HID (US-103); the HID tables stay out
#include "UPSData.h"

class IUpsControl;
class IHidDriver;

/**
 * @brief What the host needs from every driver, whatever its protocol family (ADR 0013,
 * section 1): setup, poll, first poll done, device strings, control, delays and driver-side
 * values. The hooks of one family live in its extension, reached only through the family
 * accessor (hid() for the HID drivers), which returns nullptr for a driver of another
 * family: a driver never receives the calls of a family it does not belong to.
 *
 * A driver receives the transport of its own family outside this interface.
 */
class IUPSDriver {
public:
    virtual ~IUPSDriver() = default;
    virtual const char* getDriverName() const = 0;

    /**
     * @brief Called when the device is initialized or connected
     */
    virtual void setup() = 0;

    /**
     * @brief Called periodically to perform polling, on the transport the driver was given
     *
     * @param data Reference to the UPS data structure to update
     * @param now Current time in milliseconds
     */
    virtual void poll(UPSData& data, uint32_t now) = 0;

    /**
     * @brief True once the first full poll after setup() has run (issue #60).
     *
     * Until then the values are partial: the host reports them stale, like upsd while
     * usbhid-ups is still in upsdrv_initinfo().
     */
    virtual bool initialPollDone() const { return true; }

    /**
     * @brief Variables a NUT client may write (LIST RW), sorted as upsd lists them.
     */
    virtual void writableVariables(const UPSData& data, std::vector<NutTable::RWVariable>& out) const {}

    /**
     * @brief Keeps the value of a driver-side variable of the NUT table (HU_FLAG_ABSENT rows
     * such as outlet.desc), like dstate_setinfo() in setvar(). setup() forgets it: the host
     * sets it again after every setup().
     */
    virtual void setDriverSideValue(const char* name, const char* value) {}

    /**
     * @brief A value the host set itself right after a write to the device (US-074): the
     * beeper status after setBeeper(), before the device is read again. A driver whose NUT
     * table owns the variable must keep it, or the table would write the old value back.
     */
    virtual void noteHostValue(const char* name, const char* value) {}

    /**
     * @brief Forgets the value set with setDriverSideValue() (US-068): the HU_FLAG_ABSENT rows
     * named name go back to the dfl of the table, also at the next bind.
     *
     * @param value The default now in force
     * @return false when no HU_FLAG_ABSENT row of the table has that name; ups.delay.* is not
     * one, the host sets it again with applyDefaultDelays()
     */
    virtual bool resetDriverSideValue(const char* name, String& value) { return false; }

    /**
     * @brief Minimum gap between two control requests of the driver (US-119): the on-demand
     * read of the export keeps it from any other request, as the poll steps do.
     */
    virtual uint32_t requestSpacingMs() const { return 50; }

    /**
     * @brief A SET VAR or an INSTCMD wrote the UPS: the next poll reads the semi-static
     * values again (US-072), as data_has_changed in usbhid-ups.c:1157, 1239, 1534-1543.
     * Not called for the driver-side values, which write nothing to the UPS.
     */
    virtual void noteDataChanged() {}

    /**
     * @brief Rewrites the device strings once they are known (issue #76).
     *
     * Called after the strings are read from the device info at claim time and after
     * every string descriptor, like the format_model/format_mfr hooks of the NUT
     * subdrivers. Must be idempotent.
     */
    virtual void formatDeviceStrings(UPSData& data) {}

    /**
     * @brief True when the load.* and shutdown.* commands are offered (US-058).
     *
     * False for subdrivers whose NUT counterpart has no such commands or uses its own
     * encodings: they are then neither listed nor executed. ups.delay.* follows
     * delayVariablesSupported().
     */
    virtual bool shutdownCommandsSupported() const { return true; }

    /**
     * @brief Default ups.delay.shutdown / ups.delay.start in seconds, set by the host at
     * connection: DEFAULT_OFFDELAY / DEFAULT_ONDELAY of usbhid-ups.h (20 / 30).
     */
    virtual int32_t defaultOffDelay() const { return 20; }
    virtual int32_t defaultOnDelay() const { return 30; }

    /**
     * @brief True when the upstream subdriver declares ups.delay.shutdown/start (US-066).
     *
     * Usually the same drivers that offer the shutdown commands; powercom-hid declares the
     * variables although the commands are not offered here (US-058).
     */
    virtual bool delayVariablesSupported() const { return shutdownCommandsSupported(); }

    /**
     * @brief Commands and SET VAR of the driver's protocol (US-105).
     *
     * A static, stateless object: the host uses it after releasing the data lock, when the
     * poll task may already have deleted the driver. The HID drivers return HidControl, a
     * driver of another protocol (Q*) its own.
     */
    virtual const IUpsControl& control() const = 0;

    /**
     * @brief The HID extension of the driver (US-131), nullptr for a driver of another family.
     *
     * The host reaches the HID hooks only through it (DriverDispatch.h), as HidControl
     * reaches the HID part of IUpsControlPort only through its HID accessor (US-105).
     */
    virtual IHidDriver* hid() { return nullptr; }
    virtual const IHidDriver* hid() const { return nullptr; }
};

#endif // I_UPS_DRIVER_H
