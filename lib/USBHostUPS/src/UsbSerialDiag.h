#ifndef USB_SERIAL_DIAG_H
#define USB_SERIAL_DIAG_H

#include <stdio.h>
#include "UsbDiagSnapshot.h"
#include "UsbSerialLink.h"

namespace UsbSerialDiag {

inline const char* endpointType(const UsbSerialEndpoint& ep) {
    switch (ep.type()) {
        case 0x00: return "control";
        case 0x01: return "isochronous";
        case 0x02: return "bulk";
        default:   return "interrupt";
    }
}

inline String hex16(uint16_t v) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%04X", (unsigned)v);
    return String(buf);
}

/**
 * @brief The "serial" object of the USB diagnostics export (ADR 0014, section 5) from the
 * claimed device. Without one, d is reset and stays absent from the export.
 */
inline void fill(const UsbSerialLink& link, UsbDiagSerial& d) {
    d = UsbDiagSerial();
    if (!link.present()) return;
    const UsbSerialDeviceInfo& info = link.info();
    d.present = true;
    d.vid = hex16(info.vid);
    d.pid = hex16(info.pid);
    d.manufacturer = info.manufacturer;
    d.product = info.product;
    d.serial_number = info.serial;
    d.address = info.address;
    d.chip = link.chip();
    d.adapter = link.adapter();
    d.interface_number = link.interfaceNumber();
    d.state = link.stateName();
    d.open_result = link.openResult();
    d.rx_bytes = link.rxBytes();
    d.rx_chunks = link.rxChunks();
    d.tx_bytes = link.txBytes();
    d.last_event = link.lastEvent();
    d.last_error = link.lastError();
    d.attached_ms = link.attachedAt();
    d.descriptor_truncated = info.truncated;
    for (uint8_t i = 0; i < info.interface_count; i++) {
        const UsbSerialInterface& it = info.interfaces[i];
        UsbDiagSerialInterface di;
        di.number = it.number;
        di.alternate = it.alternate;
        di.cls = it.cls;
        di.subclass = it.subclass;
        di.protocol = it.protocol;
        for (uint8_t j = 0; j < it.endpoint_count; j++) {
            const UsbSerialEndpoint& ep = it.endpoints[j];
            UsbDiagSerialEndpoint de;
            de.address = ep.address;
            de.type = endpointType(ep);
            de.direction = ep.isIn() ? "in" : "out";
            de.max_packet = ep.max_packet;
            di.endpoints.push_back(de);
        }
        d.interfaces.push_back(di);
    }
}

}  // namespace UsbSerialDiag

#endif // USB_SERIAL_DIAG_H
