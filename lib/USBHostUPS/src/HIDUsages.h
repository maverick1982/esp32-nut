#ifndef HID_USAGES_H
#define HID_USAGES_H

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

#define HID_PAGE_UPS            0x00840000
#define HID_PAGE_BATTERY        0x00850000

// UPS Page
#define HID_USAGE_UPS_PRESENTSTATUS          (HID_PAGE_UPS | 0x0002)
#define HID_USAGE_UPS_CONFIGAPPPOW           (HID_PAGE_UPS | 0x0024)
#define HID_USAGE_UPS_CONFIGACTPOW           (HID_PAGE_UPS | 0x0025)
#define HID_USAGE_UPS_CONFIGVOLTAGE          (HID_PAGE_UPS | 0x0030)
#define HID_USAGE_UPS_OUTVOLTAGE             (HID_PAGE_UPS | 0x005a)
#define HID_USAGE_UPS_INVOLTAGE              (HID_PAGE_UPS | 0x0056)
#define HID_USAGE_UPS_LOAD                   (HID_PAGE_UPS | 0x0040)
#define HID_USAGE_UPS_ACPRAESENT             (HID_PAGE_UPS | 0x00d0)
#define HID_USAGE_UPS_DISCHARGING            (HID_PAGE_UPS | 0x00d1)
#define HID_USAGE_UPS_CHARGING               (HID_PAGE_UPS | 0x00d2)
#define HID_USAGE_UPS_BELOWREMCAP            (HID_PAGE_UPS | 0x00d3)
#define HID_USAGE_UPS_NEEDREPLACE            (HID_PAGE_UPS | 0x00d4)
#define HID_USAGE_UPS_OVERLOAD               (HID_PAGE_UPS | 0x00d5)
#define HID_USAGE_UPS_SHUTDOWNIMMINENT       (HID_PAGE_UPS | 0x0069)
#define HID_USAGE_UPS_COMMLOST               (HID_PAGE_UPS | 0x0024)

// Battery System Page
#define HID_USAGE_BAT_VOLTAGE                (HID_PAGE_BATTERY | 0x0030)
#define HID_USAGE_BAT_REMCAPACITY            (HID_PAGE_BATTERY | 0x0066)
#define HID_USAGE_BAT_REMCAPLIMIT            (HID_PAGE_BATTERY | 0x0067)
#define HID_USAGE_BAT_CAPACITYMODE           (HID_PAGE_BATTERY | 0x002c)
#define HID_USAGE_BAT_RUNTIMETOEMPTY         (HID_PAGE_BATTERY | 0x0068)
#define HID_USAGE_BAT_DESIGNCAPACITY         (HID_PAGE_BATTERY | 0x0083)
#define HID_USAGE_BAT_FULLCHGCAPACITY        (HID_PAGE_BATTERY | 0x0067)

struct HIDUsageDef {
    uint32_t usage;
    uint8_t report_id;
    uint8_t report_type;
    uint16_t bit_offset;
    uint16_t bit_size;
    bool found;
    int32_t logical_min;
    int32_t logical_max;
    int8_t exponent;
    uint32_t unit;
    String path;
};

#endif // HID_USAGES_H
