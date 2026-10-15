#ifndef NUT_TABLE_H
#define NUT_TABLE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <Arduino.h>
#include "HIDUsages.h"

/**
 * HID -> NUT table of a subdriver, shaped like hid_info_t of usbhid-ups (US-066).
 *
 * This is the future single table of each driver (approach C, decided on 2026-10-15):
 * for now it holds only the ST_FLAG_RW rows of the upstream subdrivers, in their original
 * order, so that the migrations (US-073..US-079) can add the read-only rows, the BOOL
 * status rows and the commands in place. Nothing here assumes that a row is RW except
 * the helpers that build the LIST RW answer.
 *
 * Paths use the notation of HIDUsageDef::path (names from NUTUsages.h, unnamed usages as
 * "0x%08X") plus "[N]" after an indexed collection (HIDUsageDef::coll_index). Each row
 * cites the upstream file, line and path it was copied from.
 */
namespace NutTable {

// Same values as upstream: include/extstate.h and drivers/usbhid-ups.h
constexpr uint16_t ST_FLAG_RW = 0x0001;
constexpr uint16_t ST_FLAG_STRING = 0x0002;
constexpr uint16_t HU_FLAG_STATIC = 2;
constexpr uint16_t HU_FLAG_SEMI_STATIC = 4;
constexpr uint16_t HU_FLAG_ABSENT = 8;
constexpr uint16_t HU_FLAG_QUICK_POLL = 16;
constexpr uint16_t HU_TYPE_CMD = 64;
constexpr uint16_t HU_FLAG_ENUM = 128;
constexpr uint16_t HU_FLAG_PARAM_REQUIRED = 256;

// How a HID value becomes the NUT value (the info_lkp_t of the upstream row)
enum class Conv : uint8_t {
    Number,           // "%.0f"
    Lookup,           // plain info_lkp_t table
    BatteryDate,      // date_conversion (usbhid-ups.c)
    MgeDate,          // mge_date_conversion
    MgeTime,          // mge_time_conversion
    NominalVoltage,   // nominal_output_voltage_info (mge-hid.c)
    PegasusThreshold, // pegasus_threshold_info (mge-hid.c)
    PegasusYesNo      // pegasus_yes_no_info (mge-hid.c)
};

struct Lookup {
    int32_t hid;
    const char* nut;
};

// One row of the table: the fields and their names follow hid_info_t
struct HidInfo {
    const char* info_type;  // NUT variable
    uint16_t info_flags;    // ST_FLAG_*
    uint8_t info_len;       // max length of STRING values
    const char* hidpath;
    const char* dfl;        // format, or the default value of HU_FLAG_ABSENT rows
    uint16_t hidflags;      // HU_FLAG_* / HU_TYPE_CMD
    Conv conv;
    const Lookup* lkp;
    uint8_t lkp_count;
};

#define NUT_LKP(x) x, (uint8_t)(sizeof(x) / sizeof((x)[0]))

// Lookup tables, verbatim from upstream. static, not inline: the native toolchain (GCC 5)
// has no inline variables; the copies per translation unit are identical and tiny.
static constexpr Lookup kYesNo[] = { { 0, "no" }, { 1, "yes" } };                          // yes_no_info
static constexpr Lookup kEnableDisable[] = { { 0, "disabled" }, { 1, "enabled" } };        // eaton_enable_disable_info
static constexpr Lookup kApcSensitivity[] = { { 0, "low" }, { 1, "medium" }, { 2, "high" } }; // apc_sensitivity_info
static constexpr Lookup kCpsSensitivity[] = { { 0, "low" }, { 1, "normal" }, { 2, "high" } }; // cps_sensitivity_info
static constexpr Lookup kMgeSensitivity[] = { { 0, "normal" }, { 1, "high" }, { 2, "low" } }; // mge_sensitivity_info
static constexpr Lookup kNominalOutputVoltage[] = {                                        // nominal_output_voltage_info
    { 200, "200" }, { 208, "208" }, { 220, "220" }, { 230, "230" }, { 240, "240" },
    { 100, "100" }, { 110, "110" }, { 120, "120" }, { 127, "127" },
};
static constexpr Lookup kPegasusThreshold[] = { { 10, "10" }, { 25, "25" }, { 60, "60" } }; // pegasus_threshold_info

// mge-hid.c models_type_t and country_code_t values used by the conversions
constexpr int MGE_DEFAULT_OFFLINE = 0;
constexpr int MGE_PEGASUS = 0x100;
constexpr int MGE_3S = 0x110;
constexpr int MGE_DEFAULT = 0x200;
constexpr int COUNTRY_UNKNOWN = -1;
constexpr int COUNTRY_EUROPE = 0;
constexpr int COUNTRY_EUROPE_208 = 2;

// State the mge conversion functions keep in globals upstream (mge_type, country_code,
// the static "nominal" of nominal_output_voltage_fun). Reset at every driver setup().
struct Context {
    long nominal = -1;
    int mge_type = MGE_DEFAULT;   // upstream default, overridden by mge_model_names
    int country = COUNTRY_UNKNOWN;
    // False until the inputs of the model-dependent conversions are known (upstream has
    // them before the first walk: mge_format_model and device.country come first).
    bool ready = true;
};

enum class DecodeResult { OK, FAIL, DEFER };

// mge_type from iProduct/iModel as get_model_name() (mge-hid.c), restricted to the rows
// whose type is below MGE_DEFAULT: every other row behaves like MGE_DEFAULT for the
// conversions ported here.
inline int mgeModelType(const char* iProduct, const char* iModel) {
    struct Row { const char* product; const char* model; int type; };
    static const Row rows[] = {
        { "ELLIPSE", "300", MGE_DEFAULT_OFFLINE }, { "ELLIPSE", "500", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "650", MGE_DEFAULT_OFFLINE }, { "ELLIPSE", "800", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "1200", MGE_DEFAULT_OFFLINE },
        { "ellipse", "PR500", MGE_DEFAULT_OFFLINE }, { "ellipse", "PR650", MGE_DEFAULT_OFFLINE },
        { "ellipse", "PR800", MGE_DEFAULT_OFFLINE }, { "ellipse", "PR1200", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "600", MGE_DEFAULT_OFFLINE }, { "ELLIPSE", "750", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "1000", MGE_DEFAULT_OFFLINE }, { "ELLIPSE", "1500", MGE_DEFAULT_OFFLINE },
        { "Ellipse MAX", "600", MGE_DEFAULT_OFFLINE }, { "Ellipse MAX", "850", MGE_DEFAULT_OFFLINE },
        { "Ellipse MAX", "1100", MGE_DEFAULT_OFFLINE }, { "Ellipse MAX", "1500", MGE_DEFAULT_OFFLINE },
        { "PROTECTIONCENTER", "420", MGE_DEFAULT_OFFLINE }, { "PROTECTIONCENTER", "500", MGE_DEFAULT_OFFLINE },
        { "PROTECTIONCENTER", "675", MGE_DEFAULT_OFFLINE },
        { "Protection Station", "500", MGE_PEGASUS }, { "Protection Station", "650", MGE_PEGASUS },
        { "Protection Station", "800", MGE_PEGASUS },
        { "Ellipse ECO", "650", MGE_PEGASUS }, { "Ellipse ECO", "800", MGE_PEGASUS },
        { "Ellipse ECO", "1200", MGE_PEGASUS }, { "Ellipse ECO", "1600", MGE_PEGASUS },
        { "3S", "450", MGE_DEFAULT_OFFLINE }, { "3S", "550", MGE_DEFAULT_OFFLINE },
        { "3S", "700", MGE_3S }, { "3S", "750", MGE_3S },
    };
    if (!iProduct || !iModel || !iModel[0]) return MGE_DEFAULT;
    for (const Row& r : rows) {
        if (strcmp(iProduct, r.product) == 0 && strcmp(iModel, r.model) == 0) return r.type;
    }
    return MGE_DEFAULT;
}

// eaton_check_pegasus_fun / pegasus_yes_no_info_fun. Kept literally: the switch is on
// (mge_type & 0xFF00), so MGE_3S (0x110) lands in the MGE_PEGASUS case and its country
// check is never reached, exactly as upstream.
inline bool pegasusValid(const Context& ctx) {
    switch (ctx.mge_type & 0xFF00) {
    case MGE_PEGASUS:
        return true;
    case MGE_3S:
        return ctx.country != COUNTRY_EUROPE;
    default:
        return false;
    }
}

// nominal_output_voltage_fun: the first value fixes the voltage class
inline bool nominalVoltageValid(long value, Context& ctx) {
    if (ctx.nominal < 0) ctx.nominal = value;
    switch (ctx.nominal) {
    case 100: case 110: case 120: case 127:
        return value == 100 || value == 110 || value == 120 || value == 127;
    case 200: case 208:
        if (value == 200 || value == 208) return true;
        if (value == 220 || value == 230 || value == 240) return (ctx.mge_type & 0xFF00) >= MGE_DEFAULT;
        return false;
    case 220: case 230: case 240:
        if (value == 200 || value == 208) {
            // Line-interactive and online also support 200/208 V; COUNTRY_EUROPE_208
            // and the fallthrough both accept them
            return (ctx.mge_type & 0xFF00) >= MGE_DEFAULT;
        }
        return value == 220 || value == 230 || value == 240;
    default:
        return true; // "can't autodetect settable voltages": the value is still published
    }
}

inline String formatRounded(double v) {
    return String((long)lrint(v));
}

// Converts a HID value as ups_infoval_set() does. DEFER: the model-dependent inputs are
// not known yet, retry with the next report.
inline DecodeResult decode(const HidInfo& info, double v, Context& ctx, String& out) {
    char buf[32];
    switch (info.conv) {
    case Conv::Number:
        out = formatRounded(v);
        return DecodeResult::OK;
    case Conv::Lookup:
        for (uint8_t i = 0; i < info.lkp_count; i++) {
            if (info.lkp[i].hid == (long)v) {
                out = info.lkp[i].nut;
                return DecodeResult::OK;
            }
        }
        return DecodeResult::FAIL;
    case Conv::BatteryDate: {
        long value = (long)v;
        if (value == 0) {
            out = "not set";
            return DecodeResult::OK;
        }
        snprintf(buf, sizeof(buf), "%04ld/%02ld/%02ld", 1980 + (value >> 9), (value >> 5) & 0x0f, value & 0x1f);
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::MgeDate:
    case Conv::MgeTime: {
        // Upstream uses localtime_r(); the board has no time zone, so this is UTC
        time_t sec = (time_t)v;
        struct tm* tmv = gmtime(&sec);
        if (!tmv) return DecodeResult::FAIL;
        bool date = info.conv == Conv::MgeDate;
        size_t n = strftime(buf, sizeof(buf), date ? "%Y/%m/%d" : "%H:%M:%S", tmv);
        if (n != (date ? 10u : 8u)) return DecodeResult::FAIL;
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::NominalVoltage:
        if (!ctx.ready) return DecodeResult::DEFER;
        if (!nominalVoltageValid((long)v, ctx)) return DecodeResult::FAIL;
        out = formatRounded(v);
        return DecodeResult::OK;
    case Conv::PegasusThreshold:
        if (!ctx.ready) return DecodeResult::DEFER;
        if (!pegasusValid(ctx)) return DecodeResult::FAIL;
        out = formatRounded(v);
        return DecodeResult::OK;
    case Conv::PegasusYesNo:
        if (!ctx.ready) return DecodeResult::DEFER;
        if (!pegasusValid(ctx)) return DecodeResult::FAIL;
        out = (v == 0) ? "no" : "yes";
        return DecodeResult::OK;
    }
    return DecodeResult::FAIL;
}

// LIST ENUM values of a row: only HU_FLAG_ENUM rows, each lookup value the conversion
// accepts (usbhid-ups.c:2499-2512)
inline std::vector<const char*> enumValues(const HidInfo& info, Context& ctx) {
    std::vector<const char*> out;
    if (!(info.hidflags & HU_FLAG_ENUM) || !(info.info_flags & ST_FLAG_RW)) return out;
    for (uint8_t i = 0; i < info.lkp_count; i++) {
        bool ok = true;
        if (info.conv == Conv::NominalVoltage) ok = nominalVoltageValid(info.lkp[i].hid, ctx);
        else if (info.conv == Conv::PegasusThreshold || info.conv == Conv::PegasusYesNo) ok = pegasusValid(ctx);
        if (ok) out.push_back(info.lkp[i].nut);
    }
    return out;
}

// Case-insensitive equality, as strcasecmp() in upstream lookups
inline bool equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}

inline int compareIgnoreCase(const char* a, const char* b) {
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * True when usage u has exactly the catalog path: same segments as u.path and, for each
 * collection, the same "[N]" index (coll_index N+1) or none (coll_index 0).
 */
inline bool pathMatches(const HIDUsageDef& u, const char* path) {
    if (!path) return false;
    const char* p = path;
    const char* q = u.path;
    size_t depth = 0;
    while (true) {
        const char* pe = strchr(p, '.');
        size_t plen = pe ? (size_t)(pe - p) : strlen(p);
        const char* qe = strchr(q, '.');
        size_t qlen = qe ? (size_t)(qe - q) : strlen(q);
        if (plen != qlen || strncmp(p, q, plen) != 0) return false;

        // An index token right after this segment
        uint8_t want = 0;
        if (pe && pe[1] == '[') {
            const char* close = strchr(pe + 2, ']');
            if (!close) return false;
            want = (uint8_t)(atoi(pe + 2) + 1);
            pe = (close[1] == '.') ? close + 1 : nullptr;
            if (!pe) return false; // an index must be followed by more segments
        }
        bool last_p = (pe == nullptr);
        bool last_q = (qe == nullptr);
        if (last_p != last_q) return false;
        if (last_p) return want == 0; // the usage itself carries no index
        uint8_t have = depth < sizeof(u.coll_index) ? u.coll_index[depth] : 0;
        if (have != want) return false;
        p = pe + 1;
        q = qe + 1;
        depth++;
    }
}

// A table row bound to the attached device, as an hid_info_t with its hiddata
struct Bound {
    const HidInfo* info;
    int idx;            // first usage with the path, in descriptor order, any report type
    int twin_idx;       // same path and report ID, other report type; -1 if none
    String value;       // last converted value
    bool has = false;
    bool deferred = false; // last value arrived before the conversion inputs were known
    enum class First : uint8_t { UNREAD, OK, FAIL } first = First::UNREAD;
    enum class Role : uint8_t { PENDING, ACTIVE, DEAD } role = Role::PENDING;
};

// The upstream INIT rule: per variable, the first row in table order whose first value
// converts wins; later rows with the same name are dropped (item->hiddata = NULL).
inline void resolve(std::vector<Bound>& binds) {
    for (size_t i = 0; i < binds.size(); i++) {
        const char* name = binds[i].info->info_type;
        bool first_of_group = true;
        for (size_t j = 0; j < i; j++) {
            if (strcmp(binds[j].info->info_type, name) == 0) { first_of_group = false; break; }
        }
        if (!first_of_group) continue;
        // An unread row blocks the later ones: they wait until it is read or fails
        bool blocked = false, decided = false;
        for (size_t j = i; j < binds.size(); j++) {
            Bound& b = binds[j];
            if (strcmp(b.info->info_type, name) != 0) continue;
            if (decided || b.first == Bound::First::FAIL) {
                b.role = Bound::Role::DEAD;
            } else if (b.first == Bound::First::UNREAD) {
                b.role = Bound::Role::PENDING;
                blocked = true;
            } else if (blocked) {
                b.role = Bound::Role::PENDING;
            } else {
                b.role = Bound::Role::ACTIVE;
                decided = true;
            }
        }
    }
}

/**
 * Binds the rows of a table to the usages of the attached device. HU_FLAG_ABSENT rows
 * need their path too and take dfl right away, as in hid_ups_walk().
 */
inline std::vector<Bound> bind(const HidInfo* table, size_t n, const std::vector<HIDUsageDef>& usages) {
    std::vector<Bound> binds;
    if (!table) return binds;
    for (size_t r = 0; r < n; r++) {
        int idx = -1;
        for (size_t u = 0; u < usages.size(); u++) {
            if (pathMatches(usages[u], table[r].hidpath)) { idx = (int)u; break; }
        }
        if (idx < 0) continue;
        int twin = -1;
        const HIDUsageDef& a = usages[idx];
        for (size_t u = 0; u < usages.size(); u++) {
            const HIDUsageDef& b = usages[u];
            if ((int)u != idx && b.report_id == a.report_id && b.report_type != a.report_type &&
                strcmp(b.path, a.path) == 0 && memcmp(b.coll_index, a.coll_index, sizeof(a.coll_index)) == 0) {
                twin = (int)u;
                break;
            }
        }
        Bound b;
        b.info = &table[r];
        b.idx = idx;
        b.twin_idx = twin;
        if (table[r].hidflags & HU_FLAG_ABSENT) {
            b.value = table[r].dfl;
            b.has = true;
            b.first = Bound::First::OK;
        }
        binds.push_back(b);
    }
    resolve(binds);
    return binds;
}

// A value read for binds[i]
inline void onValue(std::vector<Bound>& binds, size_t i, double v, Context& ctx) {
    Bound& b = binds[i];
    if (b.info->hidflags & HU_FLAG_ABSENT) return; // driver-side value, never read
    if (b.role == Bound::Role::DEAD) return;
    String out;
    DecodeResult r = decode(*b.info, v, ctx, out);
    b.deferred = (r == DecodeResult::DEFER);
    if (b.deferred) return;
    if (r == DecodeResult::OK) {
        b.value = out;
        b.has = true;
        if (b.first == Bound::First::UNREAD) b.first = Bound::First::OK;
    } else if (b.first == Bound::First::UNREAD) {
        // As ups_infoval_set() failing during INIT; later failures keep the old value
        b.first = Bound::First::FAIL;
    }
    resolve(binds);
}

// End of the first full poll, the end of upstream's INIT walk: rows never read count as
// failed, so the next row with the same name can take over. A deferred row was read and
// waits only for its conversion inputs: it is decided by its next value.
inline void finishInit(std::vector<Bound>& binds) {
    bool changed = false;
    for (Bound& b : binds) {
        if (b.first == Bound::First::UNREAD && !b.deferred) {
            b.first = Bound::First::FAIL;
            changed = true;
        }
    }
    if (changed) resolve(binds);
}

// A writable variable as the NUT server reports it
struct RWVariable {
    const char* name;
    uint8_t aux;
    std::vector<const char*> enums;
};

// Paths of the HU_FLAG_ABSENT ups.delay.shutdown / ups.delay.start rows of a subdriver,
// in the notation of this table. The variables exist when one of the paths is in the
// descriptor, whatever the report type (CommandCatalog::applyDefaultDelays).
struct DelayPaths {
    const char* const* shutdown;
    size_t n_shutdown;
    const char* const* startup;
    size_t n_startup;
};

inline bool hasAnyPath(const std::vector<HIDUsageDef>& usages, const char* const* paths, size_t n) {
    for (size_t i = 0; i < n; i++) {
        for (const auto& u : usages) {
            if (pathMatches(u, paths[i])) return true;
        }
    }
    return false;
}

// mge-hid.c:2066-2067
inline const DelayPaths* mgeDelayPaths() {
    static const char* const sd[] = { "UPS.PowerSummary.DelayBeforeShutdown" };
    static const char* const st[] = { "UPS.PowerSummary.DelayBeforeStartup" };
    static const DelayPaths p = { sd, 1, st, 1 };
    return &p;
}

// apc-hid.c:355-368: PowerSummary, Output, APCGeneralCollection (0xff860005) with
// APCDelayBeforeShutdown (0xff86007d) / APCDelayBeforeStartup (0xff86007e)
inline const DelayPaths* apcDelayPaths() {
    static const char* const sd[] = { "UPS.PowerSummary.DelayBeforeShutdown", "UPS.Output.DelayBeforeShutdown",
                                      "UPS.0xFF860005.0xFF86007D" };
    static const char* const st[] = { "UPS.PowerSummary.DelayBeforeStartup", "UPS.Output.DelayBeforeStartup",
                                      "UPS.0xFF860005.0xFF86007E" };
    static const DelayPaths p = { sd, 3, st, 3 };
    return &p;
}

// cps-hid.c:386-387
inline const DelayPaths* cpsDelayPaths() {
    static const char* const sd[] = { "UPS.Output.DelayBeforeShutdown" };
    static const char* const st[] = { "UPS.Output.DelayBeforeStartup" };
    static const DelayPaths p = { sd, 1, st, 1 };
    return &p;
}

// powercom-hid.c:620-639: PCMDelayBeforeShutdown is 0x00ff0057 in powercom_usage_lkp
inline const DelayPaths* powercomDelayPaths() {
    static const char* const sd[] = { "UPS.PowerSummary.DelayBeforeShutdown", "UPS.PowerSummary.0x00FF0057" };
    static const char* const st[] = { "UPS.PowerSummary.DelayBeforeStartup" };
    static const DelayPaths p = { sd, 2, st, 1 };
    return &p;
}

// Subdriver tables. Partial on purpose (US-066): only the ST_FLAG_RW rows, in upstream
// order. ups.delay.* is left to CommandCatalog::applyDefaultDelays.

inline const HidInfo* mgeHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // mge-hid.c:2013 UPS.PowerSummary.RemainingCapacityLimitSetting
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerSummary.0xFFFF004D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2015 UPS.PowerSummary.RestartLevel
        { "battery.charge.restart", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.PowerSummary.0xFFFF005F", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2018 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2020 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2027 UPS.BatterySystem.Battery.DeepDischargeProtection
        { "battery.protection", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.BatterySystem.Battery.0xFFFF0049", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2028 UPS.PowerConverter.Input.[3].EnergySaving
        { "battery.energysave", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].0xFFFF0046", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2029 UPS.PowerConverter.Input.[3].ConfigPercentLoad
        { "battery.energysave.load", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].ConfigPercentLoad", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2031 UPS.PowerConverter.Input.[3].EnergySaving.ShutdownTimer
        { "battery.energysave.delay", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].0xFFFF0046.0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2033 UPS.PowerConverter.Input.[3].ShutdownTimer
        { "battery.energysave.delay", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2034 UPS.PowerConverter.Input.[3].ConfigActivePower
        { "battery.energysave.realpower", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].ConfigActivePower", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2065 UPS.Flow.[4].ConfigPercentLoad
        { "ups.load.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.Flow.[4].ConfigPercentLoad", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // ups.delay.start (mge-hid.c:2066) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (mge-hid.c:2067) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // mge-hid.c:2072 UPS.BatterySystem.Battery.TestPeriod
        { "ups.test.interval", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.BatterySystem.Battery.0xFFFF0045", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2092 UPS.PowerConverter.Input.[1].AutomaticRestart
        { "ups.start.auto", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[1].0xFFFF0043", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2093 UPS.PowerConverter.Input.[3].StartOnBattery
        { "ups.start.battery", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Input.[3].0xFFFF0047", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2094 UPS.PowerConverter.Output.ForcedReboot
        { "ups.start.reboot", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0044", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2095 UPS.PowerSummary.PresentStatus.Switchable
        { "ups.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.PresentStatus.Switchable", "%s", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, Conv::Lookup, NUT_LKP(kEnableDisable) },
        // mge-hid.c:2097 UPS.PowerSummary.Time
        { "ups.date", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.0xFFFF0097", "%s", 0, Conv::MgeDate, nullptr, 0 },
        // mge-hid.c:2098 UPS.PowerSummary.Time
        { "ups.time", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.0xFFFF0097", "%s", 0, Conv::MgeTime, nullptr, 0 },
        // mge-hid.c:2179 UPS.PowerConverter.Output.LowVoltageTransfer
        { "input.transfer.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.LowVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2180 UPS.PowerConverter.Output.LowVoltageEcoTransfer
        { "input.transfer.eco.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF005A", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2181 UPS.PowerConverter.Output.LowVoltageBypassTransfer
        { "input.transfer.bypass.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0057", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2182 UPS.PowerConverter.Output.LowVoltageBoostTransfer
        { "input.transfer.boost.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0050", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2183 UPS.PowerConverter.Output.HighVoltageBoostTransfer
        { "input.transfer.boost.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0051", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2184 UPS.PowerConverter.Output.LowVoltageBuckTransfer
        { "input.transfer.trim.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0052", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2186 UPS.PowerConverter.Output.HighVoltageTransfer
        { "input.transfer.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.HighVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2187 UPS.PowerConverter.Output.HighVoltageEcoTransfer
        { "input.transfer.eco.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF005B", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2188 UPS.PowerConverter.Output.HighVoltageBypassTransfer
        { "input.transfer.bypass.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0058", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2189 UPS.PowerConverter.Output.FrequencyRangeBypassTransfer
        { "input.transfer.frequency.bypass.range", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0059", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2190 UPS.PowerConverter.Output.FrequencyRangeEcoTransfer
        { "input.transfer.frequency.eco.range", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF005C", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2191 UPS.PowerConverter.Output.HysteresisVoltageTransfer
        { "input.transfer.hysteresis", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF009C", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2192 UPS.PowerConverter.Input.[2].ForcedTransferEnable
        { "input.transfer.bypass.forced", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.PowerConverter.Input.[2].0xFFFF0056", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kEnableDisable) },
        // mge-hid.c:2193 UPS.PowerConverter.Input.[2].OverloadTransferEnable
        { "input.transfer.bypass.overload", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.PowerConverter.Input.[2].0xFFFF0054", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kEnableDisable) },
        // mge-hid.c:2194 UPS.PowerConverter.Input.[2].OutOfToleranceTransferEnable
        { "input.transfer.bypass.outlimits", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.PowerConverter.Input.[2].0xFFFF0055", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kEnableDisable) },
        // mge-hid.c:2195 UPS.PowerConverter.Output.HighVoltageBuckTransfer
        { "input.transfer.trim.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF0053", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2196 UPS.PowerConverter.Output.SensitivityMode
        { "input.sensitivity", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerConverter.Output.0xFFFF004C", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kMgeSensitivity) },
        // mge-hid.c:2197 UPS.PowerConverter.Output.ExtendedVoltageMode
        { "input.voltage.extended", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF004B", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2198 UPS.PowerConverter.Output.ExtendedFrequencyMode
        { "input.frequency.extended", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerConverter.Output.0xFFFF004E", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // input.bypass.switch.on (mge-hid.c:2218) excluded: stateful mge function, US-070
        // input.bypass.switch.off (mge-hid.c:2219) excluded: stateful mge function, US-070
        // mge-hid.c:2222 UPS.PowerConverter.Input.[2].Switchable
        { "input.bypass.switchable", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.PowerConverter.Input.[2].Switchable", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kEnableDisable) },
        // mge-hid.c:2232 UPS.Flow.[4].ConfigVoltage
        { "output.voltage.nominal", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.Flow.[4].ConfigVoltage", "%.0f", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, Conv::NominalVoltage, NUT_LKP(kNominalOutputVoltage) },
        // mge-hid.c:2247 UPS.OutletSystem.Outlet.[1].OutletID
        { "outlet.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[1].OutletID", "Main Outlet", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2252 UPS.OutletSystem.Outlet.[1].ConfigApparentPower
        { "outlet.power", ST_FLAG_RW | ST_FLAG_STRING, 6, "UPS.OutletSystem.Outlet.[1].ConfigApparentPower", "%s", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, Conv::PegasusThreshold, NUT_LKP(kPegasusThreshold) },
        // mge-hid.c:2261 UPS.OutletSystem.Outlet.[2].OutletID
        { "outlet.1.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[2].OutletID", "PowerShare Outlet 1", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2270 UPS.OutletSystem.Outlet.[2].RemainingCapacityLimit
        { "outlet.1.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[2].RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2271 UPS.OutletSystem.Outlet.[2].ShutdownTimer
        { "outlet.1.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[2].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2272 UPS.OutletSystem.Outlet.[2].StartupTimer
        { "outlet.1.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[2].0xFFFF005E", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2282 UPS.OutletSystem.Outlet.[3].OutletID
        { "outlet.2.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[3].OutletID", "PowerShare Outlet 2", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2285 UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable
        { "outlet.2.switchable", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable", "%s", HU_FLAG_SEMI_STATIC, Conv::PegasusYesNo, NUT_LKP(kYesNo) },
        // mge-hid.c:2291 UPS.OutletSystem.Outlet.[3].RemainingCapacityLimit
        { "outlet.2.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[3].RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2292 UPS.OutletSystem.Outlet.[3].ShutdownTimer
        { "outlet.2.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[3].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2293 UPS.OutletSystem.Outlet.[3].StartupTimer
        { "outlet.2.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[3].0xFFFF005E", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2303 UPS.OutletSystem.Outlet.[4].OutletID
        { "outlet.3.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[4].OutletID", "PowerShare Outlet 2", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2306 UPS.OutletSystem.Outlet.[4].PresentStatus.Switchable
        { "outlet.3.switchable", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[4].PresentStatus.Switchable", "%s", HU_FLAG_SEMI_STATIC, Conv::PegasusYesNo, NUT_LKP(kYesNo) },
        // mge-hid.c:2312 UPS.OutletSystem.Outlet.[4].RemainingCapacityLimit
        { "outlet.3.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[4].RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2313 UPS.OutletSystem.Outlet.[4].ShutdownTimer
        { "outlet.3.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[4].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2314 UPS.OutletSystem.Outlet.[4].StartupTimer
        { "outlet.3.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[4].0xFFFF005E", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // input.eco.switchable (mge-hid.c:2373) excluded: stateful mge function, US-070
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

inline const HidInfo* apcHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // apc-hid.c:335 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:339 UPS.Battery.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Battery.RemainingTimeLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:340 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:347 UPS.Battery.ManufacturerDate
        { "battery.mfr.date", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Battery.ManufacturerDate", "%s", HU_FLAG_SEMI_STATIC, Conv::BatteryDate, nullptr, 0 },
        // ups.delay.start (apc-hid.c:355) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (apc-hid.c:356) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.start (apc-hid.c:361) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (apc-hid.c:362) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.start (apc-hid.c:367) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (apc-hid.c:368) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // apc-hid.c:409 UPS.Output.LowVoltageTransfer
        { "input.transfer.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Output.LowVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:410 UPS.Output.HighVoltageTransfer
        { "input.transfer.high", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Output.HighVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:412 UPS.Input.LowVoltageTransfer
        { "input.transfer.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Input.LowVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:413 UPS.Input.HighVoltageTransfer
        { "input.transfer.high", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Input.HighVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:414 UPS.Input.APCSensitivity
        { "input.sensitivity", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Input.0xFF860061", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kApcSensitivity) },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

inline const HidInfo* cpsHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // cps-hid.c:367 UPS.Battery.ManufacturerDate
        { "battery.mfr.date", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Battery.ManufacturerDate", "%s", HU_FLAG_SEMI_STATIC, Conv::BatteryDate, nullptr, 0 },
        // cps-hid.c:370 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:373 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // ups.delay.start (cps-hid.c:386) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (cps-hid.c:387) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // cps-hid.c:411 UPS.Input.LowVoltageTransfer
        { "input.transfer.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Input.LowVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:412 UPS.Input.HighVoltageTransfer
        { "input.transfer.high", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Input.HighVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:414 UPS.Output.LowVoltageTransfer
        { "input.transfer.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Output.LowVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:415 UPS.Output.HighVoltageTransfer
        { "input.transfer.high", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Output.HighVoltageTransfer", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:416 UPS.Output.CPSInputSensitivity
        { "input.sensitivity", ST_FLAG_RW | ST_FLAG_STRING, 0, "UPS.Output.0xFF010043", "%s", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, Conv::Lookup, NUT_LKP(kCpsSensitivity) },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

inline const HidInfo* ecoflowHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // ecoflow-hid.c:181 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

// VARDESC lines of nut_repo/data/cmdvartab for the variables of the tables, matched
// case-insensitively as desc.c:list_get(). nullptr when cmdvartab has none.
inline const char* describe(const char* name) {
    struct Desc { const char* name; const char* desc; };
    static const Desc descs[] = {
        { "battery.charge.low", "Remaining battery level when UPS switches to LB (percent)" },
        { "battery.charge.restart", "Minimum battery level for restart after power off (percent)" },
        { "battery.energysave", "Switch off when running on battery and no/low load" },
        { "battery.energysave.delay", "Delay before switch off UPS if on battery and load level low (min)" },
        { "battery.energysave.load", "Switch off UPS if on battery and load level lower (percent)" },
        { "battery.energysave.realpower", "Switch off UPS if on battery and load level lower (Watts)" },
        { "battery.mfr.date", "Battery manufacturing date" },
        { "battery.protection", "Prevent deep discharge of battery" },
        { "battery.runtime.low", "Remaining battery runtime when UPS switches to LB (seconds)" },
        { "input.bypass.switchable", "Input auto transfer on Bypass when overload or out of tolerance (enabled or disabled)" },
        { "input.frequency.extended", "Extended input frequency range" },
        { "input.sensitivity", "Input power sensitivity" },
        { "input.transfer.boost.high", "High voltage boosting transfer point (V)" },
        { "input.transfer.boost.low", "Low voltage boosting transfer point (V)" },
        { "input.transfer.bypass.forced", "Rule for allow auto Bypass switch (on/off) transfer modes (enabled or disabled)" },
        { "input.transfer.bypass.high", "High voltage Bypass transfer point (V)" },
        { "input.transfer.bypass.low", "Low voltage Bypass transfer point (V)" },
        { "input.transfer.bypass.outlimits", "Rule for auto transfer on Bypass when out of tolerance (enabled or disabled)" },
        { "input.transfer.bypass.overload", "Rule for auto transfer on Bypass when overload (enabled or disabled)" },
        { "input.transfer.eco.high", "High voltage ECO transfer point (V)" },
        { "input.transfer.eco.low", "Low voltage ECO transfer point (V)" },
        { "input.transfer.frequency.bypass.range", "Frequency range Bypass transfer point (percent of nominal Hz)" },
        { "input.transfer.frequency.eco.range", "Frequency range ECO transfer point (percent of nominal Hz)" },
        { "input.transfer.high", "High voltage transfer point (V)" },
        { "input.transfer.hysteresis", "Threshold of switching protection modes, voltage transfer point (V)" },
        { "input.transfer.low", "Low voltage transfer point (V)" },
        { "input.transfer.trim.high", "High voltage trimming transfer point (V)" },
        { "input.transfer.trim.low", "Low voltage trimming transfer point (V)" },
        { "input.voltage.extended", "Extended input voltage range" },
        { "outlet.1.autoswitch.charge.low", "Remaining battery level to power off this outlet (percent)" },
        { "outlet.1.delay.shutdown", "Interval to wait before shutting down this outlet (seconds)" },
        { "outlet.1.delay.start", "Interval to wait before restarting this outlet (seconds)" },
        { "outlet.1.desc", "Outlet description" },
        { "outlet.2.autoswitch.charge.low", "Remaining battery level to power off this outlet (percent)" },
        { "outlet.2.delay.shutdown", "Interval to wait before shutting down this outlet (seconds)" },
        { "outlet.2.delay.start", "Interval to wait before restarting this outlet (seconds)" },
        { "outlet.2.desc", "Outlet description" },
        { "outlet.2.switchable", "Outlet switch ability" },
        { "outlet.3.autoswitch.charge.low", nullptr /* not in cmdvartab */ },
        { "outlet.3.delay.shutdown", nullptr /* not in cmdvartab */ },
        { "outlet.3.delay.start", nullptr /* not in cmdvartab */ },
        { "outlet.3.desc", nullptr /* not in cmdvartab */ },
        { "outlet.3.switchable", nullptr /* not in cmdvartab */ },
        { "outlet.desc", "Outlet description" },
        { "outlet.power", nullptr /* not in cmdvartab */ },
        { "output.voltage.nominal", "Nominal output voltage (V)" },
        { "ups.date", "Internal UPS clock date" },
        { "ups.delay.shutdown", "Interval to wait after shutdown with delay command (seconds)" },
        { "ups.delay.start", "Interval to wait before (re)starting the load (seconds)" },
        { "ups.load.high", "Load when UPS switches to overload condition (percent)" },
        { "ups.shutdown", "Enable or disable UPS shutdown ability (poweroff)" },
        { "ups.start.auto", "UPS starts when mains is (re)applied" },
        { "ups.start.battery", "Allow to start UPS from battery" },
        { "ups.start.reboot", "UPS reboots when power returns during shutdown delay" },
        { "ups.test.interval", "Interval between self tests (seconds)" },
        { "ups.time", "Internal UPS clock time" },
    };
    if (!name) return nullptr;
    for (const Desc& d : descs) {
        if (equalsIgnoreCase(d.name, name)) return d.desc;
    }
    return nullptr;
}

} // namespace NutTable

#endif // NUT_TABLE_H
