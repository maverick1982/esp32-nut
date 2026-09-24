#ifndef HID_PARSER_H
#define HID_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <map>
#include "HIDUsages.h"

class HIDParser {
public:
    HIDParser() {}
    
    bool parseReportDescriptor(const uint8_t* desc, size_t len);
    const HIDUsageDef* getUsageDef(uint32_t usage) const;
    const std::vector<HIDUsageDef>& getUsages() const { return _usages; }
    bool hasFeatureBeeperControl() const;
    uint16_t getExpectedLength(uint8_t report_id, uint8_t report_type) const;
    // Declared INPUT report length in bytes, report ID included; 0 if the ID is unknown
    uint16_t getInputLength(uint8_t report_id) const;
    // True when the descriptor declares report IDs (every report then starts with one)
    bool usesReportIds() const;
    
    static double extractUsage(const HIDUsageDef* def, uint8_t report_id, const uint8_t* data, size_t length);
    
private:
    static const uint32_t MAX_USAGE_RANGE = 256;
    std::vector<HIDUsageDef> _usages;
    std::map<uint8_t, uint16_t> _input_lengths;
    std::map<uint8_t, uint16_t> _output_lengths;
    std::map<uint8_t, uint16_t> _feature_lengths;
};

#endif // HID_PARSER_H
