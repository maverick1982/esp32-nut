#ifndef I_HID_DRIVER_H
#define I_HID_DRIVER_H

#include <Arduino.h>
#include <stddef.h>
#include <vector>
#include "IUPSDriver.h"
#include "IHidTransport.h"
#include "HIDParser.h"
#include "NutTable.h"

/**
 * @brief The HID extension of a driver (US-131, ADR 0013 section 1): the hooks the host
 * calls only for the HID family. The host reaches them through IUPSDriver::hid(), never
 * directly (DriverDispatch.h), so a driver of another family cannot receive them by mistake.
 *
 * The HID transport is bound once after the driver is created (bindTransport()); poll() of
 * the core runs loop() on it.
 */
class IHidDriver : public IUPSDriver {
public:
    IHidDriver* hid() override { return this; }
    const IHidDriver* hid() const override { return this; }

    /**
     * @brief The transport of the attached device, used by poll(). The host outlives the
     * driver it owns, so the pointer stays valid as long as the driver exists.
     */
    void bindTransport(IHidTransport* host) { _transport = host; }

    // Core poll on the bound transport: nothing before bindTransport()
    void poll(UPSData& data, uint32_t now) override {
        if (_transport) loop(_transport, data, now);
    }

    /**
     * @brief Called periodically to perform polling
     *
     * @param host Transport of the attached device (to request reports)
     * @param data Reference to the UPS data structure to update
     * @param now Current time in milliseconds
     */
    virtual void loop(IHidTransport* host, UPSData& data, uint32_t now) = 0;

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
     * @brief wLength of a GET_REPORT, given the length declared in the report descriptor.
     *
     * Used by the poll and by the host's own requests (SET VAR read-modify-write and read
     * back), so a device that needs longer requests (APC Back-UPS BX) gets them everywhere.
     */
    virtual uint16_t requestLength(uint8_t report_type, uint8_t report_id, uint16_t expected) const { return expected; }

    /**
     * @brief False for the reports the on-demand read of the diagnostics export must not
     * request (US-119): the same reports the poll leaves out for safety, so a report that
     * freezes a firmware (CyberPower 6 and >= 130, Eaton 254/255) is never asked.
     */
    virtual bool acceptDiagReport(uint8_t report_type, uint8_t report_id) const { return true; }

    /**
     * @brief Forgets that a report was decoded: the next poll requests it again even when
     * static reports are read once (US-067, after a failed read back of a written report).
     */
    virtual void invalidateReport(uint8_t report_type, uint8_t report_id) {}

    /**
     * @brief True when every value the driver reads from the INPUT reports is also requested
     * by the poll with GET_REPORT (US-123, issue #111). The INPUT watchdog then never makes
     * the data stale nor asks for a recovery or a restart: a silence freezes nothing. The
     * default false keeps the watchdog active for a driver that does not compute it.
     */
    virtual bool inputValuesPolled() const { return false; }

    /**
     * @brief How long a silence of the interrupt pipe may last before the values count as
     * frozen (issue #138, InputSilenceGuard): stale data, interface restart, then system
     * restart. 0 (default) = a silence is not a fault, whatever inputValuesPolled() says.
     */
    virtual uint32_t inputSilenceFaultMs() const { return 0; }

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
     * @brief True when the report of the active beeper field has been decoded, even if its
     * value is not in the table (US-109): since setup(), and again after invalidateReport()
     * of that report once the poll reads it back.
     *
     * A value outside beeper_info (Powercom SPD-750U: 3 after beeper.toggle, #36) leaves
     * ups.beeper.status missing, as in NUT; beeper.enable and beeper.disable must stay offered.
     */
    virtual bool beeperFieldRead(const IHidTransport* host) const { return false; }

    /**
     * @brief NUT table whose HU_TYPE_CMD rows give the commands of the device (US-074).
     *
     * Every HID driver has one since US-079 (GenericDriver: genericHid2Nut()); nullptr offers
     * no command. The tables are static data: the pointer stays valid after the driver is deleted, so the
     * host can hand it to HidControl outside its lock.
     */
    virtual const NutTable::HidInfo* commandTable(size_t& n) const { n = 0; return nullptr; }

    /**
     * @brief Paths of the subdriver's ups.delay.* rows (NutTable.h), nullptr for a driver
     * without an upstream subdriver: the paths of every subdriver then apply (US-058).
     */
    virtual const NutTable::DelayPaths* delayPaths() const { return nullptr; }

protected:
    IHidTransport* _transport = nullptr;
};

#endif // I_HID_DRIVER_H
