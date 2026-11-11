#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>
#include <vector>
#include "HIDUsages.h"

class IUPSDriver;

/**
 * @brief Driver selection by USB VID/PID (review S3).
 *
 * One table instead of an if/else in USBHostUPS::claimInterface(), shared with the
 * fixture replay tests. The first matching row wins; GenericDriver otherwise. A VID/PID
 * that several upstream subdrivers share is told apart by the manufacturer and product
 * strings, in the order of usbhid-ups subdriver_list (US-097).
 *
 * match() picks the protocol family before the report descriptor is read (ADR 0013,
 * section 2): (1) the explicit HID rows of create(); (2) the Q* rows, a copy of
 * qx_usb_id[] of nutdrv_qx; (3) the generic HID fallback, which the caller keeps only for
 * an interface with UPS or Battery pages (hasUpsPages()).
 */
namespace DriverRegistry {
    static const uint16_t ANY_PID = 0xFFFF;

    enum class Family : uint8_t { HID, QX };

    struct Match {
        Family family;
        bool explicit_hid;        // an explicit HID row (false: candidate for the generic fallback)
        const char* qx_transport; // name of the NUT USB subdriver ("hunnox"), nullptr for HID
    };

    // Family of the device from its VID, PID and USB strings (nullptr or "" when it has
    // none), in the order above. Pure: no allocation, no I/O.
    Match match(uint16_t vid, uint16_t pid, const char* mfr = "", const char* product = "");

    // True when a Q* row of this VID/PID tests a string, so match() needs the device strings
    // to pick the transport (0001:0000, 06da:0002). The caller reads them itself, as NUT
    // does, when the enumeration left them empty (issue #91). Pure.
    bool qxNeedsStrings(uint16_t vid, uint16_t pid);

    // True when the interface has a usage on the UPS (0x84) or Battery (0x85) page: the
    // condition of the generic HID fallback.
    bool hasUpsPages(const std::vector<HIDUsageDef>& usages);

    // New driver for the device, owned by the caller (never null). mfr and product are the
    // USB strings of the device, nullptr or "" when it has none. HID only, also used by the
    // replay tests.
    IUPSDriver* create(uint16_t vid, uint16_t pid, const char* mfr = "", const char* product = "");
}

#endif // DRIVER_REGISTRY_H
