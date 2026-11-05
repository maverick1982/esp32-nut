#ifndef USB_DIAG_SNAPSHOT_H
#define USB_DIAG_SNAPSHOT_H

#include <Arduino.h>
#include <cstdint>
#include <vector>
#include "UPSData.h"
#include "DiagSweep.h"

// Copy of the data behind the USB diagnostics export (/api/usb/dump), taken under the host
// mutex by USBHostUPS::snapshotUSBDiagnostics(). UsbDiagJson writes the JSON from it outside
// the lock, without a JsonDocument (US-096). Fields in export order, with the same types as
// the values they copy, so numbers print the same.
struct UsbDiagReport {
    uint8_t id = 0;
    uint8_t type = 0;
    std::vector<uint8_t> data;
    // Read by the on-demand read of the export, not by the poll (US-119); empty: the device
    // answered with 0 bytes (data stays empty). Written only when true.
    bool on_demand = false;
    bool empty = false;
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
    // "export_read" object (US-119): Idle is written as "not_requested"
    DiagSweep::Status export_read;
    // The single "Live ESP32 dump" scenario
    std::vector<UsbDiagReport> reports;    // by key (type << 8) | id, as the report cache
    std::vector<UPSParameter> ups_data;    // expected_ups_data, in UPSData order
};

// Adds the reports of the on-demand read (US-119) whose key the poll's reports do not hold,
// marked on_demand, and keeps the reports ordered by key (type << 8) | id. reports must
// already be in that order (the report cache is a map on the same key).
inline void addOnDemandReports(std::vector<UsbDiagReport>& reports,
                               const std::vector<DiagSweep::Entry>& results) {
    auto keyOf = [](const UsbDiagReport& r) { return DiagSweep::key(r.type, r.id); };
    for (const auto& e : results) {
        const uint16_t k = DiagSweep::key(e.type, e.id);
        auto pos = reports.begin();
        while (pos != reports.end() && keyOf(*pos) < k) ++pos;
        if (pos != reports.end() && keyOf(*pos) == k) continue; // the poll's copy wins
        UsbDiagReport r;
        r.id = e.id;
        r.type = e.type;
        r.data = e.data;
        r.on_demand = true;
        r.empty = e.empty;
        reports.insert(pos, std::move(r));
    }
}

#endif // USB_DIAG_SNAPSHOT_H
