#ifndef I_UPS_DRIVER_H
#define I_UPS_DRIVER_H

#include <Arduino.h>
#include <stddef.h>

struct UPSData;
class IUSBHostUPS;

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
     * @param host Pointer to the main USB host controller (to request reports)
     * @param data Reference to the UPS data structure to update
     * @param now Current time in milliseconds
     */
    virtual void loop(IUSBHostUPS* host, UPSData& data, uint32_t now) = 0;

    /**
     * @brief Decodes a received HID report
     * 
     * @param report_id The ID of the report
     * @param data Pointer to the report payload
     * @param length Length of the payload
     * @param ups_data Reference to the UPS data structure to update
     */
    virtual void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) = 0;

    /**
     * @brief Parses a received String Descriptor
     * 
     * @param index The string index
     * @param data Pointer to the string descriptor payload
     * @param length Length of the payload
     * @param ups_data Reference to the UPS data structure to update
     */
    virtual void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) = 0;

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
};

#endif // I_UPS_DRIVER_H
