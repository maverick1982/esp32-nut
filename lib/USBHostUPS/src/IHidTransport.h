#ifndef I_HID_TRANSPORT_H
#define I_HID_TRANSPORT_H

#include <Arduino.h>
#include <vector>
#include "HIDUsages.h"
#include "HIDParser.h"

/**
 * @brief The attached HID device as the drivers see it: report and string descriptor
 * requests, usages and parser of the report descriptor, writes of a usage and the lock
 * of the data shared with the host (ADR 0010 step 4, US-104).
 *
 * The drivers depend on this interface only, never on the USB host itself.
 */
class IHidTransport {
public:
    virtual ~IHidTransport() = default;

    // Lock of the UPS data shared with the host (_mutex, ADR 0008)
    virtual void lock() const = 0;
    virtual void unlock() const = 0;

    virtual const std::vector<HIDUsageDef>& getUsages() const = 0;
    virtual const HIDParser* getHIDParser() const = 0;
    virtual String getActiveBeeperPath() const = 0;
    // Writes value into the FEATURE field of def (read-modify-write of its report)
    virtual bool writeUsage(const HIDUsageDef& def, uint32_t value) { return false; }
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
};

#endif // I_HID_TRANSPORT_H
