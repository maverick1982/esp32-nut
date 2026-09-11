#ifndef QUIRKS_H
#define QUIRKS_H

#include <stdint.h>

// Quirk flags
#define QUIRK_INVERT_STRINGS       (1 << 0)
#define QUIRK_IGNORE_BATTERY       (1 << 1)
#define QUIRK_NO_STRING_DESCRIPTOR (1 << 2)
#define QUIRK_NO_BEEPER_CONTROL    (1 << 3)
#define QUIRK_NO_GET_REPORT        (1 << 4)
#define QUIRK_MAX_REPORT_SIZE_1    (1 << 5)

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
    { 0x051D, 0x0002, QUIRK_MAX_REPORT_SIZE_1 },
    // APC 5G models
    { 0x051D, 0x0003, QUIRK_NO_GET_REPORT },
    // APC Smart-UPS 1000
    { 0x051D, 0x0004, QUIRK_NO_GET_REPORT },
    // Terminator
    { 0x0000, 0x0000, 0 }
};

#endif // QUIRKS_H
