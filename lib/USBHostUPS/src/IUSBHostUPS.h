#ifndef I_USB_HOST_UPS_H
#define I_USB_HOST_UPS_H

#include <Arduino.h>
#include <vector>
#include "UPSData.h"
#include "HIDUsages.h"

class IUSBHostUPS {
public:
    virtual ~IUSBHostUPS() = default;
    virtual void end() {}
    virtual const UPSData& getUPSData() const = 0;
    virtual String getUPSStatusString() const = 0;
    virtual bool setBeeper(bool enable) = 0;
    virtual bool isConnected() const = 0;

    virtual const std::vector<HIDUsageDef>& getUsages() const = 0;
    virtual const HIDUsageDef* getUsageDef(uint32_t usage) const = 0;
    virtual String getActiveBeeperPath() const = 0;
    virtual uint32_t getQuirks() const = 0;
    virtual bool isControlPending() const = 0;
    virtual bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) = 0;
    virtual bool requestStringDescriptor(uint8_t string_index) = 0;

    uint8_t _iManufacturer = 0;
    uint8_t _iProduct = 0;
    uint8_t _iSerialNumber = 0;
};

#endif // I_USB_HOST_UPS_H
