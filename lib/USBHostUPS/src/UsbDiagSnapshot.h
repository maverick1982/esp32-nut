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

// One Q* exchange of the "qx" object (US-136)
struct UsbDiagQxExchange {
    String command;
    std::vector<uint8_t> answer; // raw bytes, at most QxExchange::ANSWER_MAX
    int32_t length = 0;          // return value of the transport: length, or an error < 0
    uint32_t uptime_ms = 0;      // when the command started
    uint32_t duration_ms = 0;
    bool truncated = false;
    String error;
    // Board time of the command in the format of the System Log lines (US-118), set by the
    // web server when the board clock is valid; empty otherwise
    String time;
};

// "qx" object of the export (US-136): written only when present (a Q* UPS is bound), so the
// HID export does not change
struct UsbDiagQx {
    bool present = false;
    String transport;       // NUT USB subdriver of the DriverRegistry row
    String protocol;        // "Megatec 0.10" once claimed, empty before
    String probe;           // "probing", "claimed", "not_supported"; empty without a driver
    uint8_t tries = 0;      // attempts on the protocol being tried
    uint16_t failed_cycles = 0;
    uint32_t retry_ms = 0;  // back-off after the last cycle without a claim
    String forced_protocol; // the protocol setting, empty when automatic
    bool novendor = false;  // the novendor setting (US-138)
    String langid_fix;      // "0x0409" once the transport applied it by itself, empty otherwise (#91)
    // The last string read refused by usb_get_string_simple() (#91), written only when present:
    // first bytes of string descriptor 0 and of the answer, with what each read returned
    bool string_failure = false;
    uint8_t sf_index = 0;
    int32_t sf_result = 0;
    int32_t sf_langids_length = 0;
    std::vector<uint8_t> sf_langids;
    bool sf_answer_asked = false;
    int32_t sf_answer_length = 0;
    std::vector<uint8_t> sf_answer;
    std::vector<UsbDiagQxExchange> exchanges; // oldest first
    uint32_t uptime_ms = 0; // uptime of the snapshot, the reference of the exchange times
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
    // An INPUT silence would make the data stale and trigger a recovery, then a restart: some
    // value comes only from the INPUT reports (US-123). False with QUIRK_NO_INTERRUPT_PIPE.
    bool input_watchdog = false;
    uint32_t port_cycles = 0;       // USB port power cycles since the boot (US-122)
    uint32_t incomplete_input_reports = 0;
    uint16_t ep_in_mps = 0;
    // "export_read" object (US-119): Idle is written as "not_requested"
    DiagSweep::Status export_read;
    // The single "Live ESP32 dump" scenario
    std::vector<UsbDiagReport> reports;    // by key (type << 8) | id, as the report cache
    std::vector<UPSParameter> ups_data;    // expected_ups_data, in UPSData order
    UsbDiagQx qx;                          // Q* section (US-136)
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
