#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>

class IUPSDriver;

/**
 * @brief Driver selection by USB VID/PID (review S3).
 *
 * One table instead of an if/else in USBHostUPS::claimInterface(), shared with the
 * fixture replay tests. The first matching row wins; GenericDriver otherwise. A VID/PID
 * that several upstream subdrivers share is told apart by the manufacturer and product
 * strings, in the order of usbhid-ups subdriver_list (US-097).
 */
namespace DriverRegistry {
    static const uint16_t ANY_PID = 0xFFFF;

    // New driver for the device, owned by the caller (never null). mfr and product are the
    // USB strings of the device, nullptr or "" when it has none.
    IUPSDriver* create(uint16_t vid, uint16_t pid, const char* mfr = "", const char* product = "");
}

#endif // DRIVER_REGISTRY_H
