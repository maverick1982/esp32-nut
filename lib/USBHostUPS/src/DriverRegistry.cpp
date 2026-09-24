#include "DriverRegistry.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
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
    { 0x0D9F, DriverRegistry::ANY_PID, make<PowercomDriver> },   // Powercom
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
