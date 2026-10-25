#ifndef I_UPS_DRIVER_H
#define I_UPS_DRIVER_H

#include <Arduino.h>
#include <stddef.h>
#include <vector>
#include "NutTable.h"
#include "UPSData.h"

class IHidTransport;

class IUPSDriver {
public:
    virtual ~IUPSDriver() = default;
    virtual const char* getDriverName() const = 0;

    /**
     * @brief Called when the device is initialized or connected
     */
    virtual void setup() = 0;

    /**
     * @brief Called periodically to perform polling
     * 
     * @param host Transport of the attached device (to request reports)
     * @param data Reference to the UPS data structure to update
     * @param now Current time in milliseconds
     */
    virtual void loop(IHidTransport* host, UPSData& data, uint32_t now) = 0;

    /**
     * @brief True once the first full poll after setup() has run (issue #60).
     *
     * Until then the values are partial: the host reports them stale, like upsd while
     * usbhid-ups is still in upsdrv_initinfo().
     */
    virtual bool initialPollDone() const { return true; }

    /**
     * @brief Decodes a received HID report
     * 
     * @param report_id The ID of the report
     * @param data Pointer to the report payload
     * @param length Length of the payload
     * @param ups_data Reference to the UPS data structure to update
     */
    virtual void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) = 0;

    /**
     * @brief Called by the host right after every decodeReport(), with the same arguments.
     *
     * Decodes the rows of the driver's NUT table (NutTable.h) carried by this report and
     * writes the values of the rows that own their variable last, so a hand-written
     * mapping of the same key never overrides them (US-066).
     */
    virtual void postDecode(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {}

    /**
     * @brief Variables a NUT client may write (LIST RW), sorted as upsd lists them.
     */
    virtual void writableVariables(const UPSData& data, std::vector<NutTable::RWVariable>& out) const {}

    /**
     * @brief Decides what a SET VAR writes, as setvar() in usbhid-ups (US-067). No I/O and
     * no change to data: the host runs the plan.
     *
     * @param data Current values (ups.delay.*, the other half of the MGE clock)
     * @param usages Parsed usages of the attached device
     * @param name Variable, matched case-insensitively as find_nut_info()
     * @param value Text of the SET VAR
     * @param plan Filled only when the result is OK; plan.name is the canonical name
     */
    virtual NutTable::SetStatus planSet(const UPSData& data, const std::vector<HIDUsageDef>& usages,
                                        const char* name, const char* value, NutTable::SetPlan& plan) const {
        return NutTable::SetStatus::UNKNOWN;
    }

    /**
     * @brief Keeps the value of a driver-side variable of the NUT table (HU_FLAG_ABSENT rows
     * such as outlet.desc), like dstate_setinfo() in setvar(). setup() forgets it: the host
     * sets it again after every setup().
     */
    virtual void setDriverSideValue(const char* name, const char* value) {}

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
     * @brief wLength of a GET_REPORT, given the length declared in the report descriptor.
     *
     * Used by the poll and by the host's own requests (SET VAR read-modify-write and read
     * back), so a device that needs longer requests (APC Back-UPS BX) gets them everywhere.
     */
    virtual uint16_t requestLength(uint8_t report_type, uint8_t report_id, uint16_t expected) const { return expected; }

    /**
     * @brief Forgets that a report was decoded: the next poll requests it again even when
     * static reports are read once (US-067, after a failed read back of a written report).
     */
    virtual void invalidateReport(uint8_t report_type, uint8_t report_id) {}

    /**
     * @brief Parses a received String Descriptor
     * 
     * @param index The string index
     * @param data Pointer to the string descriptor payload
     * @param length Length of the payload
     * @param ups_data Reference to the UPS data structure to update
     */
    virtual void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) = 0;

    /**
     * @brief Rewrites the device strings once they are known (issue #76).
     *
     * Called after the strings are read from the device info at claim time and after
     * every string descriptor, like the format_model/format_mfr hooks of the NUT
     * subdrivers. Must be idempotent.
     */
    virtual void formatDeviceStrings(UPSData& data) {}

    /**
     * @brief Encodes boolean beeper state into device-specific HID value
     * 
     * @param enable Desired beeper state
     * @param bit_size Size of the HID usage field in bits
     * @return Encoded value to write into HID report
     */
    virtual uint8_t encodeBeeperValue(bool enable, uint16_t bit_size) const {
        if (bit_size == 1) return enable ? 1 : 0;
        return enable ? 2 : 1; // Default standard HID PDC (1 = disabled, 2 = enabled)
    }

    /**
     * @brief True when the beeper can be controlled (SET_REPORT) on this device.
     *
     * Some devices expose an AudibleAlarmControl field but ignore the beeper
     * commands (ecoflow-hid.c leaves them commented out for this reason). Returning
     * false hides the toggle in the Web UI and the NUT beeper commands, while
     * ups.beeper.status is still reported read-only.
     */
    virtual bool beeperControllable() const { return true; }

    /**
     * @brief True when beeper.mute is offered (US-087).
     *
     * beeper.mute writes 3 to the beeper field (beeper_info "muted" in usbhid-ups). On
     * Legrand 3 means disabled and legrand-hid.c has no beeper.mute: the driver returns false.
     */
    virtual bool beeperMuteSupported() const { return true; }

    /**
     * @brief NUT commands of the upstream subdriver, nullptr-terminated (US-097).
     *
     * nullptr offers the whole catalog, as before. A list limits LIST CMD and INSTCMD to the
     * commands it names, among those the device supports: liebert-hid.c has test.battery.start
     * and beeper.toggle only, where the catalog would also offer quick/deep/stop and
     * enable/disable/mute. test.battery.start is offered only through such a list.
     */
    virtual const char* const* commandAllowList() const { return nullptr; }

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
     * @brief Maximum length of ups.delay.* (info_len of the upstream rows: 10 in mge-hid,
     * apc-hid and cps-hid).
     */
    virtual uint8_t delayAux() const { return 10; }

    /**
     * @brief Paths of the subdriver's ups.delay.* rows (NutTable.h), nullptr for a driver
     * without an upstream subdriver: the paths of every subdriver then apply (US-058).
     */
    virtual const NutTable::DelayPaths* delayPaths() const { return nullptr; }
};

#endif // I_UPS_DRIVER_H
