#ifndef QUIRKS_H
#define QUIRKS_H

#include <stdint.h>

// Quirk flags
#define QUIRK_INVERT_STRINGS    (1 << 0)
#define QUIRK_IGNORE_BATTERY    (1 << 1)
#define QUIRK_NO_STRING_DESCRIPTOR (1 << 2)

struct QuirkDef {
    uint16_t vid;
    uint16_t pid; // 0xFFFF means any PID for this VID
    uint32_t flags;
};

// Common Quirks table can be extended
static const QuirkDef UPS_QUIRKS[] = {
    // CyberPower CP1500PFCLCD inverted strings
    { 0x0764, 0x0501, QUIRK_INVERT_STRINGS },
    // Terminator
    { 0x0000, 0x0000, 0 }
};

#endif // QUIRKS_H
