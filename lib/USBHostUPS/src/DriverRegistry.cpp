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

// Explicit HID rows, shared by create() and match(): Phoenixtec 06da:ffff, then DRIVERS[].
// *entry stays nullptr for the Phoenixtec case.
bool findExplicitHid(uint16_t vid, uint16_t pid, const DriverEntry** entry) {
    *entry = nullptr;
    // liebert_usb_device_table: other Phoenixtec products stay generic, as upstream
    // rejects them unless the productid option is given
    if (vid == PHOENIXTEC_VENDORID && pid == 0xFFFF) return true;
    for (const auto& d : DRIVERS) {
        if (d.vid == vid && (d.pid == DriverRegistry::ANY_PID || d.pid == pid)) {
            *entry = &d;
            return true;
        }
    }
    return false;
}

// Row of qx_usb_id[] (nut_repo/drivers/nutdrv_qx.c:2704-2727). The handler of NUT becomes
// the name of its USB subdriver in usbsubdriver[] (nutdrv_qx.c:3376-3390).
struct QxUsbId {
    uint16_t vid;
    uint16_t pid;
    const char* vendor;  // iManufacturer, nullptr = any
    const char* product; // iProduct, nullptr = any
    const char* transport;
};

// Copy of qx_usb_id[] in the order of NUT, with the comment of each row. The QinHeng
// 1a86:7523 row is left out: its handler is NULL (no USB subdriver in NUT, and CH340/341
// chips of non-UPS devices use the same VID:PID), so NUT never claims it with nutdrv_qx.
// The snr row stays although krauler, with NULL strings, comes first and hides it: so it
// is in NUT too.
const QxUsbId QX_USB_ID[] = {
    { 0x05B8, 0x0000, nullptr, nullptr, "cypress" },    // Agiler UPS
    { 0xFFFF, 0x0000, nullptr, nullptr, "ablerex" },    // Ablerex 625L USB (Note: earlier best-fit was "krauler_subdriver" before PR #1135)
    { 0x1CB0, 0x0035, nullptr, nullptr, "krauler" },    // Legrand Daker DK / DK Plus
    { 0x0665, 0x5161, nullptr, nullptr, "cypress" },    // Belkin F6C1200-UNV/Voltronic Power UPSes
    { 0x06DA, 0x0002, "Phoenixtec Power", "USB Cable (V2.00)", "phoenixtec" }, // Masterguard A Series
    { 0x06DA, 0x0002, nullptr, nullptr, "cypress" },    // Online Yunto YQ450
    { 0x06DA, 0x0003, nullptr, nullptr, "ippon" },      // Mustek Powermust
    { 0x06DA, 0x0004, nullptr, nullptr, "cypress" },    // Phoenixtec Innova 3/1 T
    { 0x06DA, 0x0005, nullptr, nullptr, "cypress" },    // Phoenixtec Innova RT
    { 0x06DA, 0x0201, nullptr, nullptr, "cypress" },    // Phoenixtec Innova T
    { 0x06DA, 0x0601, nullptr, nullptr, "phoenix" },    // Online Zinto A
    { 0x0F03, 0x0001, nullptr, nullptr, "cypress" },    // Unitek Alpha 1200Sx
    { 0x14F0, 0x00C9, nullptr, nullptr, "phoenix" },    // GE EP series
    // (1a86:7523 QinHeng CH340, handler NULL in NUT: not copied, see above)
    { 0x0483, 0x0035, nullptr, nullptr, "sgs" },        // TS Shara UPSes; vendor ID 0x0483 is from ST Microelectronics - with product IDs delegated to different OEMs
    { 0x0590, 0x00B7, nullptr, nullptr, "omron" },      // OMRON BN150T
    { 0x0001, 0x0000, "MEC", "MEC0003", "fabula" },     // Fideltronik/MEC LUPUS 500 USB
    { 0x0001, 0x0000, nullptr, "MEC0003", "hunnox" },   // Hunnox HNX 850, reported to also help support Powercool and some other devices; closely related to fabula with tweaks
    { 0x0001, 0x0000, "ATCL FOR UPS", "ATCL FOR UPS", "fuji" }, // Fuji UPSes
    { 0x0001, 0x0000, nullptr, nullptr, "krauler" },    // Krauler UP-M500VA
    { 0x0001, 0x0000, nullptr, "MEC0003", "snr" },      // SNR-UPS-LID-XXXX UPSes
    { 0x0925, 0x1234, nullptr, nullptr, "armac" },      // Armac UPS and maybe other richcomm-like or using old PowerManagerII software
};

// strcasecmp() without the platform header: ASCII only, as the USB strings after toAscii()
bool equalsIgnoreCase(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

// String test of qx_is_usb_device_supported() (nutdrv_qx.c:2755-2765): a nullptr in the
// row matches anything; otherwise a missing device string does not match.
bool qxStringMatches(const char* row, const char* device) {
    if (!row) return true;
    if (!device || !*device) return false;
    return equalsIgnoreCase(row, device);
}

} // namespace

DriverRegistry::Match DriverRegistry::match(uint16_t vid, uint16_t pid, const char* mfr, const char* product) {
    const DriverEntry* entry;
    if (findExplicitHid(vid, pid, &entry)) return { Family::HID, true, nullptr };
    // A device that only GenericDriver would take today, with UPS pages but the VID/PID
    // of a Q* row (#91), now goes to the Q* path: the correction of ADR 0013, not a
    // regression. NUT gives it to nutdrv_qx as well.
    for (const auto& q : QX_USB_ID) {
        if (q.vid != vid || q.pid != pid) continue;
        if (!qxStringMatches(q.vendor, mfr) || !qxStringMatches(q.product, product)) continue;
        return { Family::QX, false, q.transport };
    }
    return { Family::HID, false, nullptr };
}

bool DriverRegistry::hasUpsPages(const std::vector<HIDUsageDef>& usages) {
    for (const auto& u : usages) {
        if ((u.usage & 0xFFFF0000) == HID_PAGE_UPS || (u.usage & 0xFFFF0000) == HID_PAGE_BATTERY) {
            return true;
        }
    }
    return false;
}

IUPSDriver* DriverRegistry::create(uint16_t vid, uint16_t pid, const char* mfr, const char* product) {
    const DriverEntry* entry;
    if (findExplicitHid(vid, pid, &entry)) {
        return entry ? entry->create() : createPhoenixtec(mfr, product);
    }
    return new GenericDriver();
}
