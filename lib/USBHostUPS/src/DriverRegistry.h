#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>

class IUPSDriver;

/**
 * @brief Driver selection by USB VID/PID (review S3).
 *
 * One table instead of an if/else in USBHostUPS::claimInterface(), shared with the
 * fixture replay tests. The first matching row wins; GenericDriver otherwise.
 */
namespace DriverRegistry {
    static const uint16_t ANY_PID = 0xFFFF;

    // New driver for the device, owned by the caller (never null)
    IUPSDriver* create(uint16_t vid, uint16_t pid);
}

#endif // DRIVER_REGISTRY_H
