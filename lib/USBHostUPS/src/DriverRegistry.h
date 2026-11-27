#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>
#include <vector>
#include "HIDUsages.h"
#include "UsbSerialDevice.h"

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
 *
 * matchSerial() is the device-level choice of the serial family (ADR 0014, section 2), for
 * the devices usb_host_cdc_acm reports: a device with an HID interface stays with hid_host.
 */
namespace DriverRegistry {
    static const uint16_t ANY_PID = 0xFFFF;

    // USB_SERIAL, not SERIAL: Arduino.h defines SERIAL as a macro
    enum class Family : uint8_t { HID, QX, USB_SERIAL };

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

    // What the serial family does with a new device (ADR 0014, section 2)
    enum class SerialClaim : uint8_t {
        HID_DEVICE,  // it has an HID interface: hid_host and claimInterface() take it, silently
        HUB,         // a USB hub: nothing to do, silently
        CLAIM,       // a serial bridge or a CDC-ACM device: opened by the serial path
        UNSUPPORTED  // none of the above: logged once, left alone
    };

    struct SerialMatch {
        SerialClaim claim;
        const char* adapter;      // "xr21v141x", "cdc-acm"; nullptr unless CLAIM
        const char* chip;         // "XR21V141x", "CDC-ACM", for the log and the banner
        uint8_t interface_number; // interface_idx of cdc_acm_host_open_v2(), with CLAIM
        const char* reason;       // why UNSUPPORTED, for the log
    };

    // Device-level choice of the serial family: (1) an HID interface wins; (2) a hub is
    // ignored; (3) the explicit bridge rows (IDs of the Linux USB serial drivers, data only);
    // (4) the CDC-ACM class fallback (Communication 0x02/0x02 plus a Data 0x0A interface with
    // bulk IN and OUT). Pure: no allocation, no I/O.
    SerialMatch matchSerial(const UsbSerialDeviceInfo& info);

    // New driver for the device, owned by the caller (never null). mfr and product are the
    // USB strings of the device, nullptr or "" when it has none. HID only, also used by the
    // replay tests.
    IUPSDriver* create(uint16_t vid, uint16_t pid, const char* mfr = "", const char* product = "");
}

#endif // DRIVER_REGISTRY_H
