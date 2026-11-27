#ifndef USB_SERIAL_DEVICE_H
#define USB_SERIAL_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief What the serial family needs to know about a new USB device (ADR 0014, section 2).
 *
 * The usb_device_handle_t of the new-device callback of usb_host_cdc_acm is valid only during
 * the callback, so the callback copies the device descriptor fields, the strings and the
 * interface and endpoint layout into this fixed-size record and posts it to the poll task.
 * Plain data, no ESP-IDF type: the parser and the classification run in the native tests.
 */
struct UsbSerialEndpoint {
    uint8_t address = 0;     // bEndpointAddress, bit 7 = IN
    uint8_t attributes = 0;  // bmAttributes, bits 0-1 = transfer type
    uint16_t max_packet = 0; // wMaxPacketSize
    bool isIn() const { return (address & 0x80) != 0; }
    uint8_t type() const { return attributes & 0x03; }
    bool isBulk() const { return type() == 0x02; }
    bool isInterrupt() const { return type() == 0x03; }
};

struct UsbSerialInterface {
    static const uint8_t MAX_ENDPOINTS = 4;
    uint8_t number = 0;     // bInterfaceNumber
    uint8_t alternate = 0;  // bAlternateSetting
    uint8_t cls = 0;        // bInterfaceClass
    uint8_t subclass = 0;
    uint8_t protocol = 0;
    uint8_t endpoint_count = 0; // endpoints kept, at most MAX_ENDPOINTS
    UsbSerialEndpoint endpoints[MAX_ENDPOINTS];
    bool hasBulkInOut() const {
        bool in = false, out = false;
        for (uint8_t i = 0; i < endpoint_count; i++) {
            if (!endpoints[i].isBulk()) continue;
            if (endpoints[i].isIn()) in = true;
            else out = true;
        }
        return in && out;
    }
};

struct UsbSerialDeviceInfo {
    static const uint8_t MAX_INTERFACES = 8;
    static const size_t STRING_LEN = 32;
    uint8_t address = 0;    // USB device address, for cdc_acm_host_open_v2()
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t device_class = 0;
    char manufacturer[STRING_LEN + 1] = {0};
    char product[STRING_LEN + 1] = {0};
    char serial[STRING_LEN + 1] = {0};
    uint8_t interface_count = 0; // interfaces kept, at most MAX_INTERFACES
    UsbSerialInterface interfaces[MAX_INTERFACES];
    // The configuration descriptor had more interfaces or endpoints than the record holds,
    // or ended inside a descriptor: what was kept is valid, the rest is missing
    bool truncated = false;

    bool hasClass(uint8_t cls) const {
        for (uint8_t i = 0; i < interface_count; i++) {
            if (interfaces[i].cls == cls) return true;
        }
        return false;
    }
};

namespace UsbConfigParser {

static const uint8_t DESC_CONFIGURATION = 0x02;
static const uint8_t DESC_INTERFACE = 0x04;
static const uint8_t DESC_ENDPOINT = 0x05;

/**
 * @brief Fills the interfaces of info from a raw configuration descriptor (all of
 * wTotalLength, as usb_host_get_active_config_descriptor() returns it).
 *
 * Every interface descriptor (each alternate setting) and the endpoints that follow it are
 * kept, in their order; class-specific descriptors are skipped. A descriptor with
 * bLength < 2, or one that runs past the end, stops the walk and marks the record truncated,
 * as more interfaces or endpoints than the record holds do.
 * @return false when the data is not a configuration descriptor (nothing is filled)
 */
inline bool parse(const uint8_t* cfg, size_t len, UsbSerialDeviceInfo& info) {
    info.interface_count = 0;
    info.truncated = false;
    if (!cfg || len < 9 || cfg[0] < 9 || cfg[1] != DESC_CONFIGURATION) return false;
    size_t total = (size_t)cfg[2] | ((size_t)cfg[3] << 8);
    if (total > len) {
        total = len;
        info.truncated = true;
    }
    UsbSerialInterface* current = nullptr;
    size_t off = 0;
    while (off < total) {
        const uint8_t blen = cfg[off];
        if (blen < 2 || off + blen > total) {
            info.truncated = true;
            break;
        }
        const uint8_t type = cfg[off + 1];
        if (type == DESC_INTERFACE && blen >= 9) {
            if (info.interface_count < UsbSerialDeviceInfo::MAX_INTERFACES) {
                current = &info.interfaces[info.interface_count++];
                *current = UsbSerialInterface();
                current->number = cfg[off + 2];
                current->alternate = cfg[off + 3];
                current->cls = cfg[off + 5];
                current->subclass = cfg[off + 6];
                current->protocol = cfg[off + 7];
            } else {
                current = nullptr; // its endpoints are dropped with it
                info.truncated = true;
            }
        } else if (type == DESC_ENDPOINT && blen >= 7) {
            if (current) {
                if (current->endpoint_count < UsbSerialInterface::MAX_ENDPOINTS) {
                    UsbSerialEndpoint& ep = current->endpoints[current->endpoint_count++];
                    ep.address = cfg[off + 2];
                    ep.attributes = cfg[off + 3];
                    ep.max_packet = (uint16_t)(cfg[off + 4] | (cfg[off + 5] << 8));
                } else {
                    info.truncated = true;
                }
            }
        }
        off += blen;
    }
    return true;
}

// UTF-16LE string descriptor (bLength, 0x03, code units) to ASCII, '?' for anything else
inline void stringToAscii(const uint8_t* desc, size_t desc_len, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!desc || desc_len < 2 || desc[1] != 0x03) return;
    size_t blen = desc[0] < desc_len ? desc[0] : desc_len;
    size_t o = 0;
    for (size_t i = 2; i + 1 < blen && o + 1 < out_size; i += 2) {
        const uint16_t cu = (uint16_t)(desc[i] | (desc[i + 1] << 8));
        out[o++] = (cu >= 0x20 && cu < 0x7F) ? (char)cu : '?';
    }
    out[o] = '\0';
}

}  // namespace UsbConfigParser

#endif // USB_SERIAL_DEVICE_H
