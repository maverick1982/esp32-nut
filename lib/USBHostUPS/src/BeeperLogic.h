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
        size_t needed = byte_index + (def->bit_offset + def->bit_size + 7) / 8;
        if (needed > fetched_len) {
            if (needed <= 256) fetched_len = needed;
            else return 0; 
        }
        
        // Encode device-specific value
        uint32_t val = driver ? driver->encodeBeeperValue(enable, def->bit_size) : (def->bit_size == 1 ? (enable ? 1 : 0) : (enable ? 2 : 1));
        
        // Modify buffer (using exact masking for non-aligned fields)
        uint8_t bit_shift = def->bit_offset % 8;
        uint32_t mask = (def->bit_size >= 32) ? 0xFFFFFFFF : ((1ULL << def->bit_size) - 1);
        
        // Apply val to buffer across multiple bytes
        val &= mask;
        val <<= bit_shift;
        mask <<= bit_shift;
        
        size_t target_idx = byte_index + (def->bit_offset / 8);
        for (int i = 0; i < 4 && target_idx + i < fetched_len; i++) {
            uint8_t byte_mask = (mask >> (i * 8)) & 0xFF;
            if (!byte_mask && i > 0) break; // Reached end of mask
            
            buffer[target_idx + i] &= ~byte_mask;
            buffer[target_idx + i] |= ((val >> (i * 8)) & byte_mask);
        }
        
        return fetched_len;
    }
};

#endif
