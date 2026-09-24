#ifndef USAGE_MAP_INDEX_H
#define USAGE_MAP_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "HIDUsages.h"
#include "HIDParser.h"

struct UPSData;

/**
 * @brief Precomputed match between the device usages and a driver mapping table (review S2).
 *
 * Decoding used to compare every usage of the device with every mapping path (strcmp) for
 * each report, again in the base driver and in the derived one. The index is built once,
 * on the first report after the usages change, and keeps for each report only its usages
 * with the mapping they match. The result is the same as the old loops: usages in
 * descriptor order, first matching mapping only.
 */
template <typename Drv>
class UsageMapIndex {
public:
    struct Mapping {
        const char* path;
        void (*apply)(Drv*, UPSData&, double, const HIDUsageDef*);
    };

    void invalidate() {
        _count = (size_t)-1;
        _entries.clear();
    }

    template <size_t N>
    void apply(Drv* drv, const Mapping (&table)[N], const std::vector<HIDUsageDef>& usages,
               uint8_t report_id, uint8_t report_type, const uint8_t* data, size_t length, UPSData& ups_data) {
        uint32_t fp = fingerprint(usages);
        if (usages.size() != _count || fp != _fingerprint) build(table, N, usages, fp);

        const uint16_t key = (uint16_t)((report_type << 8) | report_id);
        auto it = std::lower_bound(_entries.begin(), _entries.end(), key,
                                   [](const Entry& e, uint16_t k) { return e.key < k; });
        for (; it != _entries.end() && it->key == key; ++it) {
            const HIDUsageDef& u = usages[it->usage];
            double val = HIDParser::extractUsage(&u, report_id, data, length);
            table[it->mapping].apply(drv, ups_data, val, &u);
        }
    }

private:
    struct Entry {
        uint16_t key;
        uint16_t usage;
        uint16_t mapping;
    };

    // O(1) identity of the usage list. A new device also recreates the driver, and
    // setup() calls invalidate(): this only guards against a list changed in place.
    static uint32_t fingerprint(const std::vector<HIDUsageDef>& usages) {
        uint32_t h = (uint32_t)(uintptr_t)usages.data();
        if (!usages.empty()) {
            const HIDUsageDef& a = usages.front();
            const HIDUsageDef& b = usages.back();
            h ^= a.usage * 31u + a.report_id + ((uint32_t)a.bit_offset << 8);
            h = h * 16777619u ^ (b.usage * 31u + b.report_id + ((uint32_t)b.bit_offset << 8));
        }
        return h;
    }

    void build(const Mapping* table, size_t n, const std::vector<HIDUsageDef>& usages, uint32_t fp) {
        _entries.clear();
        for (size_t i = 0; i < usages.size() && i <= 0xFFFF; i++) {
            const HIDUsageDef& u = usages[i];
            for (size_t m = 0; m < n; m++) {
                if (strcmp(u.path, table[m].path) == 0) {
                    _entries.push_back(Entry{(uint16_t)((u.report_type << 8) | u.report_id), (uint16_t)i, (uint16_t)m});
                    break;
                }
            }
        }
        // Stable: usages of a report keep their descriptor order
        std::stable_sort(_entries.begin(), _entries.end(),
                         [](const Entry& a, const Entry& b) { return a.key < b.key; });
        _count = usages.size();
        _fingerprint = fp;
    }

    size_t _count = (size_t)-1;
    uint32_t _fingerprint = 0;
    std::vector<Entry> _entries;
};

#endif // USAGE_MAP_INDEX_H
