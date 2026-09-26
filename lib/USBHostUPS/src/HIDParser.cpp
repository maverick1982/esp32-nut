#include "HIDParser.h"
#include <map>
#include "NUTUsages.h"

// Appends ".seg" (or "seg" to an empty path), truncated to cap. A NULL segment is
// skipped: an Arduino String left without a buffer returns NULL from c_str(), and
// strncat on it crashed the board at enumeration (issue #55).
static void appendPathSegment(char* path, size_t cap, const char* seg) {
    if (!seg) return;
    if (path[0] != '\0') strncat(path, ".", cap - strlen(path) - 1);
    strncat(path, seg, cap - strlen(path) - 1);
}

bool HIDParser::parseReportDescriptor(const uint8_t* desc, size_t len) {
    _usages.clear();
    _input_lengths.clear();
    _output_lengths.clear();
    _feature_lengths.clear();
    uint32_t current_usage_page = 0;
    uint8_t current_report_id = 0;
    uint16_t report_size = 0;
    uint16_t report_count = 0;
    int8_t current_exponent = 0;
    uint32_t current_unit = 0;
    int32_t logical_min = 0;
    int32_t logical_max = 0;

    struct GlobalState {
        uint32_t usage_page;
        uint8_t report_id;
        uint16_t report_size;
        uint16_t report_count;
        int8_t exponent;
        uint32_t unit;
        int32_t logical_min;
        int32_t logical_max;
    };
    std::vector<GlobalState> global_stack;

    std::vector<uint32_t> local_usages;
    std::vector<String> collection_names;
    // Usage Minimum / Maximum (review M1): expanded into local_usages once both are known
    bool have_usage_min = false, have_usage_max = false;
    uint32_t usage_min = 0, usage_max = 0;

    size_t i = 0;
    while (i < len) {
        // Long item (review M1): 0xFE, bDataSize, bLongItemTag, data. Skipped whole:
        // read as a short item it desynchronised the rest of the descriptor.
        if (desc[i] == 0xFE) {
            if (i + 1 >= len) break;
            i += 3 + desc[i + 1];
            continue;
        }

        uint8_t bSize = desc[i] & 0x03;
        if (bSize == 3) bSize = 4;
        uint8_t bType = (desc[i] >> 2) & 0x03;
        uint8_t bTag = (desc[i] >> 4) & 0x0F;
        i++;

        uint32_t data = 0;
        for (int j = 0; j < bSize && i < len; j++) {
            data |= ((uint32_t)desc[i++]) << (8 * j); // unsigned: desc[i] << 24 overflowed an int
        }
        // Global items such as Logical Minimum are signed in the size they are sent in
        int32_t sdata = (bSize == 1) ? (int32_t)(int8_t)data
                      : (bSize == 2) ? (int32_t)(int16_t)data
                      : (int32_t)data;
        
        if (bType == 0) { // Main
            if (bTag == 8 || bTag == 9 || bTag == 11) { // Input, Output, Feature
                uint16_t size_bits = report_size;
                
                std::map<uint8_t, uint16_t>* offsets_map = nullptr;
                if (bTag == 8) offsets_map = &_input_lengths;
                else if (bTag == 9) offsets_map = &_output_lengths;
                else if (bTag == 11) offsets_map = &_feature_lengths;
                
                for (uint16_t c = 0; c < report_count; c++) {
                    uint32_t usage = 0;
                    if (c < local_usages.size()) {
                        usage = local_usages[c];
                    } else if (!local_usages.empty()) {
                        usage = local_usages.back();
                    }
                    
                    if (offsets_map && usage != 0) {
                        HIDUsageDef def;
                        def.usage = usage;
                        def.report_id = current_report_id;
                        def.report_type = (bTag == 8) ? 0x01 : 0x03;
                        def.bit_offset = (*offsets_map)[current_report_id];
                        def.bit_size = size_bits;
                        def.found = true;
                        def.logical_min = logical_min;
                        def.logical_max = logical_max;
                        def.exponent = current_exponent;
                        def.unit = current_unit;
                        
                        def.path[0] = '\0';
                        for (const String& n : collection_names) {
                            appendPathSegment(def.path, sizeof(def.path), n.c_str());
                        }
                        appendPathSegment(def.path, sizeof(def.path), get_nut_usage_name(usage).c_str());
                        _usages.push_back(def);
                    }
                    if (offsets_map) {
                        (*offsets_map)[current_report_id] += size_bits;
                    }
                }
                local_usages.clear();
                have_usage_min = have_usage_max = false;
            } else if (bTag == 10) { // Collection
                uint32_t usage = 0;
                if (!local_usages.empty()) {
                    usage = local_usages.back();
                }
                collection_names.push_back(get_nut_usage_name(usage));
                local_usages.clear();
                have_usage_min = have_usage_max = false;
            } else if (bTag == 12) { // End Collection
                if (!collection_names.empty()) {
                    collection_names.pop_back();
                }
            }
        } else if (bType == 1) { // Global
            if (bTag == 0) current_usage_page = data;
            else if (bTag == 1) logical_min = sdata;
            else if (bTag == 2) {
                // Descriptors often send an unsigned maximum (e.g. 0xFF in one byte for 255)
                logical_max = (sdata < logical_min) ? (int32_t)data : sdata;
            }
            else if (bTag == 5) {
                int8_t nibble = data & 0x0F;
                if (nibble > 7) nibble -= 16;
                current_exponent = nibble;
            }
            else if (bTag == 6) current_unit = data;
            else if (bTag == 7) report_size = data;
            else if (bTag == 8) current_report_id = data;
            else if (bTag == 9) report_count = data;
            else if (bTag == 10) { // Push
                global_stack.push_back({current_usage_page, current_report_id, report_size, report_count,
                                        current_exponent, current_unit, logical_min, logical_max});
            }
            else if (bTag == 11) { // Pop
                if (!global_stack.empty()) {
                    auto state = global_stack.back();
                    global_stack.pop_back();
                    current_usage_page = state.usage_page;
                    current_report_id = state.report_id;
                    report_size = state.report_size;
                    report_count = state.report_count;
                    current_exponent = state.exponent;
                    current_unit = state.unit;
                    logical_min = state.logical_min;
                    logical_max = state.logical_max;
                }
            }
        } else if (bType == 2) { // Local
            uint32_t full_usage = (bSize <= 2) ? ((current_usage_page << 16) | data) : data;
            if (bTag == 0) { // Usage
                local_usages.push_back(full_usage);
            } else if (bTag == 1 || bTag == 2) { // Usage Minimum / Maximum
                if (bTag == 1) { usage_min = full_usage; have_usage_min = true; }
                else { usage_max = full_usage; have_usage_max = true; }
                if (have_usage_min && have_usage_max) {
                    // Same page only, and bounded: a corrupt range must not exhaust the heap
                    if ((usage_min >> 16) == (usage_max >> 16) && usage_max >= usage_min &&
                        usage_max - usage_min < MAX_USAGE_RANGE) {
                        for (uint32_t u = usage_min; u <= usage_max; u++) local_usages.push_back(u);
                    }
                    have_usage_min = have_usage_max = false;
                }
            }
        }
    }
    return true;
}

