#include "DriverRegistry.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "EcoFlowDriver.h"
#include "LegrandDriver.h"
#include "OpenUPSDriver.h"
#include "PowercomDriver.h"

namespace {

struct DriverEntry {
    uint16_t vid;
    uint16_t pid; // DriverRegistry::ANY_PID for every product of the vendor
    IUPSDriver* (*create)();
};

template <typename T>
IUPSDriver* make() { return new T(); }

const DriverEntry DRIVERS[] = {
    { 0x051D, DriverRegistry::ANY_PID, make<APCDriver> },        // APC
    { 0x0764, DriverRegistry::ANY_PID, make<CyberPowerDriver> }, // CyberPower
    { 0x0463, DriverRegistry::ANY_PID, make<EatonDriver> },      // Eaton / MGE
    { 0x3746, DriverRegistry::ANY_PID, make<EcoFlowDriver> },    // EcoFlow
    { 0x0D9F, DriverRegistry::ANY_PID, make<PowercomDriver> },   // Powercom
    // legrand_usb_device_table: other Legrand products stay generic (upstream rejects them
    // unless the productid option is given)
    { 0x1CB0, 0x0038, make<LegrandDriver> },                     // Legrand Keor PDU
    { 0x1CB0, 0x0032, make<LegrandDriver> },                     // Legrand Keor SP
    { 0x04D8, 0xD004, make<OpenUPSDriver> },                     // OpenUPS
    { 0x04D8, 0xD005, make<OpenUPSDriver> },                     // OpenUPS2 / WalleCube
};

} // namespace

IUPSDriver* DriverRegistry::create(uint16_t vid, uint16_t pid) {
    for (const auto& d : DRIVERS) {
        if (d.vid == vid && (d.pid == ANY_PID || d.pid == pid)) return d.create();
    }
    return new GenericDriver();
}
