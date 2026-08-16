#include "HIDParser.h"
#include <map>
#include "NUTUsages.h"

bool HIDParser::parseReportDescriptor(const uint8_t* desc, size_t len) {
    _usages.clear();
    uint32_t current_usage_page = 0;
    uint8_t current_report_id = 0;
    uint16_t report_size = 0;
    uint16_t report_count = 0;
    int8_t current_exponent = 0;
    uint32_t current_unit = 0;
    
    struct GlobalState {
        uint32_t usage_page;
        uint8_t report_id;
        uint16_t report_size;
        uint16_t report_count;
        int8_t exponent;
        uint32_t unit;
    };
    std::vector<GlobalState> global_stack;
    
    std::map<uint8_t, uint16_t> input_offsets;
    std::map<uint8_t, uint16_t> feature_offsets;
    std::vector<uint32_t> local_usages;
    std::vector<String> collection_names;
    
    size_t i = 0;
    while (i < len) {
        uint8_t bSize = desc[i] & 0x03;
        if (bSize == 3) bSize = 4;
        uint8_t bType = (desc[i] >> 2) & 0x03;
        uint8_t bTag = (desc[i] >> 4) & 0x0F;
        i++;
        
        uint32_t data = 0;
        for (int j = 0; j < bSize && i < len; j++) {
            data |= (desc[i++] << (8 * j));
        }
        
        if (bType == 0) { // Main
            if (bTag == 8 || bTag == 9 || bTag == 11) { // Input, Output, Feature
                uint16_t size_bits = report_size;
                
                std::map<uint8_t, uint16_t>* offsets_map = nullptr;
                if (bTag == 8) offsets_map = &input_offsets;
                else if (bTag == 11) offsets_map = &feature_offsets;
                
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
                        def.logical_min = 0;
                        def.logical_max = 0;
                        def.exponent = current_exponent;
                        def.unit = current_unit;
                        
                        String full_path = "";
                        for (const String& n : collection_names) {
                            if (full_path.length() > 0) full_path += ".";
                            full_path += n;
                        }
                        String leaf = get_nut_usage_name(usage);
                        if (full_path.length() > 0) def.path = full_path + "." + leaf;
                        else def.path = leaf;
                        
                        _usages.push_back(def);
                    }
                    if (offsets_map) {
                        (*offsets_map)[current_report_id] += size_bits;
                    }
                }
                local_usages.clear();
            } else if (bTag == 10) { // Collection
                uint32_t usage = 0;
                if (!local_usages.empty()) {
                    usage = local_usages.back();
                }
                collection_names.push_back(get_nut_usage_name(usage));
                local_usages.clear();
            } else if (bTag == 12) { // End Collection
                if (!collection_names.empty()) {
                    collection_names.pop_back();
                }
            }
        } else if (bType == 1) { // Global
            if (bTag == 0) current_usage_page = data;
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
                global_stack.push_back({current_usage_page, current_report_id, report_size, report_count, current_exponent, current_unit});
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
                }
            }
        } else if (bType == 2) { // Local
            if (bTag == 0) { // Usage
                if (bSize <= 2) {
                    local_usages.push_back((current_usage_page << 16) | data);
                } else {
                    local_usages.push_back(data);
                }
            }
        }
    }
    return true;
}

const HIDUsageDef* HIDParser::getUsageDef(uint32_t usage) const {
    for (const auto& u : _usages) {
        if (u.usage == usage) return &u;
    }
    return nullptr;
}

double HIDParser::extractUsage(const HIDUsageDef* def, uint8_t report_id, const uint8_t* data, size_t length) {
    if (!def || !def->found || !data) return 0.0;
    
    uint16_t bit_offset = def->bit_offset;
    if (def->report_id != 0) {
        if (data[0] != def->report_id) return 0.0;
        bit_offset += 8;
    }
    
    if ((bit_offset + def->bit_size) > (length * 8)) return 0.0;
    
    uint16_t byte_idx = bit_offset / 8;
    uint8_t bit_shift = bit_offset % 8;
    
    uint64_t raw = 0;
    for (int i = 0; i < ((def->bit_size + bit_shift + 7) / 8) && (byte_idx + i) < length; i++) {
        raw |= ((uint64_t)data[byte_idx + i]) << (i * 8);
    }
    raw >>= bit_shift;
    raw &= (1ULL << def->bit_size) - 1;
    
    double val = (double)(int32_t)raw;
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