uint16_t HIDParser::getExpectedLength(uint8_t report_id, uint8_t report_type) const {
    uint16_t bits = 0;
    if (report_type == 1) { // Input
        auto it = _input_lengths.find(report_id);
        if (it != _input_lengths.end()) bits = it->second;
    } else if (report_type == 2) { // Output
        auto it = _output_lengths.find(report_id);
        if (it != _output_lengths.end()) bits = it->second;
    } else if (report_type == 3) { // Feature
        auto it = _feature_lengths.find(report_id);
        if (it != _feature_lengths.end()) bits = it->second;
    }
    
    if (bits == 0) return 64; // Safe fallback if not found
    
    uint16_t bytes = (bits + 7) / 8;
    if (report_id != 0) bytes += 1; // Include Report ID prefix
    return bytes;
}

uint16_t HIDParser::getInputLength(uint8_t report_id) const {
    auto it = _input_lengths.find(report_id);
    if (it == _input_lengths.end() || it->second == 0) return 0;
    return (it->second + 7) / 8 + (report_id != 0 ? 1 : 0);
}

bool HIDParser::usesReportIds() const {
    for (const auto* m : {&_input_lengths, &_output_lengths, &_feature_lengths}) {
        for (const auto& kv : *m) {
            if (kv.first != 0) return true;
        }
    }
    return false;
}

const HIDUsageDef* HIDParser::getUsageDef(uint32_t usage) const {
    for (const auto& u : _usages) {
        if (u.usage == usage) return &u;
    }
    return nullptr;
}

bool HIDParser::hasFeatureBeeperControl() const {
    for (const auto& u : _usages) {
        if ((u.usage == 0x0084005A || u.usage == 0x0085005A || strstr(u.path, "AudibleAlarmControl") != nullptr)
            && (u.report_type == 0x03 || u.report_type == 0x02)) {
            return true;
        }
    }
    return false;
}

double HIDParser::extractUsage(const HIDUsageDef* def, uint8_t report_id, const uint8_t* data, size_t length) {
    if (!def || !def->found || !data) return 0.0;
    
    uint16_t bit_offset = def->bit_offset;
    if (def->report_id != 0) {
        if (data[0] != def->report_id) return 0.0;
        bit_offset += 8;
    }
    
    // Some UPS devices (e.g. APC) have buggy descriptors that declare a larger
    // size (like 32 bits for input voltage) than the actual payload returned.
    // We tolerate short reports by allowing the loop below to read up to 'length'.
    
    // Fields wider than 32 bits are read as their low 32 bits (review M1: a shift by 64
    // was undefined behaviour)
    uint16_t bits = def->bit_size;
    if (bits == 0) return 0.0;
    if (bits > 32) bits = 32;

    uint16_t byte_idx = bit_offset / 8;
    uint8_t bit_shift = bit_offset % 8;

    uint64_t raw = 0;
    for (int i = 0; i < ((bits + bit_shift + 7) / 8) && (byte_idx + i) < length; i++) {
        raw |= ((uint64_t)data[byte_idx + i]) << (i * 8);
    }
    raw >>= bit_shift;
    raw &= (bits == 32) ? 0xFFFFFFFFULL : ((1ULL << bits) - 1);

    // Signed only when the descriptor says so (review M1): a negative Logical Minimum.
    // Discharge currents, for example, are negative.
    int64_t value = (int64_t)raw;
    if (def->logical_min < 0 && ((raw >> (bits - 1)) & 1)) {
        value -= (int64_t)1 << bits;
    }
    double val = (double)value;
    if (def->bit_size > 1) {
        int8_t unit_expo = def->exponent;
        if (def->unit == 0x00F0D121 || def->unit == 0x0000D121) {
            unit_expo -= 7;
        }
        
        if (unit_expo != 0) {
            val *= pow(10.0, unit_expo);
        }
    }
    return val;
}
