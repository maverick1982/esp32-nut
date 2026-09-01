#ifndef HID_PARSER_H
#define HID_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include "HIDUsages.h"

class HIDParser {
public:
    HIDParser() {}
    
    bool parseReportDescriptor(const uint8_t* desc, size_t len);
    const HIDUsageDef* getUsageDef(uint32_t usage) const;
    const std::vector<HIDUsageDef>& getUsages() const { return _usages; }
    bool hasFeatureBeeperControl() const;
    
    static double extractUsage(const HIDUsageDef* def, uint8_t report_id, const uint8_t* data, size_t length);
    
private:
    std::vector<HIDUsageDef> _usages;
};

#endif // HID_PARSER_H
