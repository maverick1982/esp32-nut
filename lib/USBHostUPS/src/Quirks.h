#ifndef QUIRKS_H
#define QUIRKS_H

#include <stdint.h>

// Quirk flags
#define QUIRK_INVERT_STRINGS       (1 << 0)
#define QUIRK_IGNORE_BATTERY       (1 << 1)
#define QUIRK_NO_STRING_DESCRIPTOR (1 << 2)
#define QUIRK_NO_INTERRUPT_PIPE    (1 << 4)
// Every GET_REPORT asks at least QUIRK_MIN_REQUEST_LENGTH bytes, never less than the
// declared length (GenericDriver::requestLength). NUT's libhid.c never asks the declared
// length either: sizeof(rbuf->data[id]) (8 on 64 bit) up to ab2ccd7, MAX_REPORT_ALLOC
// (256) since. A device that answers with more than it declares overruns a request of the
// exact length, and the ESP-IDF host controller driver does not survive that.
#define QUIRK_MIN_REQUEST_8        (1 << 5)
#define QUIRK_MIN_REQUEST_LENGTH   8

struct QuirkDef {
    uint16_t vid;
    uint16_t pid; // 0xFFFF means any PID for this VID
    uint32_t flags;
};

// Common Quirks table can be extended
static const QuirkDef UPS_QUIRKS[] = {
    // CyberPower CP1500PFCLCD inverted strings
    { 0x0764, 0x0501, QUIRK_INVERT_STRINGS },
    // Powercom SPD-750U (and BNT series): the beeper commands are offered as in NUT
    // (US-109; QUIRK_NO_BEEPER_CONTROL was unused since e14b3d2 and is gone)
    { 0x0D9F, 0x0004, 0 },
    // APC Back-UPS (e.g. SU750i, ES 525, CS 650)
    { 0x051D, 0x0002, 0 },
    // APC 5G models: disable_interrupt_pipe, apc-hid.c:51-62: the interrupt pipe carries a
    // proprietary protocol, every value is read with GET_REPORT (US-108)
    { 0x051D, 0x0003, QUIRK_NO_INTERRUPT_PIPE },
    // APC Smart-UPS 1000: disable_interrupt_pipe as above
    { 0x051D, 0x0004, QUIRK_NO_INTERRUPT_PIPE },
    // Liebert PSI5 / Vertiv Edge (10af:0002): FEATURE report 30 (Outlet.PercentLoad) is
    // declared 8 bits long (2 bytes with the ID) but the UPS answers 3 bytes. With wLength 2
    // the overrun trips assert(urb != NULL) in hcd_dwc.c _buffer_parse() and the board
    // panics a few seconds after every enumeration; with wLength 8 the transfer completes.
    { 0x10AF, 0x0002, QUIRK_MIN_REQUEST_8 },
    // Terminator
    { 0x0000, 0x0000, 0 }
};

#endif // QUIRKS_H
