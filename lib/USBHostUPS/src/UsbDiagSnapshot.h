#ifndef USB_DIAG_SNAPSHOT_H
#define USB_DIAG_SNAPSHOT_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "UPSData.h"

// Copy of the data behind the USB diagnostics export (/api/usb/dump), taken under the host
// mutex by USBHostUPS::snapshotUSBDiagnostics(). UsbDiagJson writes the JSON from it outside
// the lock, without a JsonDocument (US-096). Fields in export order, with the same types as
// the values they copy, so numbers print the same.
struct UsbDiagReport {
    uint8_t id = 0;
    uint8_t type = 0;
    std::vector<uint8_t> data;
};

struct UsbDiagSnapshot {
    String firmware_version;
    String vid;
    String pid;
    String manufacturer;
    String product;
    String serial_number;
    String descriptor_hex;  // raw hex string of the report descriptor, two digits per byte
    uint32_t quirks = 0;
    String driver;
    // "link" object
    uint8_t link_failures = 0;
    uint8_t link_recoveries = 0;
    bool stale = false;
    uint32_t dropped_input_reports = 0;
    uint32_t input_period_ms = 0;
    uint8_t input_recoveries = 0;
    uint32_t incomplete_input_reports = 0;
    uint16_t ep_in_mps = 0;
    // The single "Live ESP32 dump" scenario
    std::vector<UsbDiagReport> reports;    // in report-cache order
    std::vector<UPSParameter> ups_data;    // expected_ups_data, in UPSData order
};

#endif // USB_DIAG_SNAPSHOT_H
