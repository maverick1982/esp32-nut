#include "DriverRegistry.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "EcoFlowDriver.h"
#include "LegrandDriver.h"
#include "LiebertDriver.h"
#include "OpenUPSDriver.h"
#include "PowercomDriver.h"
#include <string.h>

namespace {

const uint16_t PHOENIXTEC_VENDORID = 0x06DA;

bool contains(const char* s, const char* what) {
    return s && strstr(s, what) != nullptr;
}

// Phoenixtec 06da:ffff is shared by three subdrivers, tried in the order of usbhid-ups
// subdriver_list: mge-hid.c, idowell-hid.c, liebert-hid.c (US-097)
IUPSDriver* createPhoenixtec(const char* mfr, const char* product) {
    // mge_claim(): AEG PROTECT NAS
    if (contains(mfr, "AEG") || contains(product, "AEG")) return new EatonDriver();
    // idowell_is_goldenmate(): GoldenMate LiFePO4 packs; idowell-hid.c is not ported
    if (contains(mfr, "BMS") || contains(product, "Smart-Battery")) return new GenericDriver();
    // liebert-hid.c is the default sink of the VID
    return new LiebertDriver();
}

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

IUPSDriver* DriverRegistry::create(uint16_t vid, uint16_t pid, const char* mfr, const char* product) {
    // liebert_usb_device_table: other Phoenixtec products stay generic, as upstream
    // rejects them unless the productid option is given
    if (vid == PHOENIXTEC_VENDORID && pid == 0xFFFF) return createPhoenixtec(mfr, product);
    for (const auto& d : DRIVERS) {
        if (d.vid == vid && (d.pid == ANY_PID || d.pid == pid)) return d.create();
    }
    return new GenericDriver();
}
