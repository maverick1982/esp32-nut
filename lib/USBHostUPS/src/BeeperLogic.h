#ifndef BEEPER_LOGIC_H
#define BEEPER_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include "HIDParser.h"
#include "IUPSDriver.h"

class BeeperLogic {
public:
    // Bytes a SET_REPORT must carry to reach the beeper field
    static size_t requiredLength(const HIDUsageDef& def) {
        return (def.report_id != 0 ? 1 : 0) + (def.bit_offset + def.bit_size + 7) / 8;
    }

    // True when the beeper report also carries other usages. The parser records OUTPUT
    // usages with the FEATURE type, so both reports with the same id count: when in
    // doubt, the report is treated as shared.
    static bool reportHasOtherFields(const std::vector<HIDUsageDef>& usages, const HIDUsageDef& def) {
        for (const auto& u : usages) {
            if (u.report_id != def.report_id || u.report_type != def.report_type) continue;
            if (u.usage == def.usage && u.bit_offset == def.bit_offset && strcmp(u.path, def.path) == 0) continue;
            return true;
        }
        return false;
    }

    /**
     * A SET_REPORT writes the whole report: every byte not read back from the device
     * goes out as zero. In a shared report that may be DelayBeforeShutdown = 0, i.e.
     * an immediate load.off (review C1). The write is allowed only when the report was
     * read back up to the beeper field, or when it holds nothing but the beeper.
     */
    static bool canWriteBack(bool shared_report, const HIDUsageDef& def, bool fetched, size_t fetched_len) {
        return !shared_report || (fetched && fetched_len >= requiredLength(def));
    }

    static size_t manipulateBeeperBuffer(bool enable, const HIDUsageDef* def, uint8_t* buffer, size_t fetched_len, IUPSDriver* driver) {
        if (!def || !buffer) return 0;

        bool has_report_id = (def->report_id != 0);
        size_t byte_index = has_report_id ? 1 : 0;
        
        // Enlarge if report was unexpectedly small
        size_t needed = requiredLength(*def);
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
