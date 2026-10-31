#ifndef QUIRKS_H
#define QUIRKS_H

#include <stdint.h>

// Quirk flags
#define QUIRK_INVERT_STRINGS       (1 << 0)
#define QUIRK_IGNORE_BATTERY       (1 << 1)
#define QUIRK_NO_STRING_DESCRIPTOR (1 << 2)
#define QUIRK_NO_BEEPER_CONTROL    (1 << 3)
#define QUIRK_NO_INTERRUPT_PIPE    (1 << 4)

struct QuirkDef {
    uint16_t vid;
    uint16_t pid; // 0xFFFF means any PID for this VID
    uint32_t flags;
};

// Common Quirks table can be extended
static const QuirkDef UPS_QUIRKS[] = {
    // CyberPower CP1500PFCLCD inverted strings
    { 0x0764, 0x0501, QUIRK_INVERT_STRINGS },
    // Powercom SPD-750U (and BNT series) non-persistent beeper toggle
    { 0x0D9F, 0x0004, QUIRK_NO_BEEPER_CONTROL },
    // APC Back-UPS (e.g. SU750i, ES 525, CS 650)
    { 0x051D, 0x0002, 0 },
    // APC 5G models: disable_interrupt_pipe, apc-hid.c:51-62: the interrupt pipe carries a
    // proprietary protocol, every value is read with GET_REPORT (US-108)
    { 0x051D, 0x0003, QUIRK_NO_INTERRUPT_PIPE },
    // APC Smart-UPS 1000: disable_interrupt_pipe as above
    { 0x051D, 0x0004, QUIRK_NO_INTERRUPT_PIPE },
    // Terminator
    { 0x0000, 0x0000, 0 }
};

#endif // QUIRKS_H
