#ifndef DEVICE_STRINGS_H
#define DEVICE_STRINGS_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/**
 * @brief Conversion of USB string descriptors (UTF-16 code units) to ASCII.
 *
 * Pure logic, so it runs in the native unit tests (review C4). Replaces wcstombs(),
 * which returns -1 without terminating the buffer on the first unit it cannot convert.
 *
 * Some CyberPower firmwares send "inverted" strings: every unit is 0xFF00 | ~c.
 * They are detected from the first unit, as GenericDriver::parseStringDescriptor does;
 * QUIRK_INVERT_STRINGS forces the inversion when the first unit says nothing.
 */
class DeviceStrings {
public:
    /**
     * @param src         NUL-terminated code units, at most max_units long
     * @param invert      QUIRK_INVERT_STRINGS is set for the device
     * @param dst         output buffer, always NUL-terminated (dst_size > 0)
     * @return length of the string written to dst
     */
    static size_t toAscii(const wchar_t* src, size_t max_units, bool invert, char* dst, size_t dst_size) {
        if (!dst || dst_size == 0) return 0;
        dst[0] = '\0';
        if (!src || max_units == 0) return 0;

        uint32_t first = (uint32_t)src[0] & 0xFFFF;
        if ((first >> 8) == 0xFF) invert = true;
        else if (first != 0 && (first >> 8) == 0x00) invert = false;

        size_t out = 0;
        for (size_t i = 0; i < max_units && src[i] != 0 && out + 1 < dst_size; i++) {
            uint32_t unit = (uint32_t)src[i] & 0xFFFF;
            uint8_t c;
            if (invert && (unit >> 8) == 0xFF) c = (uint8_t)~unit;
            else if (unit < 0x80) c = (uint8_t)unit;
            else c = '?'; // outside ASCII: keep the position, NUT values are ASCII
            if (c < 0x20 || c >= 0x7F) continue; // control characters
            dst[out++] = (char)c;
        }
        // Firmwares pad fixed-size fields with spaces
        while (out > 0 && dst[out - 1] == ' ') out--;
        dst[out] = '\0';
        return out;
    }
};

#endif // DEVICE_STRINGS_H
