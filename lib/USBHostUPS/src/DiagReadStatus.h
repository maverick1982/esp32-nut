#ifndef DIAG_READ_STATUS_H
#define DIAG_READ_STATUS_H

#include <stdint.h>

// State and counters of the on-demand read of the USB diagnostics export (US-119, issue #123).
// Kept apart from DiagSweep.h so that the consumer headers (WebApiJson.h) bring in no HID
// type (US-103, ADR 0010).
enum class DiagReadState : uint8_t { Idle, Reading, Done, Interrupted };

struct DiagReadStatus {
    DiagReadState state = DiagReadState::Idle;
    uint16_t total = 0;
    uint16_t done = 0;
    uint16_t read = 0;
    uint16_t empty = 0;
    uint16_t failed = 0;
};

#endif // DIAG_READ_STATUS_H
