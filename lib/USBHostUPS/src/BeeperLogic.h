#ifndef BEEPER_LOGIC_H
#define BEEPER_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include "HIDParser.h"
#include "IUPSDriver.h"

class BeeperLogic {
public:
    static size_t manipulateBeeperBuffer(bool enable, const HIDUsageDef* def, uint8_t* buffer, size_t fetched_len, IUPSDriver* driver) {
        if (!def || !buffer) return 0;

        bool has_report_id = (def->report_id != 0);
        size_t byte_index = has_report_id ? 1 : 0;
        
        // Enlarge if report was unexpectedly small
        if (byte_index + def->bit_offset / 8 >= fetched_len) {
            size_t needed = byte_index + def->bit_offset / 8 + 1;
            if (needed <= 256) fetched_len = needed;
            else return 0; 
        }
        
        // Encode device-specific value
        uint8_t val = driver ? driver->encodeBeeperValue(enable, def->bit_size) : (def->bit_size == 1 ? (enable ? 1 : 0) : (enable ? 2 : 1));
        
        // Modify buffer (using exact masking for non-aligned fields)
        uint8_t bit_shift = def->bit_offset % 8;
        uint8_t target_idx = byte_index + (def->bit_offset / 8);
        
        uint8_t mask = (1 << def->bit_size) - 1;
        if (def->bit_size == 8) mask = 0xFF; // Avoid shift overflow behavior
        
        buffer[target_idx] &= ~(mask << bit_shift);
        buffer[target_idx] |= (val & mask) << bit_shift;
        
        return fetched_len;
    }
};

#endif
