/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/usbhid-ups.c:
 *   Copyright (C) 2003-2022 Arnaud Quette <arnaud.quette@gmail.com>
 *                 2005 John Stamp <kinsayder@hotmail.com>
 *                 2005-2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2007-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2016 Eaton / Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2020-2026 Jim Klimov <jimklimov+nut@gmail.com>
 *
 * drivers/mge-hid.c:
 *   Copyright (C) 2003 - 2015 Arnaud Quette <arnaud.quette@free.fr>
 *                 2015 - 2024 Eaton / Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2020 - 2026 Jim Klimov <jimklimov+nut@gmail.com>
 *                 2024 - 2025 "DaRK AnGeL" <28630321+masterwishx@users.noreply.github.com>
 *
 * drivers/apc-hid.c:
 *   Copyright (C) 2003 - 2009 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 John Stamp <kinsayder@hotmail.com>
 *                 2005 Peter Selinger <selinger@users.sourceforge.net>
 *                 2009 - 2010 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *
 * drivers/cps-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2020 - 2025 Jim Klimov <jimklimov+nut@gmail.com>
 *                 2024 Alejandro González <me@alegon.dev>
 *
 * drivers/ecoflow-hid.c:
 *   Copyright (C) 2003 - 2012 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2013 Charles Lepple <clepple+nut@gmail.com>
 *                 2024 Yevgeniy Kuksenko <2882631+ykuksenko@users.noreply.github.com>
 *                 2025 M. Greyson Christoforo <grey@christoforo.net>
 *
 * drivers/powercom-hid.c:
 *   Copyright (C) 2003 - 2009 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2020 - 2024 Jim Klimov <jimklimov+nut@gmail.com>
 *
 * drivers/legrand-hid.c:
 *   Copyright (C) 2003 - 2012 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2013 Charles Lepple <clepple+nut@gmail.com>
 *                 2018 Gabriele Taormina <gabriele.taormina@legrand.com>, for Legrand
 *
 * drivers/liebert-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2007 Charles Lepple <clepple@gmail.com>
 *                 2018 Markus "Links2004" <help.markus+gitk@gmail.com>
 *                 2023 Blaž Zakrajšek <zakrajsek.blaz@gmail.com>
 */

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
#include "UPSData.h"
#include "UpsVariables.h"

/**
 * HID -> NUT table of a subdriver, shaped like hid_info_t of usbhid-ups (US-066).
 *
 * This is the single table of each driver (approach C, ADR 0011), in the original upstream
 * order. The engine handles writable, read-only, BOOL status and HU_FLAG_ABSENT rows; the
 * migrations (US-073..US-079, US-106) fill the tables one subdriver at a time, so most
 * tables still hold only their ST_FLAG_RW rows (US-066). Nothing here assumes that a row
 * is RW except the helpers that build the LIST RW answer.
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
    Number,           // no info_lkp_t: the row's format, "%.0f", "%.1f" or "%.2f"
    Lookup,           // plain info_lkp_t table
    BatteryDate,      // date_conversion (usbhid-ups.c)
    MgeDate,          // mge_date_conversion
    MgeTime,          // mge_time_conversion
    NominalVoltage,   // nominal_output_voltage_info (mge-hid.c)
    PegasusThreshold, // pegasus_threshold_info (mge-hid.c)
    PegasusYesNo,     // pegasus_yes_no_info (mge-hid.c)
    StringId,         // stringid_conversion (usbhid-ups.c): the value is a string index
    KelvinCelsius,    // kelvin_celsius_conversion (usbhid-ups.c)
    MgeBatteryCapacity,       // mge_battery_capacity (mge-hid.c)
    MgeBatteryVoltage,        // mge_battery_voltage (mge-hid.c)
    MgeBatteryVoltageNominal, // mge_battery_voltage_nominal (mge-hid.c)
    // Internal rows (US-073): they keep state, never publish their info_type
    MgeCountry,       // eaton_check_country_info (mge-hid.c)
    AbmEnabled,       // eaton_abm_enabled_info (mge-hid.c)
    AbmPathMode,      // eaton_abm_path_mode_info (mge-hid.c)
    AbmPathStatus,    // eaton_abm_path_status_info (mge-hid.c)
    AbmStatus,        // eaton_abm_status_info (mge-hid.c), battery.charger.status
    // Functions of BOOL rows (US-073): they give a status token or nothing
    AbmChrgDischrg,       // eaton_abm_chrg_dischrg_info (mge-hid.c)
    EatonDischarging,     // eaton_discharging_info (mge-hid.c)
    EatonCharging,        // eaton_charging_info (mge-hid.c)
    EatonConverterOnline, // eaton_converter_online_info (mge-hid.c)
    // US-074
    MgePowerFactor,       // mge_powerfactor_conversion (mge-hid.c)
    EatonComputeRealpower, // eaton_compute_realpower_info (mge-hid.c)
    // US-075
    CpsBattCharge,        // cps_battcharge (cps-hid.c)
    CpsBattStatus,        // cps_battstatus (cps-hid.c)
    CpsBattVolt,          // cps_battvolt (cps-hid.c)
    CpsInputFreq,         // cps_input_freq (cps-hid.c)
    CpsOutputFreq,        // cps_output_freq (cps-hid.c)
    // US-076
    ApcDate,              // apc_date_conversion (apc-hid.c)
    // US-077
    PowercomHackVoltage,  // powercom_hack_voltage_lkp (powercom-hid.c): the last valid report 0xA4
    PowercomVoltage,      // powercom_voltage_conversion (powercom-hid.c)
    PowercomStartup,      // powercom_startup_info (powercom-hid.c)
    PowercomShutdown,     // powercom_shutdown_info (powercom-hid.c)
    PowercomUpsfail,      // powercom_upsfail_conversion (powercom-hid.c)
    PowercomReplacebatt,  // powercom_replacebatt_conversion (powercom-hid.c)
    PowercomTest,         // powercom_test_conversion (powercom-hid.c)
    PowercomShutdownimm,  // powercom_shutdownimm_conversion (powercom-hid.c)
    PowercomOnline,       // powercom_online_conversion (powercom-hid.c)
    PowercomLowbatt,      // powercom_lowbatt_conversion (powercom-hid.c)
    PowercomTrim,         // powercom_trim_conversion (powercom-hid.c)
    PowercomBoost,        // powercom_boost_conversion (powercom-hid.c)
    PowercomOverload      // powercom_overload_conversion (powercom-hid.c)
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
static constexpr Lookup kTestRead[] = {                                                    // test_read_info
    { 1, "Done and passed" }, { 2, "Done and warning" }, { 3, "Done and error" }, { 4, "Aborted" },
    { 5, "In progress" }, { 6, "No test initiated" }, { 7, "Test scheduled" },
};
static constexpr Lookup kEatonChargerType[] = {                                            // eaton_charger_type_info
    { 0, "None" }, { 1, "Extended (CLA)" }, { 2, "Large extension" }, { 3, "Extra large extension (XL)" },
    { 4, "ABM" }, { 5, "Constant Charge (CC)" },
};

// mge-hid.c models_type_t and country_code_t values used by the conversions
constexpr int MGE_DEFAULT_OFFLINE = 0;
constexpr int MGE_PEGASUS = 0x100;
constexpr int MGE_3S = 0x110;
constexpr int MGE_DEFAULT = 0x200;
constexpr int MGE_EVOLUTION = 0x300;
constexpr int MGE_EVOLUTION_650 = 0x301;
constexpr int MGE_EVOLUTION_850 = 0x302;
constexpr int MGE_EVOLUTION_1150 = 0x303;
constexpr int MGE_EVOLUTION_S_1250 = 0x304;
constexpr int MGE_EVOLUTION_1550 = 0x305;
constexpr int MGE_EVOLUTION_S_1750 = 0x306;
constexpr int MGE_EVOLUTION_2000 = 0x307;
constexpr int MGE_EVOLUTION_S_2500 = 0x308;
constexpr int MGE_EVOLUTION_S_3000 = 0x309;
constexpr int MGE_PULSAR_M = 0x400;
constexpr int MGE_PULSAR_M_2200 = 0x401;
constexpr int MGE_PULSAR_M_3000 = 0x402;
constexpr int MGE_PULSAR_M_3000_XL = 0x403;
constexpr int EATON_5P = 0x500;
constexpr int EATON_9E = 0x900;
constexpr int COUNTRY_UNKNOWN = -1;
constexpr int COUNTRY_EUROPE = 0;
constexpr int COUNTRY_EUROPE_208 = 2;

// mge-hid.c advanced battery monitoring flags
constexpr int ABM_UNKNOWN = -1;
constexpr int ABM_DISABLED = 0;
constexpr int ABM_ENABLED = 1;
constexpr int ABM_PATH_UNKNOWN = -1;
constexpr int ABM_PATH_STATUS = 0;
constexpr int ABM_PATH_MODE = 1;

/**
 * The ups_status bits of usbhid-ups (status_bit_t, usbhid-ups.h), collected by the BOOL
 * rows (US-073). known marks the bits a BOOL row has set or cleared at least once: only
 * those become internal ups.status.* keys (statusKeys()).
 */
struct StatusBits {
    uint32_t set = 0;
    uint32_t known = 0;
};

// status_info of usbhid-ups.c: the token of each bit, bit number = index
static constexpr const char* kStatusTokens[] = {
    "online", "offline", "dischrg", "chrg", "lowbatt", "overload", "replacebatt", "shutdownimm",
    "trim", "boost", "bypassauto", "bypassman", "ecomode", "essmode", "off", "cal",
    "overheat", "commfault", "depleted", "timelimitexp", "fullycharged", "notfullycharged",
    "awaitingpower", "fanfail", "nobattery", "battvoltlo", "battvolthi", "chargerfail",
    "vrange", "frange",
};
constexpr int ST_ONLINE = 0;
constexpr int ST_OFFLINE = 1;
constexpr int ST_OFF = 14;
constexpr int ST_FULLYCHARGED = 20;
constexpr int ST_NOTFULLYCHARGED = 21;
constexpr uint32_t statusBit(int b) { return (uint32_t)1 << b; }

// process_boolean_info() (usbhid-ups.c:2030), literally: online and offline, fullycharged
// and notfullycharged neuter each other, "!" clears, unknown tokens are ignored
inline void applyStatusToken(StatusBits& s, const char* token) {
    if (!token) return;
    if (strcmp(token, "online") == 0) applyStatusToken(s, "!offline");
    else if (strcmp(token, "offline") == 0) applyStatusToken(s, "!online");
    if (strcmp(token, "fullycharged") == 0) applyStatusToken(s, "!notfullycharged");
    else if (strcmp(token, "notfullycharged") == 0) applyStatusToken(s, "!fullycharged");

    bool clear = false;
    if (*token == '!') {
        token++;
        clear = true;
    }
    for (size_t b = 0; b < sizeof(kStatusTokens) / sizeof(kStatusTokens[0]); b++) {
        if (!equalsIgnoreCase(kStatusTokens[b], token)) continue;
        if (clear) s.set &= ~statusBit((int)b);
        else s.set |= statusBit((int)b);
        s.known |= statusBit((int)b);
        return;
    }
}

/**
 * The internal ups.status.* keys read by UPSData::computeUPSStatusString(), from the bits
 * known so far: f(key, value) with value nullptr when the key must not exist.
 * online/offline make ups.status.ac_present ("1", "0", or no key when neither is set: no
 * power state, as ups_status_set() sees it), fullycharged/notfullycharged make
 * ups.status.fully_charged; the other bits keep the names the generic mappings already use,
 * or ups.status.<token> for the bits no ups.status token is rendered from yet.
 */
template <typename F>
inline void statusKeys(const StatusBits& s, F f) {
    static const char* const keys[] = {
        nullptr, nullptr, "ups.status.discharging", "ups.status.charging", "ups.status.battery_low",
        "ups.status.overload", "ups.status.replace_battery", "ups.status.shutdown_imminent",
        "ups.status.trim", "ups.status.boost", "ups.status.bypassauto", "ups.status.bypassman",
        "ups.status.ecomode", "ups.status.essmode", "ups.status.off", "ups.status.cal",
        "ups.status.overheat", "ups.status.commfault", "ups.status.depleted",
        "ups.status.remaining_time_limit_expired", nullptr, nullptr, "ups.status.awaitingpower",
        "ups.status.fanfail", "ups.status.no_battery", "ups.status.battvoltlo", "ups.status.battvolthi",
        "ups.status.chargerfail", "ups.status.vrange", "ups.status.frange",
    };
    static_assert(sizeof(keys) / sizeof(keys[0]) == sizeof(kStatusTokens) / sizeof(kStatusTokens[0]),
                  "one key per status bit");
    const uint32_t power = statusBit(ST_ONLINE) | statusBit(ST_OFFLINE);
    if (s.known & power) {
        f("ups.status.ac_present", (s.set & statusBit(ST_ONLINE)) ? "1" : (s.set & statusBit(ST_OFFLINE)) ? "0" : nullptr);
    }
    const uint32_t full = statusBit(ST_FULLYCHARGED) | statusBit(ST_NOTFULLYCHARGED);
    if (s.known & full) {
        f("ups.status.fully_charged",
          (s.set & statusBit(ST_FULLYCHARGED)) ? "1" : (s.set & statusBit(ST_NOTFULLYCHARGED)) ? "0" : nullptr);
    }
    for (size_t b = 0; b < sizeof(keys) / sizeof(keys[0]); b++) {
        if (!keys[b] || !(s.known & statusBit((int)b))) continue;
        f(keys[b], (s.set & statusBit((int)b)) ? "1" : "0");
    }
}

// Status lookups of usbhid-ups.c and mge-hid.c, verbatim: the text is the token
static constexpr Lookup kOnlineInfo[] = { { 1, "online" }, { 0, "offline" } };               // online_info
static constexpr Lookup kDischargingInfo[] = { { 1, "dischrg" }, { 0, "!dischrg" } };        // discharging_info
static constexpr Lookup kChargingInfo[] = { { 1, "chrg" }, { 0, "!chrg" } };                 // charging_info
static constexpr Lookup kLowbattInfo[] = { { 1, "lowbatt" }, { 0, "!lowbatt" } };            // lowbatt_info
static constexpr Lookup kOverloadInfo[] = { { 1, "overload" }, { 0, "!overload" } };         // overload_info
static constexpr Lookup kReplacebattInfo[] = { { 1, "replacebatt" }, { 0, "!replacebatt" } }; // replacebatt_info
static constexpr Lookup kTrimInfo[] = { { 1, "trim" }, { 0, "!trim" } };                     // trim_info
static constexpr Lookup kBoostInfo[] = { { 1, "boost" }, { 0, "!boost" } };                  // boost_info
static constexpr Lookup kBypassAutoInfo[] = { { 1, "bypassauto" }, { 0, "!bypassauto" } };   // bypass_auto_info
static constexpr Lookup kBypassManualInfo[] = { { 1, "bypassman" }, { 0, "!bypassman" } };   // bypass_manual_info
static constexpr Lookup kEcoModeInfo[] = { { 0, "normal" }, { 1, "ecomode" }, { 2, "essmode" } }; // eco_mode_info
static constexpr Lookup kOffInfo[] = { { 0, "off" }, { 1, "!off" } };                        // off_info
static constexpr Lookup kNobatteryInfo[] = { { 1, "!nobattery" }, { 0, "nobattery" } };      // nobattery_info
static constexpr Lookup kFanfailInfo[] = { { 1, "fanfail" }, { 0, "!fanfail" } };            // fanfail_info
static constexpr Lookup kShutdownimmInfo[] = { { 1, "shutdownimm" }, { 0, "!shutdownimm" } }; // shutdownimm_info
static constexpr Lookup kOverheatInfo[] = { { 1, "overheat" }, { 0, "!overheat" } };         // overheat_info
static constexpr Lookup kCommfaultInfo[] = { { 1, "commfault" }, { 0, "!commfault" } };      // commfault_info
static constexpr Lookup kBattvoltloInfo[] = { { 1, "battvoltlo" }, { 0, "!battvoltlo" } };   // battvoltlo_info
static constexpr Lookup kBattvolthiInfo[] = { { 1, "battvolthi" }, { 0, "!battvolthi" } };   // battvolthi_info
static constexpr Lookup kChargerfailInfo[] = { { 1, "chargerfail" }, { 0, "!chargerfail" } }; // chargerfail_info
static constexpr Lookup kVrangeInfo[] = { { 0, "!vrange" }, { 1, "vrange" } };               // vrange_info
static constexpr Lookup kFrangeInfo[] = { { 0, "!frange" }, { 1, "frange" } };               // frange_info
static constexpr Lookup kMgeOnbattInfo[] = { { 1, "!online" }, { 0, "online" } };            // mge_onbatt_info
static constexpr Lookup kFullychargedInfo[] = { { 1, "fullycharged" }, { 0, "notfullycharged" } }; // fullycharged_info
static constexpr Lookup kTimelimitexpiredInfo[] = { { 1, "timelimitexp" }, { 0, "!timelimitexp" } }; // timelimitexpired_info
static constexpr Lookup kApcStatusFlag[] = { { 8, "!off" }, { 16, "!off" }, { 0, "off" } };     // apcstatusflag_info
static constexpr Lookup kApcLineFailCauseVrange[] = {                                        // apc_linefailcause_vrange_info
    { 1, "vrange" }, { 2, "vrange" }, { 4, "vrange" }, { 8, "vrange" }, { 9, "vrange" }, { 0, "!vrange" },
};
static constexpr Lookup kApcLineFailCauseFrange[] = { { 7, "frange" }, { 0, "!frange" } };   // apc_linefailcause_frange_info
static constexpr Lookup kPowercomBeeperInfo[] = { { 1, "enabled" }, { 2, "disabled" } };    // powercom_beeper_info

// Vendor ups.alarm lookups of mge-hid.c (US-107), verbatim: the text is the alarm
static constexpr Lookup kMgeEmergencyStop[] = { { 1, "Emergency stop!" } };                    // mge_emergency_stop
static constexpr Lookup kMgeWiringFault[] = { { 1, "Wiring fault!" } };                        // mge_wiring_fault
static constexpr Lookup kMgeConfigFailure[] = { { 1, "Fatal EEPROM fault!" } };                // mge_config_failure
static constexpr Lookup kMgeInverterVolthi[] = { { 1, "Inverter AC voltage too high!" } };     // mge_inverter_volthi
static constexpr Lookup kMgeInverterVoltlo[] = { { 1, "Inverter AC voltage too low!" } };      // mge_inverter_voltlo
static constexpr Lookup kMgeShortCircuit[] = { { 1, "Output short circuit!" } };               // mge_short_circuit

// Lookups of the UPS, Output and Outlet pages (US-074), verbatim
static constexpr Lookup kBeeperInfo[] = { { 1, "disabled" }, { 2, "enabled" }, { 3, "muted" } }; // beeper_info
static constexpr Lookup kOnOff[] = { { 0, "off" }, { 1, "on" } };                           // on_off_info
static constexpr Lookup kUpsType[] = {                                                      // mge_upstype_conversion
    { 1, "offline / line interactive" }, { 2, "online" }, { 3, "online - unitary/parallel" },
    { 4, "online - parallel with hot standy" }, { 5, "online - hot standby redundancy" },
};
static constexpr Lookup kOutletProtection[] = {                                             // eaton_outlet_protection_status_info
    { 0, "not powered" }, { 1, "not protected" }, { 2, "protected" },
};
static constexpr Lookup kOutletEco[] = {                                                    // outlet_eco_yes_no_info
    { 0, "The outlet is not ECO controlled" }, { 1, "The outlet is ECO controlled" },
};

// State the mge conversion functions keep in globals upstream (mge_type, country_code,
// the static "nominal" of nominal_output_voltage_fun, the ABM flags, ups_status).
// Reset at every driver setup().
struct Context {
    long nominal = -1;
    int mge_type = MGE_DEFAULT;   // upstream default, overridden by mge_model_names
    int country = COUNTRY_UNKNOWN;
    bool country_seen = false;    // a value of UPS.PowerSummary.Country was read
    // False until the inputs of the model-dependent conversions are known (upstream has
    // them before the first walk: mge_format_model and device.country come first).
    bool ready = true;
    int abm = ABM_UNKNOWN;           // advanced_battery_monitoring
    int abm_path = ABM_PATH_UNKNOWN; // advanced_battery_path
    StatusBits status;               // ups_status, set by the BOOL rows
    // The scale factors of cps-hid.c (US-075), globals upstream: cps_battery_scale(), the hook
    // of PID 0x0501, sets might_need_scale; each factor is decided once, by the first value
    // that tells it
    struct CpsScale {
        bool might_need_scale = false; // might_need_battery_scale / might_need_freq_scale
        double battery = 1;            // battery_scale
        bool battery_checked = false;
        double input_freq = 1;         // input_freq_scale
        double output_freq = 1;        // output_freq_scale
        bool input_checked = false;
        bool output_checked = false;
    } cps;
    // last_valid of powercom_hack_voltage (powercom-hid.c, US-077): the text of the last
    // valid report 0xA4, written by PowercomDriver::decodeReport; empty ("0.0") until the first one
    char powercom_battery_voltage[32] = "";
};

// CLEAR: the variable must go (dstate_delinfo), the row stays undecided as with DEFER
enum class DecodeResult { OK, FAIL, DEFER, CLEAR };

// Instant command rows (HU_TYPE_CMD, US-074): bound to the device, never read, published or
// resolved; CommandCatalog takes their path and value (find_nut_info / instcmd upstream)
inline bool isCommandRow(const HidInfo& info) {
    return (info.hidflags & HU_TYPE_CMD) != 0;
}

// Vendor ups.alarm rows (US-107): their text goes to ups.alarm through alarm_set()
// (usbhid-ups.c:3194), never as a variable of their own; duplicates are allowed
// (usbhid-ups.c:2323)
inline bool isAlarmRow(const HidInfo& info) {
    return info.info_type && strcmp(info.info_type, "ups.alarm") == 0;
}

// Rows that never publish their info_type (US-073): the BOOL rows, the ups.alarm rows
// (US-107) and the "dummy" rows whose function only keeps state. Upstream never drops them
// at INIT (their name is never published), so they are always active and outside the
// first-row-wins rule.
inline bool isInternalRow(const HidInfo& info) {
    if (info.info_type && strcmp(info.info_type, "BOOL") == 0) return true;
    if (isAlarmRow(info)) return true;
    switch (info.conv) {
    case Conv::MgeCountry:
    case Conv::AbmEnabled:
    case Conv::AbmPathMode:
    case Conv::AbmPathStatus:
        return true;
    default:
        return false;
    }
}

// The state kept by an internal non-BOOL row (eaton_check_country_fun, eaton_abm_*_fun)
inline void applyInternal(const HidInfo& info, double v, Context& ctx) {
    switch (info.conv) {
    case Conv::MgeCountry:
        ctx.country = (int)v;
        ctx.country_seen = true;
        break;
    case Conv::AbmEnabled: {
        int value = (int)v;
        ctx.abm = (value == ABM_DISABLED || value == ABM_ENABLED) ? value : ABM_UNKNOWN;
        break;
    }
    case Conv::AbmPathMode:
    case Conv::AbmPathStatus:
        // Unknown or disabled ABM resets the path, so a re-enabled ABM can take another
        if (ctx.abm != ABM_ENABLED) ctx.abm_path = ABM_PATH_UNKNOWN;
        else if (ctx.abm_path == ABM_PATH_UNKNOWN)
            ctx.abm_path = (info.conv == Conv::AbmPathMode) ? ABM_PATH_MODE : ABM_PATH_STATUS;
        break;
    default:
        break;
    }
}

// The token of a BOOL row for value v, nullptr when its function returns NULL (US-073)
inline const char* statusToken(const HidInfo& info, double v, const Context& ctx, const UPSData* data) {
    int value = (int)v;
    bool abm_on = ctx.abm == ABM_ENABLED;
    switch (info.conv) {
    case Conv::Lookup:
        for (uint8_t i = 0; i < info.lkp_count; i++) {
            if (info.lkp[i].hid == (long)v) return info.lkp[i].nut;
        }
        return nullptr;
    case Conv::EatonDischarging: // eaton_abm_check_dischrg_fun
        if (abm_on) return "!dischrg";
        return (v == 1) ? "dischrg" : "!dischrg";
    case Conv::EatonCharging: // eaton_abm_check_chrg_fun
        if (abm_on) return "!chrg";
        if (v != 1) return "!chrg";
        if (data) {
            // CC-mode devices keep Charging asserted while floating: no CHRG at 100 %
            const String* charge = data->find("battery.charge");
            if (charge && strtod(charge->c_str(), nullptr) >= 100.0) return "!chrg";
        }
        return "chrg";
    case Conv::AbmChrgDischrg: // eaton_abm_chrg_dischrg_fun
        if (!abm_on) return nullptr;
        if (ctx.abm_path == ABM_PATH_STATUS) {
            switch (value) {
            case 1: case 2: return "chrg";
            case 4: return "dischrg";
            default: return nullptr; // 3 resting, 5 undefined, 6 charger disabled
            }
        }
        switch (value) {
        case 1: case 3: return "chrg";
        case 2: return "dischrg";
        default: return nullptr; // 4 resting, 5 undefined, 6 charger disabled
        }
    case Conv::EatonConverterOnline: // eaton_converter_online_fun: only when not OFF
        if (ctx.status.set & statusBit(ST_OFF)) return nullptr;
        return (v == 0) ? "!online" : "online";
    // powercom-hid.c:308-423, the status bytes of the PowercomUPS page. upsfail is "fanfail"
    // upstream although bit 0 is the UPS fault
    case Conv::PowercomUpsfail:
        return ((long)v & 0x0001) ? "fanfail" : "!fanfail";
    case Conv::PowercomReplacebatt:
        return ((long)v & 0x0002) ? "replacebatt" : "!replacebatt";
    case Conv::PowercomTest:
        return ((long)v & 0x0004) ? "cal" : "!cal";
    case Conv::PowercomShutdownimm:
        return ((long)v & 0x0010) ? "shutdownimm" : "!shutdownimm";
    case Conv::PowercomOnline:
        return ((long)v & 0x0001) ? "!online" : "online";
    case Conv::PowercomLowbatt:
        return ((long)v & 0x0002) ? "lowbatt" : "!lowbatt";
    case Conv::PowercomTrim:
        return (((long)v & 0x0018) == 0x0008) ? "trim" : "!trim";
    case Conv::PowercomBoost:
        return (((long)v & 0x0018) == 0x0018) ? "boost" : "!boost";
    case Conv::PowercomOverload:
        return ((long)v & 0x0020) ? "overload" : "!overload";
    default:
        return nullptr;
    }
}

// mge_type from iProduct/iModel as get_model_name() (mge-hid.c): every row of
// mge_model_names, in the upstream order, with only the fields that give the type (the
// formatted name is mge_format_model(), outside the table). Trailing spaces in iModel are
// upstream's (the Ellipse PRO rows).
inline int mgeModelType(const char* iProduct, const char* iModel) {
    struct Row { const char* product; const char* model; int type; };
    static const Row rows[] = {
        { "ELLIPSE", "300", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "500", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "650", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "800", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "1200", MGE_DEFAULT_OFFLINE },
        { "ellipse", "PR500", MGE_DEFAULT_OFFLINE },
        { "ellipse", "PR650", MGE_DEFAULT_OFFLINE },
        { "ellipse", "PR800", MGE_DEFAULT_OFFLINE },
        { "ellipse", "PR1200", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "600", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "750", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "1000", MGE_DEFAULT_OFFLINE },
        { "ELLIPSE", "1500", MGE_DEFAULT_OFFLINE },
        { "Ellipse MAX", "600", MGE_DEFAULT_OFFLINE },
        { "Ellipse MAX", "850", MGE_DEFAULT_OFFLINE },
        { "Ellipse MAX", "1100", MGE_DEFAULT_OFFLINE },
        { "Ellipse MAX", "1500", MGE_DEFAULT_OFFLINE },
        { "PROTECTIONCENTER", "420", MGE_DEFAULT_OFFLINE },
        { "PROTECTIONCENTER", "500", MGE_DEFAULT_OFFLINE },
        { "PROTECTIONCENTER", "675", MGE_DEFAULT_OFFLINE },
        { "Protection Station", "500", MGE_PEGASUS },
        { "Protection Station", "650", MGE_PEGASUS },
        { "Protection Station", "800", MGE_PEGASUS },
        { "Ellipse ECO", "650", MGE_PEGASUS },
        { "Ellipse ECO", "800", MGE_PEGASUS },
        { "Ellipse ECO", "1200", MGE_PEGASUS },
        { "Ellipse ECO", "1600", MGE_PEGASUS },
        { "3S", "450", MGE_DEFAULT_OFFLINE },
        { "3S", "550", MGE_DEFAULT_OFFLINE },
        { "3S", "700", MGE_3S },
        { "3S", "750", MGE_3S },
        { "Evolution", "500", MGE_DEFAULT },
        { "Evolution", "800", MGE_DEFAULT },
        { "Evolution", "1100", MGE_DEFAULT },
        { "Evolution", "1500", MGE_DEFAULT },
        { "Evolution", "2200", MGE_DEFAULT },
        { "Evolution", "3000", MGE_DEFAULT },
        { "Evolution", "3000XL", MGE_DEFAULT },
        { "Evolution", "650", MGE_EVOLUTION_650 },
        { "Evolution", "850", MGE_EVOLUTION_850 },
        { "Evolution", "1150", MGE_EVOLUTION_1150 },
        { "Evolution", "S 1250", MGE_EVOLUTION_S_1250 },
        { "Evolution", "1550", MGE_EVOLUTION_1550 },
        { "Evolution", "S 1750", MGE_EVOLUTION_S_1750 },
        { "Evolution", "2000", MGE_EVOLUTION_2000 },
        { "Evolution", "S 2500", MGE_EVOLUTION_S_2500 },
        { "Evolution", "S 3000", MGE_EVOLUTION_S_3000 },
        { "Eaton 5P", "650", EATON_5P },
        { "Eaton 5P", "850", EATON_5P },
        { "Eaton 5P", "1150", EATON_5P },
        { "Eaton 5P", "1550", EATON_5P },
        { "Eaton 5PX", "1500", EATON_5P },
        { "Eaton 5PX", "2200", EATON_5P },
        { "Eaton 5PX", "3000", EATON_5P },
        { "Eaton 5SC", "500", EATON_5P },
        { "Eaton 5SC", "750", EATON_5P },
        { "Eaton 5SC", "1000", EATON_5P },
        { "Eaton 5SC", "1500", EATON_5P },
        { "Eaton 5SC", "2200", EATON_5P },
        { "Eaton 5SC", "3000", EATON_5P },
        { "Ellipse PRO", "1200 ", EATON_5P },
        { "Ellipse PRO", "1500 ", EATON_5P },
        { "Ellipse PRO", "1600 ", EATON_5P },
        { "Eaton 9E", "1000", EATON_9E },
        { "Eaton 9E", "1000i", EATON_9E },
        { "Eaton 9E", "1000iau", EATON_9E },
        { "Eaton 9E", "1000ir", EATON_9E },
        { "Eaton 9E", "2000", EATON_9E },
        { "Eaton 9E", "2000i", EATON_9E },
        { "Eaton 9E", "2000iau", EATON_9E },
        { "Eaton 9E", "2000ir", EATON_9E },
        { "Eaton 9E", "3000", EATON_9E },
        { "Eaton 9E", "3000i", EATON_9E },
        { "Eaton 9E", "3000iau", EATON_9E },
        { "Eaton 9E", "3000ir", EATON_9E },
        { "Eaton 9E", "3000ixl", EATON_9E },
        { "Eaton 9E", "3000ixlau", EATON_9E },
        { "unknown", "1000", EATON_9E },
        { "unknown", "2000", EATON_9E },
        { "unknown", "3000", EATON_9E },
        { "Eaton 9SX", "700i", EATON_9E },
        { "Eaton 9SX", "1000i", EATON_9E },
        { "Eaton 9SX", "1000im", EATON_9E },
        { "Eaton 9SX", "1500i", EATON_9E },
        { "Eaton 9SX", "2000i", EATON_9E },
        { "Eaton 9SX", "3000i", EATON_9E },
        { "Eaton 9SX", "3000im", EATON_9E },
        { "Eaton 9SX", "1000ir", EATON_9E },
        { "Eaton 9SX", "1500ir", EATON_9E },
        { "Eaton 9SX", "2000ir", EATON_9E },
        { "Eaton 9SX", "3000ir", EATON_9E },
        { "Eaton 9PX", "1000irt2u", EATON_9E },
        { "Eaton 9PX", "1500irt2u", EATON_9E },
        { "Eaton 9PX", "1500irtm", EATON_9E },
        { "Eaton 9PX", "2200irt2u", EATON_9E },
        { "Eaton 9PX", "2200irt3u", EATON_9E },
        { "Eaton 9PX", "3000irt2u", EATON_9E },
        { "Eaton 9PX", "3000irt3u", EATON_9E },
        { "Eaton 9PX", "3000irtm", EATON_9E },
        { "PULSAR M", "2200", MGE_PULSAR_M_2200 },
        { "PULSAR M", "3000", MGE_PULSAR_M_3000 },
        { "PULSAR M", "3000 XL", MGE_PULSAR_M_3000_XL },
        { "EX", "2200", MGE_PULSAR_M_2200 },
        { "EX", "3000", MGE_PULSAR_M_3000 },
        { "EX", "3000 XL", MGE_PULSAR_M_3000 },
        { "PULSAR", "MX4000", MGE_DEFAULT },
        { "PULSAR", "MX5000", MGE_DEFAULT },
        { "NOVA AVR", "500", MGE_DEFAULT },
        { "NOVA AVR", "600", MGE_DEFAULT },
        { "NOVA AVR", "625", MGE_DEFAULT },
        { "NOVA AVR", "1100", MGE_DEFAULT },
        { "NOVA AVR", "1250", MGE_DEFAULT },
        { "EXtreme", "700C", MGE_DEFAULT },
        { "EXtreme", "1000C", MGE_DEFAULT },
        { "EXtreme", "1500C", MGE_DEFAULT },
        { "EXtreme", "1500CCLA", MGE_DEFAULT },
        { "EXtreme", "2200C", MGE_DEFAULT },
        { "EXtreme", "3200C", MGE_DEFAULT },
        { "EX", "700RT", MGE_DEFAULT },
        { "EX", "1000RT", MGE_DEFAULT },
        { "EX", "1500RT", MGE_DEFAULT },
        { "EX", "2200RT", MGE_DEFAULT },
        { "EX", "3200RT", MGE_DEFAULT },
        { "EX", "5RT31", MGE_DEFAULT },
        { "EX", "7RT31", MGE_DEFAULT },
        { "EX", "11RT31", MGE_DEFAULT },
        { "EX", "5RT", MGE_DEFAULT },
        { "EX", "7RT", MGE_DEFAULT },
        { "EX", "11RT", MGE_DEFAULT },
        { "GALAXY", "3000_10", MGE_DEFAULT },
        { "GALAXY", "3000_15", MGE_DEFAULT },
        { "GALAXY", "3000_20", MGE_DEFAULT },
        { "GALAXY", "3000_30", MGE_DEFAULT },
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

// cps-hid.c: battery_voltage_sanity_check
constexpr double kCpsBatteryVoltageSanityCheck = 1.4;

/**
 * cps_adjust_frequency_scale() (cps-hid.c:104-209): the factor (1 or 0.1) that brings the
 * reported frequency inside low..high; nominal, low and high are 0 when unknown. False when
 * neither fits: the value is published as it is and checked again on the next one.
 */
inline bool cpsFrequencyScale(double report, double nominal, double low, double high, double& scale) {
    if (nominal == 0) {
        if (45 < low && low <= 50) nominal = 50;
        else if (50 <= high && high <= 55) nominal = 50;
        else if (45 < report && report <= 55) nominal = 50;
        else if (450 < report && report <= 550) nominal = 50;
        else if (55 < low && low <= 60) nominal = 60;
        else if (60 <= high && high <= 65) nominal = 60;
        else if (55 < report && report <= 65) nominal = 60;
        else if (550 < report && report <= 650) nominal = 60;
    }
    if (low == 0) low = (nominal == 0) ? 45.0 : nominal * 0.95;
    if (high == 0) high = (nominal == 0) ? 65.0 : nominal * 1.05;

    if (low <= report && report <= high) {
        scale = 1.0;
        return true;
    }
    if (low <= report / 10.0 && report / 10.0 <= high) {
        scale = 0.1;
        return true;
    }
    return false;
}

// A published value as strtod() reads it from dstate_getinfo(), 0 when missing
inline double publishedNumber(const UPSData* data, const char* key) {
    const String* v = data ? data->find(key) : nullptr;
    return v ? strtod(v->c_str(), nullptr) : 0;
}

inline String formatRounded(double v) {
    return String((long)lrint(v));
}

// The row's dstate_setinfo_dynamic() format: "%.0f" rounds as printf does (lrint), "%.1f"
// and "%.2f" go through snprintf; any other format is not supported (FAIL)
inline bool formatNumber(const char* fmt, double v, String& out) {
    if (!fmt || strcmp(fmt, "%.0f") == 0) {
        out = formatRounded(v);
        return true;
    }
    if (strcmp(fmt, "%.1f") != 0 && strcmp(fmt, "%.2f") != 0) return false;
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, v);
    out = buf;
    return true;
}

/**
 * The text of the conversions that pick it from a static table (Lookup, and AbmStatus with
 * ABM enabled), nullptr when there is none: texts of static tables, returned without
 * building a String (US-073 rework, ADR 0007).
 */
inline const char* staticText(const HidInfo& info, double v, const Context& ctx) {
    switch (info.conv) {
    case Conv::Lookup:
        for (uint8_t i = 0; i < info.lkp_count; i++) {
            if (info.lkp[i].hid == (long)v) return info.lkp[i].nut;
        }
        return nullptr;
    case Conv::AbmStatus: { // eaton_abm_status_fun
        if (ctx.abm != ABM_ENABLED) return nullptr;
        int value = (int)v;
        if (value < 1 || value > 6) return nullptr;
        if (ctx.abm_path == ABM_PATH_STATUS) {
            static const char* const t[] = { nullptr, "charging", "floating", "resting", "discharging", nullptr, "off" };
            return t[value];
        }
        static const char* const t[] = { nullptr, "charging", "discharging", "floating", "resting", nullptr, "off" };
        return t[value]; // nullptr for 5, "ABM is not activated"
    }
    default:
        return nullptr;
    }
}

/**
 * Converts a HID value as ups_infoval_set() does. DEFER: the model-dependent inputs are not
 * known yet, or the upstream function returned NULL without it being a failure (a string
 * index waiting for its descriptor, an ABM value without text): retry with the next value.
 * CLEAR: the variable must be removed. data is the published state some upstream functions
 * read with dstate_getinfo(); nullptr when there is none.
 */
inline DecodeResult decode(const HidInfo& info, double v, Context& ctx, String& out, const UPSData* data = nullptr) {
    (void)data;
    char buf[32];
    switch (info.conv) {
    case Conv::Number:
        return formatNumber(info.dfl, v, out) ? DecodeResult::OK : DecodeResult::FAIL;
    case Conv::StringId:
        // HIDGetIndexString() of index 0 or less fails; the text comes with the descriptor
        return (v >= 1 && v <= 255) ? DecodeResult::DEFER : DecodeResult::FAIL;
    case Conv::KelvinCelsius:
        // The value may already be in °C (some HP implementations)
        snprintf(buf, sizeof(buf), "%.1f", (v >= 273 && v <= 373) ? v - 273.15 : v);
        out = buf;
        return DecodeResult::OK;
    case Conv::MgePowerFactor:
        snprintf(buf, sizeof(buf), "%.2f", v / 100); // mge_powerfactor_conversion_fun
        out = buf;
        return DecodeResult::OK;
    case Conv::EatonComputeRealpower: {
        // eaton_compute_realpower_fun (mge-hid.c:1404-1431): an approximation from the load,
        // the nominal apparent power and output.powerfactor (default 0.80) when the device
        // has no ActivePower; NULL (nothing published) while the inputs are missing
        if (!data) return DecodeResult::DEFER;
        const String* load = data->find("ups.load");
        const String* nominal = data->find("ups.power.nominal");
        if (!load || !nominal) return DecodeResult::DEFER;
        const String* pf_text = data->find("output.powerfactor");
        float pf = pf_text ? (float)atof(pf_text->c_str()) : 0.80f; // float, as upstream
        long realpower = lround(atoi(load->c_str()) * 0.01 * atoi(nominal->c_str()) * pf);
        snprintf(buf, sizeof(buf), "%ld", realpower);
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::CpsBattCharge: // cps_battcharge_fun: clamped to 100 %
        snprintf(buf, sizeof(buf), "%.0f", v < 100.0 ? v : 100.0);
        out = buf;
        return DecodeResult::OK;
    case Conv::CpsBattStatus: // cps_battstatus_fun: FullChargeCapacity in percent
        snprintf(buf, sizeof(buf), "%.0f%%", v);
        out = buf;
        return DecodeResult::OK;
    case Conv::CpsBattVolt: { // cps_battvolt_fun
        Context::CpsScale& cps = ctx.cps;
        if (cps.might_need_scale && !cps.battery_checked) {
            // cps_adjust_battery_scale(): decided once battery.voltage.nominal is known
            double nominal = publishedNumber(data, "battery.voltage.nominal");
            if (nominal != 0) {
                if (v / nominal > kCpsBatteryVoltageSanityCheck) cps.battery = 2.0 / 3;
                cps.battery_checked = true;
            }
        }
        snprintf(buf, sizeof(buf), "%.1f", cps.battery * v);
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::CpsInputFreq:   // cps_input_freq_fun
    case Conv::CpsOutputFreq: { // cps_output_freq_fun
        bool input = info.conv == Conv::CpsInputFreq;
        Context::CpsScale& cps = ctx.cps;
        double& scale = input ? cps.input_freq : cps.output_freq;
        bool& checked = input ? cps.input_checked : cps.output_checked;
        if (cps.might_need_scale && !checked) {
            double s;
            if (cpsFrequencyScale(v,
                    publishedNumber(data, input ? "input.frequency.nominal" : "output.frequency.nominal"),
                    publishedNumber(data, input ? "input.frequency.low" : "output.frequency.low"),
                    publishedNumber(data, input ? "input.frequency.high" : "output.frequency.high"), s)) {
                scale = s;
                checked = true;
            }
        }
        snprintf(buf, sizeof(buf), "%.1f", scale * v);
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::ApcDate: { // apc_date_conversion_fun (apc-hid.c:114-139): BCD 0xMMDDYY
        long value = (long)v;
        if (value == 0) {
            out = "not set";
            return DecodeResult::OK;
        }
        int year = (value & 0xf) + 10 * ((value >> 4) & 0xf);
        int month = ((value >> 16) & 0xf) + 10 * ((value >> 20) & 0xf);
        int day = ((value >> 8) & 0xf) + 10 * ((value >> 12) & 0xf);
        year += (year >= 70) ? 1900 : 2000;
        snprintf(buf, sizeof(buf), "%04d/%02d/%02d", year, month, day);
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::PowercomHackVoltage:
        // powercom_hack_voltage ignores the row's value (ACPresent) and serves the last valid
        // report 0xA4, read by the poll; "0.0" until the first one (last_valid upstream)
        out = ctx.powercom_battery_voltage[0] ? ctx.powercom_battery_voltage : "0.0";
        return DecodeResult::OK;
    case Conv::PowercomVoltage: // powercom_voltage_conversion_fun
        snprintf(buf, sizeof(buf), "%0.0f", v * 4);
        out = buf;
        return DecodeResult::OK;
    case Conv::PowercomStartup: { // powercom_startup_fun: minutes, bytes swapped
        uint16_t i = (uint16_t)(long)v; // upstream converts double to uint16_t implicitly
        snprintf(buf, sizeof(buf), "%d", 60 * (((i & 0x00FF) << 8) + (i >> 8)));
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::PowercomShutdown: { // powercom_shutdown_fun: minutes in the high byte, seconds in the low one
        // The default byte order only: the ups.conf flag powercom_sdcmd_byte_order_fallback
        // has no counterpart in the firmware
        uint16_t i = (uint16_t)(long)v;
        snprintf(buf, sizeof(buf), "%d", 60 * (i >> 8) + (i & 0x00FF));
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::MgeBatteryCapacity:
        snprintf(buf, sizeof(buf), "%.2f", v / 3600); // As to Ah
        out = buf;
        return DecodeResult::OK;
    case Conv::MgeBatteryVoltage:
    case Conv::MgeBatteryVoltageNominal: {
        // Only some series report the battery in UPS.PowerSummary (mge_battery_voltage_fun)
        if (!ctx.ready) return DecodeResult::DEFER;
        bool nominal = info.conv == Conv::MgeBatteryVoltageNominal;
        switch (ctx.mge_type & 0xFF00) {
        case MGE_EVOLUTION:
            if (nominal && ctx.mge_type == MGE_EVOLUTION_650) v = 12.0;
            break;
        case MGE_PULSAR_M:
        case EATON_5P:
        case EATON_9E:
            break;
        default:
            return DecodeResult::FAIL;
        }
        snprintf(buf, sizeof(buf), nominal ? "%.0f" : "%.1f", v);
        out = buf;
        return DecodeResult::OK;
    }
    case Conv::AbmStatus: { // eaton_abm_status_fun
        if (ctx.abm != ABM_ENABLED) return DecodeResult::CLEAR; // user switched ABM off
        const char* text = staticText(info, v, ctx);
        if (!text) return DecodeResult::DEFER; // 5 "ABM is not activated", unknown values
        out = text;
        return DecodeResult::OK;
    }
    case Conv::MgeCountry:
    case Conv::AbmEnabled:
    case Conv::AbmPathMode:
    case Conv::AbmPathStatus:
    case Conv::AbmChrgDischrg:
    case Conv::EatonDischarging:
    case Conv::EatonCharging:
    case Conv::EatonConverterOnline:
    case Conv::PowercomUpsfail:
    case Conv::PowercomReplacebatt:
    case Conv::PowercomTest:
    case Conv::PowercomShutdownimm:
    case Conv::PowercomOnline:
    case Conv::PowercomLowbatt:
    case Conv::PowercomTrim:
    case Conv::PowercomBoost:
    case Conv::PowercomOverload:
        // Internal rows and BOOL functions: applyInternal() / statusToken()
        return DecodeResult::DEFER;
    case Conv::Lookup: {
        const char* text = staticText(info, v, ctx);
        if (!text) return DecodeResult::FAIL;
        out = text;
        return DecodeResult::OK;
    }
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

// --- Reverse conversions (US-067): NUT text -> HID value, as hu_find_valinfo() and the
// nuf functions upstream. Every text upstream would turn into 0 (atol("abc"), a date
// that does not parse) is FAIL here, so that a typo never reaches the UPS.

enum class EncodeResult { OK, FAIL };

// Seconds since 1970-01-01 00:00:00 UTC of a broken-down UTC time (days-from-civil):
// the inverse of the gmtime() used by decode(). Never mktime(), which depends on the
// time zone of the host.
inline int64_t timegmUtc(int year, int month, int day, int hour, int minute, int second) {
    int64_t y = (int64_t)year - (month <= 2 ? 1 : 0);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                                          // [0, 399]
    int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1; // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                  // [0, 146096]
    int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + second;
}

inline bool isSpaceChar(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// A decimal number as atol() reads it in setvar() (usbhid-ups.c:1231), integer part
// truncated toward zero: spaces, optional sign, at least one digit, optional '.' and
// digits, spaces. Anything else fails: exponents ("1e3", 1 for atol()), hex, trailing
// characters ("30x", 30 for atol()), the empty string, an integer part outside int32_t.
inline bool parseTruncatedNumber(const char* s, int32_t& out) {
    if (!s) return false;
    const char* p = s;
    while (isSpaceChar(*p)) p++;
    bool neg = false;
    if (*p == '+' || *p == '-') neg = (*p++ == '-');
    if (*p < '0' || *p > '9') return false;
    int64_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        if (v > (int64_t)INT32_MAX + 1) return false; // stops before int64_t can overflow
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') p++;
    }
    while (isSpaceChar(*p)) p++;
    if (*p != '\0') return false;
    if (neg) v = -v;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    out = (int32_t)v;
    return true;
}

// Between min_digits and max_digits decimal digits at p, advanced past them
inline bool parseDigits(const char*& p, int min_digits, int max_digits, int& out) {
    int n = 0, v = 0;
    while (n < max_digits && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        n++;
    }
    if (n < min_digits) return false;
    out = v;
    return true;
}

inline int daysInMonth(int year, int month) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return (month == 2 && leap) ? 29 : days[month - 1];
}

// "%Y/%m/%d %H:%M:%S" in UTC, parsed by hand (strptime() is missing on mingw). Stricter
// than strptime(): a 4-digit year from 1970, a real calendar day, nothing after the time.
inline bool parseMgeDateTime(const char* s, int64_t& out) {
    const char* p = s;
    int y, mo, d, h, mi, sec;
    if (!parseDigits(p, 4, 4, y) || *p++ != '/') return false;
    if (!parseDigits(p, 1, 2, mo) || *p++ != '/') return false;
    if (!parseDigits(p, 1, 2, d)) return false;
    if (!isSpaceChar(*p)) return false;
    while (isSpaceChar(*p)) p++;
    if (!parseDigits(p, 1, 2, h) || *p++ != ':') return false;
    if (!parseDigits(p, 1, 2, mi) || *p++ != ':') return false;
    if (!parseDigits(p, 1, 2, sec) || *p != '\0') return false;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo)) return false;
    if (h > 23 || mi > 59 || sec > 59) return false;
    out = timegmUtc(y, mo, d, h, mi, sec);
    return true;
}

/**
 * Converts the text of a SET VAR into the HID value of the row (before the unit exponent:
 * HIDParser::toLogical() does that). data holds the current ups.date / ups.time, the other
 * half of the MGE clock. FAIL is STAT_SET_CONVERSION_FAILED.
 */
inline EncodeResult encode(const HidInfo& info, const char* val, const Context& ctx, const UPSData& data, double& out) {
    if (!val) return EncodeResult::FAIL;
    switch (info.conv) {
    case Conv::Number: {
        // setvar(): value = atol(val) when the row has no lookup (usbhid-ups.c:1231)
        int32_t v;
        if (!parseTruncatedNumber(val, v)) return EncodeResult::FAIL;
        out = (double)v;
        return EncodeResult::OK;
    }
    case Conv::Lookup:
    case Conv::NominalVoltage:
    case Conv::PegasusThreshold:
        // hu_find_valinfo() (usbhid-ups.c:3090): strcmp on the table when the first entry
        // has no nuf, as nominal_output_voltage_info and pegasus_threshold_info
        for (uint8_t i = 0; i < info.lkp_count; i++) {
            if (strcmp(info.lkp[i].nut, val) == 0) {
                out = info.lkp[i].hid;
                return EncodeResult::OK;
            }
        }
        return EncodeResult::FAIL;
    case Conv::PegasusYesNo:
        // pegasus_yes_no_info_nuf() (mge-hid.c:877), literally: 0 on a non Pegasus model,
        // 1 for any text starting with "yes", 0 otherwise ("assuming no")
        if (!pegasusValid(ctx)) out = 0;
        else out = (strncmp(val, "yes", 3) == 0) ? 1 : 0;
        return EncodeResult::OK;
    case Conv::BatteryDate: {
        // date_conversion_reverse() (usbhid-ups.c:561), which writes 0 ("not set") for an
        // out of range date: FAIL here. Exactly YYYY/MM/DD, 1980..2107 (7 bits of year).
        const char* p = val;
        int y, m, d;
        if (!parseDigits(p, 4, 4, y) || *p++ != '/') return EncodeResult::FAIL;
        if (!parseDigits(p, 2, 2, m) || *p++ != '/') return EncodeResult::FAIL;
        if (!parseDigits(p, 2, 2, d) || *p != '\0') return EncodeResult::FAIL;
        if (y < 1980 || y - 1980 > 127 || m < 1 || m > 12 || d < 1 || d > 31) return EncodeResult::FAIL;
        out = (double)(((y - 1980) << 9) + (m << 5) + d);
        return EncodeResult::OK;
    }
    case Conv::MgeDate:
    case Conv::MgeTime: {
        // mge_date_conversion_nuf() / mge_time_conversion_nuf() (mge-hid.c:626-655): the
        // new half joined with the current other half, then mktime(). Here in UTC, as
        // decode() uses gmtime(). Upstream writes 0 (1970) when the text does not parse.
        bool date = info.conv == Conv::MgeDate;
        String other = data.get(date ? "ups.time" : "ups.date");
        if (other.length() == 0) return EncodeResult::FAIL;
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "%s %s", date ? val : other.c_str(), date ? other.c_str() : val);
        if (n < 0 || (size_t)n >= sizeof(buf)) return EncodeResult::FAIL;
        int64_t sec;
        if (!parseMgeDateTime(buf, sec)) return EncodeResult::FAIL;
        out = (double)sec;
        return EncodeResult::OK;
    }
    default:
        // Read-only conversions (US-073): no row with them is ST_FLAG_RW
        break;
    }
    return EncodeResult::FAIL;
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
    bool cleared = false;  // CLEAR since the last publication: the key must be removed
    uint8_t string_index = 0; // Conv::StringId: the string descriptor that holds the value
    double last = 0;          // internal rows: last value read, applied again by walkInternalRows()
    bool seen = false;        // internal rows: last holds a value
    enum class First : uint8_t { UNREAD, OK, FAIL } first = First::UNREAD;
    enum class Role : uint8_t { PENDING, ACTIVE, DEAD } role = Role::PENDING;
};

// The upstream INIT rule: per variable, the first row in table order whose first value
// converts wins; later rows with the same name are dropped (item->hiddata = NULL).
// Internal rows (isInternalRow) are outside the rule: always active.
inline void resolve(std::vector<Bound>& binds) {
    for (size_t i = 0; i < binds.size(); i++) {
        if (isInternalRow(*binds[i].info) || isCommandRow(*binds[i].info)) continue;
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
            if (isInternalRow(*b.info) || isCommandRow(*b.info)) continue;
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
        if (isInternalRow(table[r])) b.role = Bound::Role::ACTIVE;
        binds.push_back(b);
    }
    resolve(binds);
    return binds;
}

/**
 * A value read for binds[i]. Internal rows only update ctx: a BOOL row applies its status
 * token (process_boolean_info), the others keep their state (applyInternal). data is the
 * published state read by some upstream functions (EatonCharging).
 */
inline void onValue(std::vector<Bound>& binds, size_t i, double v, Context& ctx, const UPSData* data = nullptr) {
    Bound& b = binds[i];
    if (b.info->hidflags & HU_FLAG_ABSENT) return; // driver-side value, never read
    if (isCommandRow(*b.info)) return;             // HU_TYPE_CMD: written, never published
    if (isInternalRow(*b.info)) {
        b.last = v;
        b.seen = true;
        if (strcmp(b.info->info_type, "BOOL") == 0) applyStatusToken(ctx.status, statusToken(*b.info, v, ctx, data));
        else if (!isAlarmRow(*b.info)) applyInternal(*b.info, v, ctx); // ups.alarm: read by vendorAlarms()
        return;
    }
    if (b.role == Bound::Role::DEAD) return;
    if (b.info->conv == Conv::StringId && v >= 1 && v <= 255) b.string_index = (uint8_t)v;
    // Texts of static tables: no String built, and none copied when the value is the same
    if (b.info->conv == Conv::Lookup || (b.info->conv == Conv::AbmStatus && ctx.abm == ABM_ENABLED)) {
        if (const char* text = staticText(*b.info, v, ctx)) {
            if (!b.has || !(b.value == text)) b.value = text;
            b.has = true;
            b.deferred = false;
            if (b.first == Bound::First::UNREAD) b.first = Bound::First::OK;
            resolve(binds);
            return;
        }
        // No text: decode() gives the same FAIL / DEFER as the upstream lookup
    }
    String out;
    DecodeResult r = decode(*b.info, v, ctx, out, data);
    if (r == DecodeResult::CLEAR) {
        // dstate_delinfo(): the variable goes whoever wrote it
        b.cleared = true;
        b.has = false;
    }
    b.deferred = (r == DecodeResult::DEFER || r == DecodeResult::CLEAR);
    if (b.deferred) return;
    if (r == DecodeResult::OK) {
        if (!b.has || !(b.value == out)) b.value = out;
        b.has = true;
        if (b.first == Bound::First::UNREAD) b.first = Bound::First::OK;
    } else if (b.first == Bound::First::UNREAD) {
        // As ups_infoval_set() failing during INIT; later failures keep the old value
        b.first = Bound::First::FAIL;
    }
    resolve(binds);
}

/**
 * The upstream walk over the internal rows (US-073): hid_ups_walk() applies every BOOL and
 * state row in table order, then ups_status_set() reads the bits. Rows of different reports
 * must not undo each other depending on which report came last (with ABM enabled,
 * eaton_discharging_info gives "!dischrg" and the later Charger.Mode row gives "dischrg"),
 * so after each report the last value of every row read so far is applied again, in order.
 */
inline void walkInternalRows(const std::vector<Bound>& binds, Context& ctx, const UPSData* data = nullptr) {
    for (const Bound& b : binds) {
        if (!b.seen || !isInternalRow(*b.info) || isAlarmRow(*b.info)) continue;
        if (strcmp(b.info->info_type, "BOOL") == 0) applyStatusToken(ctx.status, statusToken(*b.info, b.last, ctx, data));
        else applyInternal(*b.info, b.last, ctx);
    }
}

/**
 * The texts of the vendor ups.alarm rows read so far, in table order and separated by a
 * space, as hid_ups_walk() calls alarm_set() (US-107). A value without a text in the row's
 * lookup gives none, as a failed hu_find_infoval().
 */
inline void vendorAlarms(const std::vector<Bound>& binds, String& out) {
    for (const Bound& b : binds) {
        if (!b.seen || !isAlarmRow(*b.info)) continue;
        for (uint8_t i = 0; i < b.info->lkp_count; i++) {
            if (b.info->lkp[i].hid != (long)b.last) continue;
            if (out.length() > 0) out += ' ';
            out += b.info->lkp[i].nut;
            break;
        }
    }
}

// The text of string descriptor binds[i].string_index arrived (stringid_conversion)
inline void onString(std::vector<Bound>& binds, size_t i, const String& text) {
    Bound& b = binds[i];
    if (b.role == Bound::Role::DEAD) return;
    b.value = text;
    b.has = true;
    b.deferred = false;
    if (b.first == Bound::First::UNREAD) b.first = Bound::First::OK;
    resolve(binds);
}

// End of the first full poll, the end of upstream's INIT walk: rows never read count as
// failed, so the next row with the same name can take over. A deferred row was read and
// waits only for its conversion inputs (or its string): it is decided by its next value.
inline void finishInit(std::vector<Bound>& binds) {
    bool changed = false;
    for (Bound& b : binds) {
        if (isInternalRow(*b.info) || isCommandRow(*b.info)) continue;
        if (b.first == Bound::First::UNREAD && !b.deferred) {
            b.first = Bound::First::FAIL;
            changed = true;
        }
    }
    if (changed) resolve(binds);
}

// What a SET VAR writes, decided by the driver without any I/O (IUPSDriver::planSet)
struct SetPlan {
    enum class Kind : uint8_t {
        HID,         // raw into the FEATURE field def (read-modify-write of its report)
        DRIVER_SIDE  // value kept by the firmware (HU_FLAG_ABSENT rows, ups.delay.*)
    } kind = Kind::HID;
    const char* name = nullptr; // canonical variable name (the table's info_type)
    HIDUsageDef def;            // HID: the FEATURE usage to write
    int32_t raw = 0;            // HID: logical value
    String value;               // the value as set (DRIVER_SIDE: the value to keep)
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

// liebert-hid.c:133-134: PowerSummary only (US-097)
inline const DelayPaths* liebertDelayPaths() {
    static const char* const sd[] = { "UPS.PowerSummary.DelayBeforeShutdown" };
    static const char* const st[] = { "UPS.PowerSummary.DelayBeforeStartup" };
    static const DelayPaths p = { sd, 1, st, 1 };
    return &p;
}

// legrand-hid.c:165-168: OutletSystem.Outlet first, then Output (US-087)
inline const DelayPaths* legrandDelayPaths() {
    static const char* const sd[] = { "UPS.OutletSystem.Outlet.DelayBeforeShutdown", "UPS.Output.DelayBeforeShutdown" };
    static const char* const st[] = { "UPS.OutletSystem.Outlet.DelayBeforeStartup", "UPS.Output.DelayBeforeStartup" };
    static const DelayPaths p = { sd, 2, st, 2 };
    return &p;
}

// Subdriver tables, rows in upstream order with their source line. mgeHid2Nut() holds every
// row of mge_hid2nut (US-066, US-073, US-074, US-107), instant commands and ups.alarm rows
// included, but the rows of the stateful mge functions (US-070); apcHid2Nut() (US-076),
// cpsHid2Nut() (US-075) and powercomHid2Nut() (US-077) every row of their upstream table.
// ecoflowHid2Nut() still holds only its ST_FLAG_RW rows (US-066). ups.delay.* is left to
// CommandCatalog::applyDefaultDelays.

inline const HidInfo* mgeHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // --- Device collection
        // mge-hid.c:2006 UPS.PowerSummary.Country
        { "device.country", ST_FLAG_STRING, 20, "UPS.PowerSummary.0xFFFF0095", "Europe", HU_FLAG_STATIC, Conv::MgeCountry, nullptr, 0 },
        // mge-hid.c:2007 UPS.System.USB.iVersion
        { "device.usb.version", ST_FLAG_STRING, 20, "UPS.0xFFFF00E5.0xFFFF0005.0xFFFF00F1", nullptr, HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // device.usb.mode (mge-hid.c:2008): commented out upstream
        // device.gateway.power.rate (mge-hid.c:2009): commented out upstream

        // --- Battery page
        // mge-hid.c:2012 UPS.PowerSummary.RemainingCapacity
        { "battery.charge", 0, 0, "UPS.PowerSummary.RemainingCapacity", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2013 UPS.PowerSummary.RemainingCapacityLimitSetting
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.PowerSummary.0xFFFF004D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2014 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", 0, 0, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2015 UPS.PowerSummary.RestartLevel
        { "battery.charge.restart", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.PowerSummary.0xFFFF005F", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2016 UPS.BatterySystem.Battery.DesignCapacity
        { "battery.capacity", 0, 0, "UPS.BatterySystem.Battery.DesignCapacity", "%s", HU_FLAG_STATIC, Conv::MgeBatteryCapacity, nullptr, 0 },
        // mge-hid.c:2017 UPS.PowerSummary.RunTimeToEmpty
        { "battery.runtime", 0, 0, "UPS.PowerSummary.RunTimeToEmpty", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2018 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2019 UPS.StatisticSystem.Input.[1].Statistic.[1].Time
        { "battery.runtime.elapsed", 0, 0, "UPS.0xFFFF0004.Input.[1].0xFFFF0003.[1].0xFFFF0097", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // mge-hid.c:2020 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2021 UPS.BatterySystem.Battery.Temperature
        { "battery.temperature", 0, 0, "UPS.BatterySystem.Battery.Temperature", "%s", 0, Conv::KelvinCelsius, nullptr, 0 },
        // mge-hid.c:2022 UPS.PowerSummary.iDeviceChemistry
        { "battery.type", 0, 0, "UPS.PowerSummary.iDeviceChemistry", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // mge-hid.c:2023 UPS.BatterySystem.Voltage
        { "battery.voltage", 0, 0, "UPS.BatterySystem.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2024 UPS.PowerSummary.Voltage
        { "battery.voltage", 0, 0, "UPS.PowerSummary.Voltage", "%s", 0, Conv::MgeBatteryVoltage, nullptr, 0 },
        // mge-hid.c:2025 UPS.BatterySystem.ConfigVoltage
        { "battery.voltage.nominal", 0, 0, "UPS.BatterySystem.ConfigVoltage", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2026 UPS.PowerSummary.ConfigVoltage
        { "battery.voltage.nominal", 0, 0, "UPS.PowerSummary.ConfigVoltage", "%s", HU_FLAG_STATIC, Conv::MgeBatteryVoltageNominal, nullptr, 0 },
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

        // --- ABM (Advanced Battery Monitoring) processing
        // mge-hid.c:2040 UPS.BatterySystem.Charger.ABMEnable
        { "battery.charger.abm.status", 0, 0, "UPS.BatterySystem.Charger.0xFFFF0087", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmEnabled, nullptr, 0 },
        // mge-hid.c:2047 UPS.BatterySystem.Charger.Mode
        { "battery.charger.mode.status", 0, 0, "UPS.BatterySystem.Charger.0xFFFF0094", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmPathMode, nullptr, 0 },
        // mge-hid.c:2048 UPS.BatterySystem.Charger.Status
        { "battery.charger.type.status", 0, 0, "UPS.BatterySystem.Charger.0xFFFF00E9", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmPathStatus, nullptr, 0 },
        // mge-hid.c:2051 UPS.BatterySystem.Charger.Mode
        { "battery.charger.status", 0, 0, "UPS.BatterySystem.Charger.0xFFFF0094", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmStatus, nullptr, 0 },
        // mge-hid.c:2052 UPS.BatterySystem.Charger.Status
        { "battery.charger.status", 0, 0, "UPS.BatterySystem.Charger.0xFFFF00E9", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmStatus, nullptr, 0 },
        // battery.charger.status (mge-hid.c:2056): commented out upstream
        // mge-hid.c:2059 UPS.BatterySystem.Charger.ChargerType
        { "battery.charger.type", 0, 0, "UPS.BatterySystem.Charger.0xFFFF006B", "%.0f", HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kEatonChargerType) },

        // --- UPS page
        // mge-hid.c:2062 UPS.PowerConverter.Output.Efficiency
        { "ups.efficiency", 0, 0, "UPS.PowerConverter.Output.0xFFFF0086", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2063 UPS.PowerSummary.iVersion
        { "ups.firmware", 0, 0, "UPS.PowerSummary.0xFFFF00F1", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // mge-hid.c:2064 UPS.PowerSummary.PercentLoad
        { "ups.load", 0, 0, "UPS.PowerSummary.PercentLoad", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2065 UPS.Flow.[4].ConfigPercentLoad
        { "ups.load.high", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.Flow.[4].ConfigPercentLoad", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // ups.delay.start (mge-hid.c:2066) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (mge-hid.c:2067) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // mge-hid.c:2068 UPS.PowerSummary.DelayBeforeStartup
        { "ups.timer.start", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // mge-hid.c:2069 UPS.PowerSummary.DelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // mge-hid.c:2070 UPS.PowerSummary.DelayBeforeReboot
        { "ups.timer.reboot", 0, 0, "UPS.PowerSummary.DelayBeforeReboot", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // mge-hid.c:2071 UPS.BatterySystem.Battery.Test
        { "ups.test.result", 0, 0, "UPS.BatterySystem.Battery.Test", "%s", 0, Conv::Lookup, NUT_LKP(kTestRead) },
        // mge-hid.c:2072 UPS.BatterySystem.Battery.TestPeriod
        { "ups.test.interval", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.BatterySystem.Battery.0xFFFF0045", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2075 UPS.BatterySystem.Battery.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.BatterySystem.Battery.AudibleAlarmControl", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kBeeperInfo) },
        // mge-hid.c:2076 UPS.PowerSummary.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kBeeperInfo) },
        // mge-hid.c:2077 UPS.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.AudibleAlarmControl", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kBeeperInfo) },
        // mge-hid.c:2078 UPS.PowerSummary.Temperature
        { "ups.temperature", 0, 0, "UPS.PowerSummary.Temperature", "%s", 0, Conv::KelvinCelsius, nullptr, 0 },
        // mge-hid.c:2079 UPS.PowerConverter.Output.ApparentPower
        { "ups.power", 0, 0, "UPS.PowerConverter.Output.ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2080 UPS.PowerConverter.Output.Phase.[1].ApparentPower
        { "ups.L1.power", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[1].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2081 UPS.PowerConverter.Output.Phase.[2].ApparentPower
        { "ups.L2.power", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[2].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2082 UPS.PowerConverter.Output.Phase.[3].ApparentPower
        { "ups.L3.power", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[3].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2083 UPS.Flow.[4].ConfigApparentPower
        { "ups.power.nominal", 0, 0, "UPS.Flow.[4].ConfigApparentPower", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2084 UPS.PowerConverter.Output.ActivePower
        { "ups.realpower", 0, 0, "UPS.PowerConverter.Output.ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2087 UPS.Flow.[4].ConfigApparentPower
        { "ups.realpower", 0, 0, "UPS.Flow.[4].ConfigApparentPower", "-1", 0, Conv::EatonComputeRealpower, nullptr, 0 },
        // mge-hid.c:2088 UPS.PowerConverter.Output.Phase.[1].ActivePower
        { "ups.L1.realpower", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[1].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2089 UPS.PowerConverter.Output.Phase.[2].ActivePower
        { "ups.L2.realpower", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[2].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2090 UPS.PowerConverter.Output.Phase.[3].ActivePower
        { "ups.L3.realpower", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[3].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2091 UPS.Flow.[4].ConfigActivePower
        { "ups.realpower.nominal", 0, 0, "UPS.Flow.[4].ConfigActivePower", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
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
        // ups.date (mge-hid.c:2100): in a disabled preprocessor branch upstream
        // ups.time (mge-hid.c:2101): in a disabled preprocessor branch upstream
        // mge-hid.c:2103 UPS.PowerConverter.ConverterType
        { "ups.type", 0, 0, "UPS.PowerConverter.0xFFFF0041", "%s", HU_FLAG_STATIC, Conv::Lookup, NUT_LKP(kUpsType) },

        // --- Special case: boolean values that are mapped to ups.status and ups.alarm
        // mge-hid.c:2106 UPS.PowerSummary.PresentStatus.ACPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kOnlineInfo) },
        // mge-hid.c:2107 UPS.PowerConverter.Input.[3].PresentStatus.Used
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[3].PresentStatus.Used", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeOnbattInfo) },
        // #if 0 upstream (line 2108): the row below is disabled there
        // BOOL (mge-hid.c:2110): in a disabled preprocessor branch upstream
        // mge-hid.c:2113 UPS.PowerSummary.PresentStatus.Discharging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Discharging", nullptr, HU_FLAG_QUICK_POLL, Conv::EatonDischarging, nullptr, 0 },
        // mge-hid.c:2114 UPS.PowerSummary.PresentStatus.Charging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Charging", nullptr, HU_FLAG_QUICK_POLL, Conv::EatonCharging, nullptr, 0 },
        // mge-hid.c:2116 UPS.BatterySystem.Charger.Mode
        { "BOOL", 0, 0, "UPS.BatterySystem.Charger.0xFFFF0094", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmChrgDischrg, nullptr, 0 },
        // mge-hid.c:2117 UPS.BatterySystem.Charger.Status
        { "BOOL", 0, 0, "UPS.BatterySystem.Charger.0xFFFF00E9", "%.0f", HU_FLAG_QUICK_POLL, Conv::AbmChrgDischrg, nullptr, 0 },
        // mge-hid.c:2119 UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kLowbattInfo) },
        // mge-hid.c:2121 UPS.PowerConverter.Output.Overload.[1].PresentStatus.OverThreshold
        { "BOOL", 0, 0, "UPS.PowerConverter.Output.Overload.[1].PresentStatus.0xFFFF007E", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },
        // mge-hid.c:2123 UPS.PowerConverter.Output.Overload.[2].PresentStatus.OverThreshold
        { "BOOL", 0, 0, "UPS.PowerConverter.Output.Overload.[2].PresentStatus.0xFFFF007E", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },
        // mge-hid.c:2125 UPS.PowerSummary.PresentStatus.Overload
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Overload", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },
        // mge-hid.c:2126 UPS.PowerSummary.PresentStatus.NeedReplacement
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.NeedReplacement", nullptr, 0, Conv::Lookup, NUT_LKP(kReplacebattInfo) },
        // mge-hid.c:2128 UPS.LCMSystem.LCMAlarm.[2].PresentStatus.TimerExpired
        { "BOOL", 0, 0, "UPS.0xFFFF0018.0xFFFF001A.[2].PresentStatus.0xFFFF0093", nullptr, 0, Conv::Lookup, NUT_LKP(kReplacebattInfo) },
        // mge-hid.c:2129 UPS.PowerConverter.Input.[1].PresentStatus.Buck
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.Buck", nullptr, 0, Conv::Lookup, NUT_LKP(kTrimInfo) },
        // mge-hid.c:2130 UPS.PowerConverter.Input.[1].PresentStatus.Boost
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.Boost", nullptr, 0, Conv::Lookup, NUT_LKP(kBoostInfo) },
        // mge-hid.c:2131 UPS.PowerConverter.Input.[1].PresentStatus.VoltageOutOfRange
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.VoltageOutOfRange", nullptr, 0, Conv::Lookup, NUT_LKP(kVrangeInfo) },
        // mge-hid.c:2132 UPS.PowerConverter.Input.[1].PresentStatus.FrequencyOutOfRange
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.FrequencyOutOfRange", nullptr, 0, Conv::Lookup, NUT_LKP(kFrangeInfo) },
        // mge-hid.c:2133 UPS.PowerSummary.PresentStatus.Good
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Good", nullptr, 0, Conv::Lookup, NUT_LKP(kOffInfo) },
        // mge-hid.c:2136 UPS.PowerConverter.Input.[1].PresentStatus.Used
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.Used", nullptr, 0, Conv::EatonConverterOnline, nullptr, 0 },
        // mge-hid.c:2137 UPS.PowerConverter.Input.[2].PresentStatus.Used
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[2].PresentStatus.Used", nullptr, 0, Conv::Lookup, NUT_LKP(kBypassAutoInfo) },
        // mge-hid.c:2139 UPS.PowerConverter.Input.[4].PresentStatus.Used
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[4].PresentStatus.Used", nullptr, 0, Conv::Lookup, NUT_LKP(kBypassManualInfo) },
        // mge-hid.c:2140 UPS.PowerConverter.Input.[5].PresentStatus.Used
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[5].PresentStatus.Used", nullptr, 0, Conv::Lookup, NUT_LKP(kEcoModeInfo) },
        // mge-hid.c:2141 UPS.PowerSummary.PresentStatus.FanFailure
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.0xFFFF0077", nullptr, 0, Conv::Lookup, NUT_LKP(kFanfailInfo) },
        // mge-hid.c:2142 UPS.BatterySystem.Battery.PresentStatus.Present
        { "BOOL", 0, 0, "UPS.BatterySystem.Battery.PresentStatus.Present", nullptr, 0, Conv::Lookup, NUT_LKP(kNobatteryInfo) },
        // mge-hid.c:2143 UPS.BatterySystem.Charger.PresentStatus.InternalFailure
        { "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.InternalFailure", nullptr, 0, Conv::Lookup, NUT_LKP(kChargerfailInfo) },
        // mge-hid.c:2144 UPS.BatterySystem.Charger.PresentStatus.VoltageTooHigh
        { "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.0xFFFF0074", nullptr, 0, Conv::Lookup, NUT_LKP(kBattvolthiInfo) },
        // mge-hid.c:2145 UPS.PowerConverter.Input.[1].PresentStatus.VoltageTooHigh
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.0xFFFF0074", nullptr, 0, Conv::Lookup, NUT_LKP(kBattvolthiInfo) },
        // mge-hid.c:2147 UPS.BatterySystem.Battery.PresentStatus.VoltageTooHigh
        { "BOOL", 0, 0, "UPS.BatterySystem.Battery.PresentStatus.0xFFFF0074", nullptr, 0, Conv::Lookup, NUT_LKP(kBattvolthiInfo) },
        // mge-hid.c:2148 UPS.BatterySystem.Charger.PresentStatus.VoltageTooLow
        { "BOOL", 0, 0, "UPS.BatterySystem.Charger.PresentStatus.0xFFFF0075", nullptr, 0, Conv::Lookup, NUT_LKP(kBattvoltloInfo) },
        // mge-hid.c:2149 UPS.PowerConverter.Input.[1].PresentStatus.VoltageTooLow
        { "BOOL", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.0xFFFF0075", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeOnbattInfo) },
        // mge-hid.c:2150 UPS.PowerSummary.PresentStatus.InternalFailure
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.InternalFailure", nullptr, 0, Conv::Lookup, NUT_LKP(kCommfaultInfo) },
        // mge-hid.c:2151 UPS.PowerSummary.PresentStatus.OverTemperature
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.OverTemperature", nullptr, 0, Conv::Lookup, NUT_LKP(kOverheatInfo) },
        // mge-hid.c:2152 UPS.PowerSummary.PresentStatus.ShutdownImminent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ShutdownImminent", nullptr, 0, Conv::Lookup, NUT_LKP(kShutdownimmInfo) },

        // --- Vendor specific ups.alarm
        // mge-hid.c:2155 UPS.PowerSummary.PresentStatus.EmergencyStop
        { "ups.alarm", 0, 0, "UPS.PowerSummary.PresentStatus.0xFFFF0068", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeEmergencyStop) },
        // mge-hid.c:2156 UPS.PowerConverter.Input.[1].PresentStatus.WiringFault
        { "ups.alarm", 0, 0, "UPS.PowerConverter.Input.[1].PresentStatus.0xFFFF0078", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeWiringFault) },
        // mge-hid.c:2157 UPS.PowerSummary.PresentStatus.ConfigurationFailure
        { "ups.alarm", 0, 0, "UPS.PowerSummary.PresentStatus.0xFFFF002D", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeConfigFailure) },
        // mge-hid.c:2158 UPS.PowerConverter.Inverter.PresentStatus.VoltageTooHigh
        { "ups.alarm", 0, 0, "UPS.PowerConverter.0xFFFF0014.PresentStatus.0xFFFF0074", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeInverterVolthi) },
        // mge-hid.c:2159 UPS.PowerConverter.Inverter.PresentStatus.VoltageTooLow
        { "ups.alarm", 0, 0, "UPS.PowerConverter.0xFFFF0014.PresentStatus.0xFFFF0075", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeInverterVoltlo) },
        // mge-hid.c:2160 UPS.PowerConverter.Output.PresentStatus.ShortCircuit
        { "ups.alarm", 0, 0, "UPS.PowerConverter.Output.PresentStatus.0xFFFF004A", nullptr, 0, Conv::Lookup, NUT_LKP(kMgeShortCircuit) },

        // --- Input page
        // mge-hid.c:2163 UPS.PowerConverter.Input.[1].Voltage
        { "input.voltage", 0, 0, "UPS.PowerConverter.Input.[1].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2164 UPS.PowerConverter.Input.[1].Phase.[1].Voltage
        { "input.L1-N.voltage", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[1].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2165 UPS.PowerConverter.Input.[1].Phase.[2].Voltage
        { "input.L2-N.voltage", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[2].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2166 UPS.PowerConverter.Input.[1].Phase.[3].Voltage
        { "input.L3-N.voltage", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[3].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2167 UPS.PowerConverter.Input.[1].Phase.[12].Voltage
        { "input.L1-L2.voltage", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[12].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2168 UPS.PowerConverter.Input.[1].Phase.[23].Voltage
        { "input.L2-L3.voltage", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[23].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2169 UPS.PowerConverter.Input.[1].Phase.[31].Voltage
        { "input.L3-L1.voltage", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[31].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2170 UPS.Flow.[1].ConfigVoltage
        { "input.voltage.nominal", 0, 0, "UPS.Flow.[1].ConfigVoltage", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2171 UPS.PowerConverter.Input.[1].Current
        { "input.current", 0, 0, "UPS.PowerConverter.Input.[1].Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2172 UPS.PowerConverter.Input.[1].Phase.[1].Current
        { "input.L1.current", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[1].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2173 UPS.PowerConverter.Input.[1].Phase.[2].Current
        { "input.L2.current", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[2].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2174 UPS.PowerConverter.Input.[1].Phase.[3].Current
        { "input.L3.current", 0, 0, "UPS.PowerConverter.Input.[1].0xFFFF0010.[3].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2175 UPS.Flow.[1].ConfigCurrent
        { "input.current.nominal", 0, 0, "UPS.Flow.[1].ConfigCurrent", "%.2f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2176 UPS.PowerConverter.Input.[1].Frequency
        { "input.frequency", 0, 0, "UPS.PowerConverter.Input.[1].Frequency", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2177 UPS.Flow.[1].ConfigFrequency
        { "input.frequency.nominal", 0, 0, "UPS.Flow.[1].ConfigFrequency", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
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

        // --- Bypass page
        // mge-hid.c:2201 UPS.PowerConverter.Input.[2].Voltage
        { "input.bypass.voltage", 0, 0, "UPS.PowerConverter.Input.[2].Voltage", "%.1f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // mge-hid.c:2202 UPS.PowerConverter.Input.[2].Phase.[1].Voltage
        { "input.bypass.L1-N.voltage", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[1].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2203 UPS.PowerConverter.Input.[2].Phase.[2].Voltage
        { "input.bypass.L2-N.voltage", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[2].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2204 UPS.PowerConverter.Input.[2].Phase.[3].Voltage
        { "input.bypass.L3-N.voltage", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[3].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2205 UPS.PowerConverter.Input.[2].Phase.[12].Voltage
        { "input.bypass.L1-L2.voltage", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[12].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2206 UPS.PowerConverter.Input.[2].Phase.[23].Voltage
        { "input.bypass.L2-L3.voltage", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[23].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2207 UPS.PowerConverter.Input.[2].Phase.[31].Voltage
        { "input.bypass.L3-L1.voltage", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[31].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2208 UPS.Flow.[2].ConfigVoltage
        { "input.bypass.voltage.nominal", 0, 0, "UPS.Flow.[2].ConfigVoltage", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2209 UPS.PowerConverter.Input.[2].Current
        { "input.bypass.current", 0, 0, "UPS.PowerConverter.Input.[2].Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2210 UPS.PowerConverter.Input.[2].Phase.[1].Current
        { "input.bypass.L1.current", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[1].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2211 UPS.PowerConverter.Input.[2].Phase.[2].Current
        { "input.bypass.L2.current", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[2].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2212 UPS.PowerConverter.Input.[2].Phase.[3].Current
        { "input.bypass.L3.current", 0, 0, "UPS.PowerConverter.Input.[2].0xFFFF0010.[3].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2213 UPS.Flow.[2].ConfigCurrent
        { "input.bypass.current.nominal", 0, 0, "UPS.Flow.[2].ConfigCurrent", "%.2f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2214 UPS.PowerConverter.Input.[2].Frequency
        { "input.bypass.frequency", 0, 0, "UPS.PowerConverter.Input.[2].Frequency", "%.1f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // mge-hid.c:2215 UPS.Flow.[2].ConfigFrequency
        { "input.bypass.frequency.nominal", 0, 0, "UPS.Flow.[2].ConfigFrequency", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // input.bypass.switch.on (mge-hid.c:2218) excluded: stateful mge function, US-070
        // input.bypass.switch.off (mge-hid.c:2219) excluded: stateful mge function, US-070
        // mge-hid.c:2222 UPS.PowerConverter.Input.[2].Switchable
        { "input.bypass.switchable", ST_FLAG_RW | ST_FLAG_STRING, 8, "UPS.PowerConverter.Input.[2].Switchable", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kEnableDisable) },

        // --- Output page
        // mge-hid.c:2225 UPS.PowerConverter.Output.Voltage
        { "output.voltage", 0, 0, "UPS.PowerConverter.Output.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2226 UPS.PowerConverter.Output.Phase.[1].Voltage
        { "output.L1-N.voltage", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[1].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2227 UPS.PowerConverter.Output.Phase.[2].Voltage
        { "output.L2-N.voltage", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[2].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2228 UPS.PowerConverter.Output.Phase.[3].Voltage
        { "output.L3-N.voltage", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[3].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2229 UPS.PowerConverter.Output.Phase.[12].Voltage
        { "output.L1-L2.voltage", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[12].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2230 UPS.PowerConverter.Output.Phase.[23].Voltage
        { "output.L2-L3.voltage", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[23].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2231 UPS.PowerConverter.Output.Phase.[31].Voltage
        { "output.L3-L1.voltage", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[31].Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2232 UPS.Flow.[4].ConfigVoltage
        { "output.voltage.nominal", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.Flow.[4].ConfigVoltage", "%.0f", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, Conv::NominalVoltage, NUT_LKP(kNominalOutputVoltage) },
        // mge-hid.c:2233 UPS.PowerConverter.Output.Current
        { "output.current", 0, 0, "UPS.PowerConverter.Output.Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2234 UPS.PowerConverter.Output.Phase.[1].Current
        { "output.L1.current", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[1].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2235 UPS.PowerConverter.Output.Phase.[2].Current
        { "output.L2.current", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[2].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2236 UPS.PowerConverter.Output.Phase.[3].Current
        { "output.L3.current", 0, 0, "UPS.PowerConverter.Output.0xFFFF0010.[3].Current", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2237 UPS.Flow.[4].ConfigCurrent
        { "output.current.nominal", 0, 0, "UPS.Flow.[4].ConfigCurrent", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2238 UPS.PowerConverter.Output.Frequency
        { "output.frequency", 0, 0, "UPS.PowerConverter.Output.Frequency", "%.1f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2240 UPS.Flow.[4].ConfigFrequency
        { "output.frequency.nominal", 0, 0, "UPS.Flow.[4].ConfigFrequency", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2241 UPS.PowerConverter.Output.PowerFactor
        { "output.powerfactor", 0, 0, "UPS.PowerConverter.Output.0xFFFF0069", "%s", 0, Conv::MgePowerFactor, nullptr, 0 },

        // --- Outlet page (using MGE UPS SYSTEMS - PowerShare technology)
        // mge-hid.c:2246 UPS.OutletSystem.Outlet.[1].OutletID
        { "outlet.id", 0, 0, "UPS.OutletSystem.Outlet.[1].OutletID", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2247 UPS.OutletSystem.Outlet.[1].OutletID
        { "outlet.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[1].OutletID", "Main Outlet", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2248 UPS.OutletSystem.Outlet.[1].PresentStatus.Switchable
        { "outlet.switchable", 0, 0, "UPS.OutletSystem.Outlet.[1].PresentStatus.Switchable", "%s", HU_FLAG_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2252 UPS.OutletSystem.Outlet.[1].ConfigApparentPower
        { "outlet.power", ST_FLAG_RW | ST_FLAG_STRING, 6, "UPS.OutletSystem.Outlet.[1].ConfigApparentPower", "%s", HU_FLAG_SEMI_STATIC | HU_FLAG_ENUM, Conv::PegasusThreshold, NUT_LKP(kPegasusThreshold) },
        // mge-hid.c:2254 UPS.OutletSystem.Outlet.[1].ApparentPower
        { "outlet.power", 0, 0, "UPS.OutletSystem.Outlet.[1].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2255 UPS.OutletSystem.Outlet.[1].ActivePower
        { "outlet.realpower", 0, 0, "UPS.OutletSystem.Outlet.[1].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2256 UPS.OutletSystem.Outlet.[1].Current
        { "outlet.current", 0, 0, "UPS.OutletSystem.Outlet.[1].Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2257 UPS.OutletSystem.Outlet.[1].PowerFactor
        { "outlet.powerfactor", 0, 0, "UPS.OutletSystem.Outlet.[1].0xFFFF0069", "%.2f", 0, Conv::Number, nullptr, 0 },

        // --- First outlet (group) [2]
        // mge-hid.c:2260 UPS.OutletSystem.Outlet.[2].OutletID
        { "outlet.1.id", 0, 0, "UPS.OutletSystem.Outlet.[2].OutletID", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2261 UPS.OutletSystem.Outlet.[2].OutletID
        { "outlet.1.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[2].OutletID", "PowerShare Outlet 1", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2262 UPS.OutletSystem.Outlet.[2].PresentStatus.Switchable
        { "outlet.1.switchable", 0, 0, "UPS.OutletSystem.Outlet.[2].PresentStatus.Switchable", "%s", HU_FLAG_STATIC, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2264 UPS.OutletSystem.Outlet.[2].PresentStatus.SwitchOn/Off
        { "outlet.1.status", 0, 0, "UPS.OutletSystem.Outlet.[2].PresentStatus.SwitchOn/Off", "%s", 0, Conv::Lookup, NUT_LKP(kOnOff) },
        // mge-hid.c:2265 UPS.OutletSystem.Outlet.[2].Status
        { "outlet.1.protect.status", 0, 0, "UPS.OutletSystem.Outlet.[2].0xFFFF00E9", "%s", 0, Conv::Lookup, NUT_LKP(kOutletProtection) },
        // mge-hid.c:2266 UPS.OutletSystem.Outlet.[2].iDesignator
        { "outlet.1.designator", 0, 0, "UPS.OutletSystem.Outlet.[2].0xFFFF00BA", nullptr, HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // mge-hid.c:2268 UPS.PowerSummary.PresentStatus.ACPresent
        { "outlet.1.status", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", "%s", 0, Conv::Lookup, NUT_LKP(kOnOff) },
        // mge-hid.c:2270 UPS.OutletSystem.Outlet.[2].RemainingCapacityLimit
        { "outlet.1.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[2].RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2271 UPS.OutletSystem.Outlet.[2].ShutdownTimer
        { "outlet.1.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[2].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2272 UPS.OutletSystem.Outlet.[2].StartupTimer
        { "outlet.1.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[2].0xFFFF005E", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2273 UPS.OutletSystem.Outlet.[2].ApparentPower
        { "outlet.1.power", 0, 0, "UPS.OutletSystem.Outlet.[2].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2274 UPS.OutletSystem.Outlet.[2].ActivePower
        { "outlet.1.realpower", 0, 0, "UPS.OutletSystem.Outlet.[2].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2275 UPS.OutletSystem.Outlet.[2].Current
        { "outlet.1.current", 0, 0, "UPS.OutletSystem.Outlet.[2].Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2276 UPS.OutletSystem.Outlet.[2].PowerFactor
        { "outlet.1.powerfactor", 0, 0, "UPS.OutletSystem.Outlet.[2].0xFFFF0069", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2278 UPS.OutletSystem.Outlet.[2].ECOControl
        { "outlet.1.ecocontrol", 0, 0, "UPS.OutletSystem.Outlet.[2].0xFFFF0085", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kOutletEco) },

        // --- Second outlet (group) [3]
        // mge-hid.c:2281 UPS.OutletSystem.Outlet.[3].OutletID
        { "outlet.2.id", 0, 0, "UPS.OutletSystem.Outlet.[3].OutletID", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2282 UPS.OutletSystem.Outlet.[3].OutletID
        { "outlet.2.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[3].OutletID", "PowerShare Outlet 2", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2285 UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable
        { "outlet.2.switchable", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable", "%s", HU_FLAG_SEMI_STATIC, Conv::PegasusYesNo, NUT_LKP(kYesNo) },
        // mge-hid.c:2287 UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable
        { "outlet.2.switchable", 0, 0, "UPS.OutletSystem.Outlet.[3].PresentStatus.Switchable", "%s", 0, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2288 UPS.OutletSystem.Outlet.[3].PresentStatus.SwitchOn/Off
        { "outlet.2.status", 0, 0, "UPS.OutletSystem.Outlet.[3].PresentStatus.SwitchOn/Off", "%s", 0, Conv::Lookup, NUT_LKP(kOnOff) },
        // mge-hid.c:2289 UPS.OutletSystem.Outlet.[3].Status
        { "outlet.2.protect.status", 0, 0, "UPS.OutletSystem.Outlet.[3].0xFFFF00E9", "%s", 0, Conv::Lookup, NUT_LKP(kOutletProtection) },
        // mge-hid.c:2291 UPS.OutletSystem.Outlet.[3].RemainingCapacityLimit
        { "outlet.2.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[3].RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2292 UPS.OutletSystem.Outlet.[3].ShutdownTimer
        { "outlet.2.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[3].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2293 UPS.OutletSystem.Outlet.[3].StartupTimer
        { "outlet.2.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[3].0xFFFF005E", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2294 UPS.OutletSystem.Outlet.[3].ApparentPower
        { "outlet.2.power", 0, 0, "UPS.OutletSystem.Outlet.[3].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2295 UPS.OutletSystem.Outlet.[3].ActivePower
        { "outlet.2.realpower", 0, 0, "UPS.OutletSystem.Outlet.[3].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2296 UPS.OutletSystem.Outlet.[3].Current
        { "outlet.2.current", 0, 0, "UPS.OutletSystem.Outlet.[3].Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2297 UPS.OutletSystem.Outlet.[3].PowerFactor
        { "outlet.2.powerfactor", 0, 0, "UPS.OutletSystem.Outlet.[3].0xFFFF0069", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2299 UPS.OutletSystem.Outlet.[3].ECOControl
        { "outlet.2.ecocontrol", 0, 0, "UPS.OutletSystem.Outlet.[3].0xFFFF0085", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kOutletEco) },

        // --- Third outlet (group) [4]
        // mge-hid.c:2302 UPS.OutletSystem.Outlet.[4].OutletID
        { "outlet.3.id", 0, 0, "UPS.OutletSystem.Outlet.[4].OutletID", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2303 UPS.OutletSystem.Outlet.[4].OutletID
        { "outlet.3.desc", ST_FLAG_RW | ST_FLAG_STRING, 20, "UPS.OutletSystem.Outlet.[4].OutletID", "PowerShare Outlet 2", HU_FLAG_ABSENT, Conv::Number, nullptr, 0 },
        // mge-hid.c:2306 UPS.OutletSystem.Outlet.[4].PresentStatus.Switchable
        { "outlet.3.switchable", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[4].PresentStatus.Switchable", "%s", HU_FLAG_SEMI_STATIC, Conv::PegasusYesNo, NUT_LKP(kYesNo) },
        // mge-hid.c:2308 UPS.OutletSystem.Outlet.[4].PresentStatus.Switchable
        { "outlet.3.switchable", 0, 0, "UPS.OutletSystem.Outlet.[4].PresentStatus.Switchable", "%s", 0, Conv::Lookup, NUT_LKP(kYesNo) },
        // mge-hid.c:2309 UPS.OutletSystem.Outlet.[4].PresentStatus.SwitchOn/Off
        { "outlet.3.status", 0, 0, "UPS.OutletSystem.Outlet.[4].PresentStatus.SwitchOn/Off", "%s", 0, Conv::Lookup, NUT_LKP(kOnOff) },
        // mge-hid.c:2310 UPS.OutletSystem.Outlet.[4].Status
        { "outlet.3.protect.status", 0, 0, "UPS.OutletSystem.Outlet.[4].0xFFFF00E9", "%s", 0, Conv::Lookup, NUT_LKP(kOutletProtection) },
        // mge-hid.c:2312 UPS.OutletSystem.Outlet.[4].RemainingCapacityLimit
        { "outlet.3.autoswitch.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 3, "UPS.OutletSystem.Outlet.[4].RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2313 UPS.OutletSystem.Outlet.[4].ShutdownTimer
        { "outlet.3.delay.shutdown", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[4].0xFFFF005D", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2314 UPS.OutletSystem.Outlet.[4].StartupTimer
        { "outlet.3.delay.start", ST_FLAG_RW | ST_FLAG_STRING, 5, "UPS.OutletSystem.Outlet.[4].0xFFFF005E", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // mge-hid.c:2315 UPS.OutletSystem.Outlet.[4].ApparentPower
        { "outlet.3.power", 0, 0, "UPS.OutletSystem.Outlet.[4].ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2316 UPS.OutletSystem.Outlet.[4].ActivePower
        { "outlet.3.realpower", 0, 0, "UPS.OutletSystem.Outlet.[4].ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2317 UPS.OutletSystem.Outlet.[4].Current
        { "outlet.3.current", 0, 0, "UPS.OutletSystem.Outlet.[4].Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2318 UPS.OutletSystem.Outlet.[4].PowerFactor
        { "outlet.3.powerfactor", 0, 0, "UPS.OutletSystem.Outlet.[4].0xFFFF0069", "%.2f", 0, Conv::Number, nullptr, 0 },
        // mge-hid.c:2320 UPS.OutletSystem.Outlet.[4].ECOControl
        { "outlet.3.ecocontrol", 0, 0, "UPS.OutletSystem.Outlet.[4].0xFFFF0085", "%s", HU_FLAG_SEMI_STATIC, Conv::Lookup, NUT_LKP(kOutletEco) },

        // --- instant commands.
        // mge-hid.c:2326 UPS.BatterySystem.Battery.Test
        { "test.battery.start.quick", 0, 0, "UPS.BatterySystem.Battery.Test", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2327 UPS.BatterySystem.Battery.Test
        { "test.battery.start.deep", 0, 0, "UPS.BatterySystem.Battery.Test", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2328 UPS.BatterySystem.Battery.Test
        { "test.battery.stop", 0, 0, "UPS.BatterySystem.Battery.Test", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2329 UPS.PowerSummary.DelayBeforeShutdown
        { "load.off.delay", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "20", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2330 UPS.PowerSummary.DelayBeforeStartup
        { "load.on.delay", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", "30", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2331 UPS.PowerSummary.DelayBeforeShutdown
        { "shutdown.stop", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "-1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2332 UPS.PowerSummary.DelayBeforeReboot
        { "shutdown.reboot", 0, 0, "UPS.PowerSummary.DelayBeforeReboot", "10", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2333 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.off", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2334 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.on", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2337 UPS.BatterySystem.Battery.AudibleAlarmControl
        { "beeper.mute", 0, 0, "UPS.BatterySystem.Battery.AudibleAlarmControl", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2338 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.mute", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2339 UPS.BatterySystem.Battery.AudibleAlarmControl
        { "beeper.disable", 0, 0, "UPS.BatterySystem.Battery.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2340 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.disable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2341 UPS.BatterySystem.Battery.AudibleAlarmControl
        { "beeper.enable", 0, 0, "UPS.BatterySystem.Battery.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2342 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.enable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2343 UPS.AudibleAlarmControl
        { "beeper.disable", 0, 0, "UPS.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2344 UPS.AudibleAlarmControl
        { "beeper.enable", 0, 0, "UPS.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },

        // --- Command for the outlet collection
        // mge-hid.c:2347 UPS.OutletSystem.Outlet.[2].DelayBeforeShutdown
        { "outlet.1.load.off", 0, 0, "UPS.OutletSystem.Outlet.[2].DelayBeforeShutdown", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2348 UPS.OutletSystem.Outlet.[2].DelayBeforeStartup
        { "outlet.1.load.on", 0, 0, "UPS.OutletSystem.Outlet.[2].DelayBeforeStartup", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2349 UPS.OutletSystem.Outlet.[3].DelayBeforeShutdown
        { "outlet.2.load.off", 0, 0, "UPS.OutletSystem.Outlet.[3].DelayBeforeShutdown", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2350 UPS.OutletSystem.Outlet.[3].DelayBeforeStartup
        { "outlet.2.load.on", 0, 0, "UPS.OutletSystem.Outlet.[3].DelayBeforeStartup", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2351 UPS.OutletSystem.Outlet.[4].DelayBeforeShutdown
        { "outlet.3.load.off", 0, 0, "UPS.OutletSystem.Outlet.[4].DelayBeforeShutdown", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2352 UPS.OutletSystem.Outlet.[4].DelayBeforeStartup
        { "outlet.3.load.on", 0, 0, "UPS.OutletSystem.Outlet.[4].DelayBeforeStartup", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },

        // --- Command to switch ECO(HE), ESS Mode
        // mge-hid.c:2355 UPS.PowerConverter.Input.[5].Switchable
        { "experimental.ecomode.stop", 0, 0, "UPS.PowerConverter.Input.[5].Switchable", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2356 UPS.PowerConverter.Input.[5].Switchable
        { "experimental.ecomode.start", 0, 0, "UPS.PowerConverter.Input.[5].Switchable", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2357 UPS.PowerConverter.Input.[5].Switchable
        { "experimental.essmode.start", 0, 0, "UPS.PowerConverter.Input.[5].Switchable", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2358 UPS.PowerConverter.Input.[5].Switchable
        { "experimental.essmode.stop", 0, 0, "UPS.PowerConverter.Input.[5].Switchable", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },

        // --- Command to switch ECO(HE) Mode with switch to Automatic Bypass Mode on before
        // experimental.bypass.ecomode.start (mge-hid.c:2361) excluded: stateful mge function, US-070

        // --- Command to switch from ECO(HE) Mode with switch from Automatic Bypass Mode on before
        // experimental.bypass.ecomode.stop (mge-hid.c:2363) excluded: stateful mge function, US-070

        // --- Command to switch Automatic Bypass Mode on/off
        // mge-hid.c:2366 UPS.PowerConverter.Input.[2].SwitchOnControl
        { "bypass.start", 0, 0, "UPS.PowerConverter.Input.[2].SwitchOnControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // mge-hid.c:2367 UPS.PowerConverter.Input.[2].SwitchOffControl
        { "bypass.stop", 0, 0, "UPS.PowerConverter.Input.[2].SwitchOffControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // input.eco.switchable (mge-hid.c:2373) excluded: stateful mge function, US-070
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

// Every row of apc_hid2nut (US-076, ADR 0011): values, status and instant commands. HIDParser
// names APCBattReplaceDate; the other vendor usages of apc_usage_lkp have hex paths:
// APCGeneralCollection 0xFF860005, APCEnvironment 0xFF860006, APCProbe1 0xFF860007,
// APCLineFailCause 0xFF860052, APCStatusFlag 0xFF860060, APCSensitivity 0xFF860061,
// APCPanelTest 0xFF860072, APCDelayBeforeReboot/Shutdown/Startup 0xFF86007C/7D/7E.
inline const HidInfo* apcHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // Battery page
        // apc-hid.c:334 UPS.PowerSummary.RemainingCapacity
        { "battery.charge", 0, 0, "UPS.PowerSummary.RemainingCapacity", "%.0f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:335 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:336 UPS.PowerSummary.WarningCapacityLimit
        { "battery.charge.warning", 0, 0, "UPS.PowerSummary.WarningCapacityLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:337 UPS.Battery.RunTimeToEmpty
        { "battery.runtime", 0, 0, "UPS.Battery.RunTimeToEmpty", "%.0f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:338 UPS.PowerSummary.RunTimeToEmpty
        { "battery.runtime", 0, 0, "UPS.PowerSummary.RunTimeToEmpty", "%.0f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:339 UPS.Battery.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Battery.RemainingTimeLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:340 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // apc-hid.c:341 UPS.Battery.Voltage
        { "battery.voltage", 0, 0, "UPS.Battery.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:342 UPS.PowerSummary.Voltage
        { "battery.voltage", 0, 0, "UPS.PowerSummary.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:343 UPS.Battery.ConfigVoltage
        { "battery.voltage.nominal", 0, 0, "UPS.Battery.ConfigVoltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:344 UPS.PowerSummary.ConfigVoltage
        { "battery.voltage.nominal", 0, 0, "UPS.PowerSummary.ConfigVoltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:345 UPS.Battery.Temperature
        { "battery.temperature", 0, 0, "UPS.Battery.Temperature", "%s", 0, Conv::KelvinCelsius, nullptr, 0 },
        // apc-hid.c:346 UPS.PowerSummary.iDeviceChemistry
        { "battery.type", 0, 0, "UPS.PowerSummary.iDeviceChemistry", "%s", 0, Conv::StringId, nullptr, 0 },
        // apc-hid.c:347 UPS.Battery.ManufacturerDate
        { "battery.mfr.date", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Battery.ManufacturerDate", "%s", HU_FLAG_SEMI_STATIC, Conv::BatteryDate, nullptr, 0 },
        // apc-hid.c:348 UPS.PowerSummary.APCBattReplaceDate
        { "battery.mfr.date", 0, 0, "UPS.PowerSummary.APCBattReplaceDate", "%s", 0, Conv::ApcDate, nullptr, 0 },
        // apc-hid.c:349 UPS.Battery.APCBattReplaceDate
        { "battery.date", 0, 0, "UPS.Battery.APCBattReplaceDate", "%s", 0, Conv::ApcDate, nullptr, 0 },

        // UPS page
        // apc-hid.c:352 UPS.Output.PercentLoad
        { "ups.load", 0, 0, "UPS.Output.PercentLoad", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:353 UPS.PowerConverter.PercentLoad
        { "ups.load", 0, 0, "UPS.PowerConverter.PercentLoad", "%.0f", 0, Conv::Number, nullptr, 0 },
        // ups.delay.start (apc-hid.c:355) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (apc-hid.c:356) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // apc-hid.c:357 UPS.PowerSummary.DelayBeforeStartup
        { "ups.timer.start", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:358 UPS.PowerSummary.DelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:359 UPS.PowerSummary.DelayBeforeReboot
        { "ups.timer.reboot", 0, 0, "UPS.PowerSummary.DelayBeforeReboot", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // ups.delay.start (apc-hid.c:361) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (apc-hid.c:362) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // apc-hid.c:363 UPS.Output.DelayBeforeStartup
        { "ups.timer.start", 0, 0, "UPS.Output.DelayBeforeStartup", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:364 UPS.Output.DelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.Output.DelayBeforeShutdown", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:365 UPS.Output.DelayBeforeReboot
        { "ups.timer.reboot", 0, 0, "UPS.Output.DelayBeforeReboot", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // ups.delay.start (apc-hid.c:367) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (apc-hid.c:368) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // apc-hid.c:369 UPS.APCGeneralCollection.APCDelayBeforeStartup
        { "ups.timer.start", 0, 0, "UPS.0xFF860005.0xFF86007E", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:370 UPS.APCGeneralCollection.APCDelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.0xFF860005.0xFF86007D", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:371 UPS.APCGeneralCollection.APCDelayBeforeReboot
        { "ups.timer.reboot", 0, 0, "UPS.0xFF860005.0xFF86007C", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // apc-hid.c:372 UPS.Battery.Test
        { "ups.test.result", 0, 0, "UPS.Battery.Test", "%s", 0, Conv::Lookup, NUT_LKP(kTestRead) },
        // apc-hid.c:373 UPS.PowerSummary.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "%s", 0, Conv::Lookup, NUT_LKP(kBeeperInfo) },
        // apc-hid.c:374 UPS.ManufacturerDate
        { "ups.mfr.date", 0, 0, "UPS.ManufacturerDate", "%s", 0, Conv::BatteryDate, nullptr, 0 },
        // apc-hid.c:375 UPS.PowerSummary.ManufacturerDate
        { "ups.mfr.date", 0, 0, "UPS.PowerSummary.ManufacturerDate", "%s", 0, Conv::BatteryDate, nullptr, 0 },
        // apc-hid.c:376 UPS.PowerConverter.ConfigActivePower
        { "ups.realpower.nominal", 0, 0, "UPS.PowerConverter.ConfigActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:377 UPS.Output.ConfigActivePower
        { "ups.realpower.nominal", 0, 0, "UPS.Output.ConfigActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },

        // Special case: ups.status & ups.alarm
        // apc-hid.c:384 UPS.PowerSummary.PresentStatus.ACPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kOnlineInfo) },
        // apc-hid.c:385 UPS.PowerSummary.PresentStatus.Discharging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Discharging", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kDischargingInfo) },
        // apc-hid.c:386 UPS.PowerSummary.PresentStatus.Charging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Charging", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kChargingInfo) },
        // apc-hid.c:387 UPS.PowerSummary.PresentStatus.ShutdownImminent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ShutdownImminent", nullptr, 0, Conv::Lookup, NUT_LKP(kShutdownimmInfo) },
        // apc-hid.c:388 UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kLowbattInfo) },
        // apc-hid.c:389 UPS.PowerSummary.PresentStatus.Overload
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Overload", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },
        // apc-hid.c:390 UPS.PowerSummary.PresentStatus.NeedReplacement
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.NeedReplacement", nullptr, 0, Conv::Lookup, NUT_LKP(kReplacebattInfo) },
        // apc-hid.c:391 UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired", nullptr, 0, Conv::Lookup, NUT_LKP(kTimelimitexpiredInfo) },
        // apc-hid.c:392 UPS.PowerSummary.PresentStatus.BatteryPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BatteryPresent", nullptr, 0, Conv::Lookup, NUT_LKP(kNobatteryInfo) },
        // apc-hid.c:394 UPS.PowerSummary.Charging
        { "BOOL", 0, 0, "UPS.PowerSummary.Charging", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kChargingInfo) },
        // apc-hid.c:395 UPS.PowerSummary.Discharging
        { "BOOL", 0, 0, "UPS.PowerSummary.Discharging", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kDischargingInfo) },
        // apc-hid.c:396 UPS.PowerSummary.ACPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.ACPresent", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kOnlineInfo) },
        // apc-hid.c:397 UPS.PowerSummary.BelowRemainingCapacityLimit
        { "BOOL", 0, 0, "UPS.PowerSummary.BelowRemainingCapacityLimit", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kLowbattInfo) },
        // apc-hid.c:398 UPS.PowerSummary.ShutdownImminent
        { "BOOL", 0, 0, "UPS.PowerSummary.ShutdownImminent", nullptr, 0, Conv::Lookup, NUT_LKP(kShutdownimmInfo) },
        // apc-hid.c:399 UPS.PowerSummary.APCStatusFlag
        { "BOOL", 0, 0, "UPS.PowerSummary.0xFF860060", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kApcStatusFlag) },
        // apc-hid.c:403 UPS.Input.APCLineFailCause
        { "BOOL", 0, 0, "UPS.Input.0xFF860052", nullptr, 0, Conv::Lookup, NUT_LKP(kApcLineFailCauseVrange) },
        // apc-hid.c:404 UPS.Input.APCLineFailCause
        { "BOOL", 0, 0, "UPS.Input.0xFF860052", nullptr, 0, Conv::Lookup, NUT_LKP(kApcLineFailCauseFrange) },

        // Input page
        // apc-hid.c:407 UPS.Input.Voltage
        { "input.voltage", 0, 0, "UPS.Input.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:408 UPS.Input.ConfigVoltage
        { "input.voltage.nominal", 0, 0, "UPS.Input.ConfigVoltage", "%.0f", 0, Conv::Number, nullptr, 0 },
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

        // Output page
        // apc-hid.c:417 UPS.Output.Voltage
        { "output.voltage", 0, 0, "UPS.Output.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:418 UPS.Output.ConfigVoltage
        { "output.voltage.nominal", 0, 0, "UPS.Output.ConfigVoltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:419 UPS.Output.Current
        { "output.current", 0, 0, "UPS.Output.Current", "%.2f", 0, Conv::Number, nullptr, 0 },
        // apc-hid.c:420 UPS.Output.Frequency
        { "output.frequency", 0, 0, "UPS.Output.Frequency", "%.1f", 0, Conv::Number, nullptr, 0 },

        // Environmental page
        // apc-hid.c:423 UPS.APCEnvironment.APCProbe1.Temperature
        { "ambient.temperature", 0, 0, "UPS.0xFF860006.0xFF860007.Temperature", "%s", 0, Conv::KelvinCelsius, nullptr, 0 },
        // apc-hid.c:424 UPS.APCEnvironment.APCProbe1.Humidity
        { "ambient.humidity", 0, 0, "UPS.0xFF860006.0xFF860007.Humidity", "%.1f", 0, Conv::Number, nullptr, 0 },
        // ambient.temperature / ambient.humidity of APCProbe2 (apc-hid.c:426-427) are commented out upstream

        // Instant commands (read by CommandCatalog, US-074)
        // apc-hid.c:434 UPS.BatterySystem.Battery.Test
        { "test.battery.start.quick", 0, 0, "UPS.BatterySystem.Battery.Test", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:435 UPS.Battery.Test
        { "test.battery.start.quick", 0, 0, "UPS.Battery.Test", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:436 UPS.BatterySystem.Battery.Test
        { "test.battery.start.deep", 0, 0, "UPS.BatterySystem.Battery.Test", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:437 UPS.Battery.Test
        { "test.battery.start.deep", 0, 0, "UPS.Battery.Test", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:438 UPS.BatterySystem.Battery.Test
        { "test.battery.stop", 0, 0, "UPS.BatterySystem.Battery.Test", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:439 UPS.Battery.Test
        { "test.battery.stop", 0, 0, "UPS.Battery.Test", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:440 UPS.APCPanelTest
        { "test.panel.start", 0, 0, "UPS.0xFF860072", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:441 UPS.APCPanelTest
        { "test.panel.stop", 0, 0, "UPS.0xFF860072", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:442 UPS.PowerSummary.APCPanelTest
        { "test.panel.start", 0, 0, "UPS.PowerSummary.0xFF860072", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:443 UPS.PowerSummary.APCPanelTest
        { "test.panel.stop", 0, 0, "UPS.PowerSummary.0xFF860072", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:446 UPS.PowerSummary.DelayBeforeShutdown, DEFAULT_OFFDELAY (usbhid-ups.h)
        { "load.off.delay", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "20", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:447 UPS.PowerSummary.DelayBeforeStartup, DEFAULT_ONDELAY (usbhid-ups.h)
        { "load.on.delay", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", "30", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:448 UPS.PowerSummary.DelayBeforeShutdown
        { "shutdown.stop", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "-1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:449 UPS.PowerSummary.DelayBeforeReboot
        { "shutdown.reboot", 0, 0, "UPS.PowerSummary.DelayBeforeReboot", "10", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:451 UPS.Output.DelayBeforeShutdown, DEFAULT_OFFDELAY (usbhid-ups.h)
        { "load.off.delay", 0, 0, "UPS.Output.DelayBeforeShutdown", "20", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:452 UPS.Output.DelayBeforeStartup, DEFAULT_ONDELAY (usbhid-ups.h)
        { "load.on.delay", 0, 0, "UPS.Output.DelayBeforeStartup", "30", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:453 UPS.Output.DelayBeforeShutdown
        { "shutdown.stop", 0, 0, "UPS.Output.DelayBeforeShutdown", "-1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:454 UPS.Output.DelayBeforeReboot
        { "shutdown.reboot", 0, 0, "UPS.Output.DelayBeforeReboot", "10", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:456 UPS.APCGeneralCollection.APCDelayBeforeShutdown, DEFAULT_OFFDELAY (usbhid-ups.h)
        { "load.off.delay", 0, 0, "UPS.0xFF860005.0xFF86007D", "20", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:457 UPS.APCGeneralCollection.APCDelayBeforeStartup, DEFAULT_ONDELAY (usbhid-ups.h)
        { "load.on.delay", 0, 0, "UPS.0xFF860005.0xFF86007E", "30", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:458 UPS.APCGeneralCollection.APCDelayBeforeShutdown
        { "shutdown.stop", 0, 0, "UPS.0xFF860005.0xFF86007D", "-1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:459 UPS.APCGeneralCollection.APCDelayBeforeReboot
        { "shutdown.reboot", 0, 0, "UPS.0xFF860005.0xFF86007C", "10", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:461 UPS.Output.APCDelayBeforeReboot
        { "shutdown.return", 0, 0, "UPS.Output.0xFF86007C", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:463 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.on", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:464 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.off", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:465 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.enable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:466 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.disable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // apc-hid.c:467 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.mute", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

// Every row of cps_hid2nut (US-075, ADR 0011): values, status and instant commands. Vendor
// usages as cps_usage_lkp names them: CPSFirmwareVersion 0xFF0100D0, CPSInputSensitivity
// 0xFF010043.
inline const HidInfo* cpsHid2Nut(size_t& n) {
    static const HidInfo t[] = {
#if WITH_UNMAPPED_DATA_POINTS
        // cps-hid.c:358 UPS.PowerSummary.Rechargeable
        { "unmapped.ups.powersummary.rechargeable", 0, 0, "UPS.PowerSummary.Rechargeable", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:359 UPS.PowerSummary.CapacityMode
        { "unmapped.ups.powersummary.capacitymode", 0, 0, "UPS.PowerSummary.CapacityMode", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:360 UPS.PowerSummary.DesignCapacity
        { "unmapped.ups.powersummary.designcapacity", 0, 0, "UPS.PowerSummary.DesignCapacity", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:361 UPS.PowerSummary.CapacityGranularity1
        { "unmapped.ups.powersummary.capacitygranularity1", 0, 0, "UPS.PowerSummary.CapacityGranularity1", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:362 UPS.PowerSummary.CapacityGranularity2
        { "unmapped.ups.powersummary.capacitygranularity2", 0, 0, "UPS.PowerSummary.CapacityGranularity2", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif

        // Battery page
        // cps-hid.c:366 UPS.PowerSummary.iDeviceChemistry
        { "battery.type", 0, 0, "UPS.PowerSummary.iDeviceChemistry", "%s", 0, Conv::StringId, nullptr, 0 },
        // cps-hid.c:367 UPS.Battery.ManufacturerDate
        { "battery.mfr.date", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.Battery.ManufacturerDate", "%s", HU_FLAG_SEMI_STATIC, Conv::BatteryDate, nullptr, 0 },
        // cps-hid.c:368 UPS.PowerSummary.iOEMInformation
        { "battery.mfr.date", 0, 0, "UPS.PowerSummary.iOEMInformation", "%s", 0, Conv::StringId, nullptr, 0 },
        // cps-hid.c:369 UPS.PowerSummary.WarningCapacityLimit
        { "battery.charge.warning", 0, 0, "UPS.PowerSummary.WarningCapacityLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:370 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:371 UPS.PowerSummary.RemainingCapacity
        { "battery.charge", 0, 0, "UPS.PowerSummary.RemainingCapacity", "%s", 0, Conv::CpsBattCharge, nullptr, 0 },
        // cps-hid.c:372 UPS.PowerSummary.RunTimeToEmpty
        { "battery.runtime", 0, 0, "UPS.PowerSummary.RunTimeToEmpty", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:373 UPS.PowerSummary.RemainingTimeLimit
        { "battery.runtime.low", ST_FLAG_RW | ST_FLAG_STRING, 10, "UPS.PowerSummary.RemainingTimeLimit", "%.0f", HU_FLAG_SEMI_STATIC, Conv::Number, nullptr, 0 },
        // cps-hid.c:374 UPS.PowerSummary.ConfigVoltage
        { "battery.voltage.nominal", 0, 0, "UPS.PowerSummary.ConfigVoltage", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:375 UPS.PowerSummary.Voltage
        { "battery.voltage", 0, 0, "UPS.PowerSummary.Voltage", "%s", 0, Conv::CpsBattVolt, nullptr, 0 },
        // cps-hid.c:376 UPS.PowerSummary.FullChargeCapacity
        { "battery.status", 0, 0, "UPS.PowerSummary.FullChargeCapacity", "%s", 0, Conv::CpsBattStatus, nullptr, 0 },

        // UPS page
        // cps-hid.c:379 UPS.Output.PercentLoad
        { "ups.load", 0, 0, "UPS.Output.PercentLoad", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:380 UPS.PowerSummary.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "%s", 0, Conv::Lookup, NUT_LKP(kBeeperInfo) },
        // cps-hid.c:381 UPS.Output.Test
        { "ups.test.result", 0, 0, "UPS.Output.Test", "%s", 0, Conv::Lookup, NUT_LKP(kTestRead) },
        // cps-hid.c:382 UPS.Output.ApparentPower
        { "ups.power", 0, 0, "UPS.Output.ApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:383 UPS.Output.ConfigApparentPower
        { "ups.power.nominal", 0, 0, "UPS.Output.ConfigApparentPower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:384 UPS.Output.ActivePower
        { "ups.realpower", 0, 0, "UPS.Output.ActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:385 UPS.Output.ConfigActivePower
        { "ups.realpower.nominal", 0, 0, "UPS.Output.ConfigActivePower", "%.0f", 0, Conv::Number, nullptr, 0 },
        // ups.delay.start (cps-hid.c:386) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // ups.delay.shutdown (cps-hid.c:387) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays
        // cps-hid.c:388 UPS.Output.DelayBeforeStartup
        { "ups.timer.start", 0, 0, "UPS.Output.DelayBeforeStartup", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // cps-hid.c:389 UPS.Output.DelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.Output.DelayBeforeShutdown", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // cps-hid.c:390 UPS.Output.DelayBeforeReboot
        { "ups.timer.reboot", 0, 0, "UPS.Output.DelayBeforeReboot", "%.0f", HU_FLAG_QUICK_POLL, Conv::Number, nullptr, 0 },
        // cps-hid.c:391 UPS.PowerSummary.CPSFirmwareVersion
        { "ups.firmware", 0, 0, "UPS.PowerSummary.0xFF0100D0", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // cps-hid.c:392 UPS.PowerSummary.Temperature
        { "ups.temperature", 0, 0, "UPS.PowerSummary.Temperature", "%s", 0, Conv::KelvinCelsius, nullptr, 0 },

        // Special case: ups.status & ups.alarm
        // cps-hid.c:395 UPS.PowerSummary.PresentStatus.ACPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kOnlineInfo) },
        // cps-hid.c:396 UPS.PowerSummary.PresentStatus.Charging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Charging", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kChargingInfo) },
        // cps-hid.c:397 UPS.PowerSummary.PresentStatus.Discharging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Discharging", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kDischargingInfo) },
        // cps-hid.c:398 UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", nullptr, HU_FLAG_QUICK_POLL, Conv::Lookup, NUT_LKP(kLowbattInfo) },
        // cps-hid.c:399 UPS.PowerSummary.PresentStatus.FullyCharged
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.FullyCharged", nullptr, 0, Conv::Lookup, NUT_LKP(kFullychargedInfo) },
        // cps-hid.c:400 UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired", nullptr, 0, Conv::Lookup, NUT_LKP(kTimelimitexpiredInfo) },
        // cps-hid.c:401 UPS.PowerSummary.PresentStatus.NeedReplacement
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.NeedReplacement", nullptr, 0, Conv::Lookup, NUT_LKP(kReplacebattInfo) },
        // cps-hid.c:402 UPS.Output.Boost
        { "BOOL", 0, 0, "UPS.Output.Boost", nullptr, 0, Conv::Lookup, NUT_LKP(kBoostInfo) },
        // cps-hid.c:403 UPS.Output.Overload
        { "BOOL", 0, 0, "UPS.Output.Overload", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },

        // Input page
        // cps-hid.c:408 UPS.Input.Frequency
        { "input.frequency", 0, 0, "UPS.Input.Frequency", "%.1f", 0, Conv::CpsInputFreq, nullptr, 0 },
        // cps-hid.c:409 UPS.Input.ConfigVoltage
        { "input.voltage.nominal", 0, 0, "UPS.Input.ConfigVoltage", "%.0f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:410 UPS.Input.Voltage
        { "input.voltage", 0, 0, "UPS.Input.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
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

        // Output page
        // cps-hid.c:421 UPS.Output.Frequency
        { "output.frequency", 0, 0, "UPS.Output.Frequency", "%.1f", 0, Conv::CpsOutputFreq, nullptr, 0 },
        // cps-hid.c:422 UPS.Output.Voltage
        { "output.voltage", 0, 0, "UPS.Output.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // cps-hid.c:423 UPS.Output.ConfigVoltage
        { "output.voltage.nominal", 0, 0, "UPS.Output.ConfigVoltage", "%.0f", 0, Conv::Number, nullptr, 0 },

        // Instant commands (read by CommandCatalog since US-074): beeper.on/off are not in
        // UpsCommands.h, never listed
        // cps-hid.c:426 UPS.Output.Test
        { "test.battery.start.quick", 0, 0, "UPS.Output.Test", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:427 UPS.Output.Test
        { "test.battery.start.deep", 0, 0, "UPS.Output.Test", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:428 UPS.Output.Test
        { "test.battery.stop", 0, 0, "UPS.Output.Test", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:429 UPS.Output.DelayBeforeShutdown, DEFAULT_OFFDELAY_CPS (usbhid-ups.h:66-67)
        { "load.off.delay", 0, 0, "UPS.Output.DelayBeforeShutdown", "60", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:430 UPS.Output.DelayBeforeStartup, DEFAULT_ONDELAY_CPS (usbhid-ups.h:66-67)
        { "load.on.delay", 0, 0, "UPS.Output.DelayBeforeStartup", "120", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:431 UPS.Output.DelayBeforeShutdown
        { "shutdown.stop", 0, 0, "UPS.Output.DelayBeforeShutdown", "-1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:432 UPS.Output.DelayBeforeReboot
        { "shutdown.reboot", 0, 0, "UPS.Output.DelayBeforeReboot", "10", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:433 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.on", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:434 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.off", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:435 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.enable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "2", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:436 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.disable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // cps-hid.c:437 UPS.PowerSummary.AudibleAlarmControl
        { "beeper.mute", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "3", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
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

// Every row of powercom_hid2nut (US-077, ADR 0011): values, status and instant commands.
// Vendor usages as powercom_usage_lkp names them; the application collection is
// PowercomUPS itself, so those paths start with its code, not with "UPS.":
// PowercomUPS 0x00020004, PowercomBatterySystem 0x00020010, PowercomPowerConverter 0x00020016,
// PowercomInput 0x0002001A, PowercomOutput 0x0002001C, PowercomVoltage 0x00020030,
// PowercomFrequency 0x00020032, PowercomPercentLoad 0x00020035, PowercomTemperature 0x00020036,
// PowercomDelayBeforeStartup 0x00020056, PowercomDelayBeforeShutdown 0x00020057, PowercomTest
// 0x00020058, PowercomShutdownRequested 0x00020068, PowercomInternalChargeController 0x00020081,
// PowercomPrimaryBatterySupport 0x00020082, PowercomDesignCapacity 0x00020083,
// PowercomSpecificationInfo 0x00020084, PowercomManufacturerDate 0x00020085,
// PowercomSerialNumber 0x00020086, PowercomManufacturerName 0x00020087,
// PCMDelayBeforeStartup 0x00FF0056, PCMDelayBeforeShutdown 0x00FF0057, POWERCOM1 0x0084002F,
// POWERCOM2 0xFF860060, POWERCOM3 0xFF860080.
inline const HidInfo* powercomHid2Nut(size_t& n) {
    static const HidInfo t[] = {
        // powercom-hid.c:544 UPS.PowerSummary.PresentStatus.ACPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", nullptr, 0, Conv::Lookup, NUT_LKP(kOnlineInfo) },
        // powercom-hid.c:545 UPS.PowerSummary.PresentStatus.BatteryPresent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BatteryPresent", nullptr, 0, Conv::Lookup, NUT_LKP(kNobatteryInfo) },
        // powercom-hid.c:546 UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", nullptr, 0, Conv::Lookup, NUT_LKP(kLowbattInfo) },
        // powercom-hid.c:547 UPS.PowerSummary.PresentStatus.Charging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Charging", nullptr, 0, Conv::Lookup, NUT_LKP(kChargingInfo) },
        // powercom-hid.c:548 UPS.PowerSummary.PresentStatus.CommunicationLost
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.CommunicationLost", nullptr, 0, Conv::Lookup, NUT_LKP(kCommfaultInfo) },
        // powercom-hid.c:549 UPS.PowerSummary.PresentStatus.Discharging
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Discharging", nullptr, 0, Conv::Lookup, NUT_LKP(kDischargingInfo) },
        // powercom-hid.c:550 UPS.PowerSummary.PresentStatus.NeedReplacement
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.NeedReplacement", nullptr, 0, Conv::Lookup, NUT_LKP(kReplacebattInfo) },
        // powercom-hid.c:551 UPS.PowerSummary.PresentStatus.Overload
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.Overload", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },
        // powercom-hid.c:552 UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired", nullptr, 0, Conv::Lookup, NUT_LKP(kTimelimitexpiredInfo) },
        // powercom-hid.c:553 UPS.PowerSummary.PresentStatus.ShutdownImminent
        { "BOOL", 0, 0, "UPS.PowerSummary.PresentStatus.ShutdownImminent", nullptr, 0, Conv::Lookup, NUT_LKP(kShutdownimmInfo) },
        // powercom-hid.c:554 UPS.PowerSummary.PresentStatus.POWERCOM3 (BOOL): commented out upstream
        // powercom-hid.c:555 UPS.PowerSummary.PresentStatus.ShutdownRequested (BOOL): commented out upstream
        // powercom-hid.c:556 UPS.PowerSummary.PresentStatus.VoltageNotRegulated (BOOL): commented out upstream
        // powercom-hid.c:557 UPS.PresentStatus.ACPresent
        { "BOOL", 0, 0, "UPS.PresentStatus.ACPresent", nullptr, 0, Conv::Lookup, NUT_LKP(kOnlineInfo) },
        // powercom-hid.c:558 UPS.PresentStatus.BatteryPresent
        { "BOOL", 0, 0, "UPS.PresentStatus.BatteryPresent", nullptr, 0, Conv::Lookup, NUT_LKP(kNobatteryInfo) },
        // powercom-hid.c:559 UPS.PresentStatus.BelowRemainingCapacityLimit
        { "BOOL", 0, 0, "UPS.PresentStatus.BelowRemainingCapacityLimit", nullptr, 0, Conv::Lookup, NUT_LKP(kLowbattInfo) },
        // powercom-hid.c:560 UPS.PresentStatus.Boost
        { "BOOL", 0, 0, "UPS.PresentStatus.Boost", nullptr, 0, Conv::Lookup, NUT_LKP(kBoostInfo) },
        // powercom-hid.c:561 UPS.PresentStatus.Buck
        { "BOOL", 0, 0, "UPS.PresentStatus.Buck", nullptr, 0, Conv::Lookup, NUT_LKP(kTrimInfo) },
        // powercom-hid.c:562 UPS.PresentStatus.Charging
        { "BOOL", 0, 0, "UPS.PresentStatus.Charging", nullptr, 0, Conv::Lookup, NUT_LKP(kChargingInfo) },
        // powercom-hid.c:563 UPS.PresentStatus.CommunicationLost
        { "BOOL", 0, 0, "UPS.PresentStatus.CommunicationLost", nullptr, 0, Conv::Lookup, NUT_LKP(kCommfaultInfo) },
        // powercom-hid.c:564 UPS.PresentStatus.Discharging
        { "BOOL", 0, 0, "UPS.PresentStatus.Discharging", nullptr, 0, Conv::Lookup, NUT_LKP(kDischargingInfo) },
        // powercom-hid.c:565 UPS.PresentStatus.NeedReplacement
        { "BOOL", 0, 0, "UPS.PresentStatus.NeedReplacement", nullptr, 0, Conv::Lookup, NUT_LKP(kReplacebattInfo) },
        // powercom-hid.c:566 UPS.PresentStatus.Overload
        { "BOOL", 0, 0, "UPS.PresentStatus.Overload", nullptr, 0, Conv::Lookup, NUT_LKP(kOverloadInfo) },
        // powercom-hid.c:567 UPS.PresentStatus.RemainingTimeLimitExpired
        { "BOOL", 0, 0, "UPS.PresentStatus.RemainingTimeLimitExpired", nullptr, 0, Conv::Lookup, NUT_LKP(kTimelimitexpiredInfo) },
        // powercom-hid.c:568 UPS.PresentStatus.ShutdownImminent
        { "BOOL", 0, 0, "UPS.PresentStatus.ShutdownImminent", nullptr, 0, Conv::Lookup, NUT_LKP(kShutdownimmInfo) },
        // powercom-hid.c:569 UPS.PresentStatus.POWERCOM3 (BOOL): commented out upstream
        // powercom-hid.c:570 UPS.PresentStatus.ShutdownRequested (BOOL): commented out upstream
        // powercom-hid.c:571 UPS.PresentStatus.Tested (BOOL): commented out upstream
        // powercom-hid.c:572 UPS.PresentStatus.VoltageNotRegulated (BOOL): commented out upstream
        // battery.voltage(.nominal) on UPS.PowerSummary/Battery Voltage and ConfigVoltage (lines 577-580
        // of the upstream file), commented out there: PowerCOM duplicates output.voltage(.nominal)
        // powercom-hid.c:587 UPS.PowerSummary.PresentStatus.ACPresent
        { "battery.voltage", 0, 0, "UPS.PowerSummary.PresentStatus.ACPresent", "%s", 0, Conv::PowercomHackVoltage, nullptr, 0 },
        // powercom-hid.c:589 UPS.PowerSummary.RemainingCapacity
        { "battery.charge", 0, 0, "UPS.PowerSummary.RemainingCapacity", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:590 UPS.Battery.RemainingCapacity
        { "battery.charge", 0, 0, "UPS.Battery.RemainingCapacity", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:591 UPS.PowerSummary.RemainingCapacityLimit
        { "battery.charge.low", 0, 0, "UPS.PowerSummary.RemainingCapacityLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:592 UPS.PowerSummary.WarningCapacityLimit
        { "battery.charge.warning", 0, 0, "UPS.PowerSummary.WarningCapacityLimit", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:593 UPS.PowerSummary.RunTimeToEmpty
        { "battery.runtime", 0, 0, "UPS.PowerSummary.RunTimeToEmpty", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:594 UPS.Battery.ManufacturerDate
        { "battery.mfr.date", 0, 0, "UPS.Battery.ManufacturerDate", "%s", HU_FLAG_STATIC, Conv::BatteryDate, nullptr, 0 },
        // powercom-hid.c:595 UPS.PowerSummary.iDeviceChemistry
        { "battery.type", 0, 0, "UPS.PowerSummary.iDeviceChemistry", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:597 UPS.Battery.DelayBeforeStartup
        { "unmapped.ups.battery.delaybeforestartup", 0, 0, "UPS.Battery.DelayBeforeStartup", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:598 UPS.Battery.Initialized
        { "unmapped.ups.battery.initialized", 0, 0, "UPS.Battery.Initialized", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // powercom-hid.c:601 UPS.PowerSummary.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "%s", 0, Conv::Lookup, NUT_LKP(kPowercomBeeperInfo) },
        // powercom-hid.c:602 UPS.AudibleAlarmControl
        { "ups.beeper.status", 0, 0, "UPS.AudibleAlarmControl", "%s", 0, Conv::Lookup, NUT_LKP(kPowercomBeeperInfo) },
        // powercom-hid.c:603 UPS.Output.PercentLoad
        { "ups.load", 0, 0, "UPS.Output.PercentLoad", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:604 UPS.PowerSummary.ManufacturerDate
        { "ups.date", 0, 0, "UPS.PowerSummary.ManufacturerDate", "%s", HU_FLAG_STATIC, Conv::BatteryDate, nullptr, 0 },
        // powercom-hid.c:605 UPS.Battery.Test
        { "ups.test.result", 0, 0, "UPS.Battery.Test", "%s", 0, Conv::Lookup, NUT_LKP(kTestRead) },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:607 UPS.PowerSummary.iManufacturer
        { "unmapped.ups.powersummary.imanufacturer", 0, 0, "UPS.PowerSummary.iManufacturer", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // powercom-hid.c:608 UPS.PowerSummary.iProduct
        { "unmapped.ups.powersummary.iproduct", 0, 0, "UPS.PowerSummary.iProduct", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // powercom-hid.c:609 UPS.PowerSummary.iSerialNumber
        { "unmapped.ups.powersummary.iserialnumber", 0, 0, "UPS.PowerSummary.iSerialNumber", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // powercom-hid.c:610 UPS.iName
        { "unmapped.ups.iname", 0, 0, "UPS.iName", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
        // powercom-hid.c:611 UPS.PowerSummary.iOEMInformation
        { "unmapped.ups.powersummary.ioeminformation", 0, 0, "UPS.PowerSummary.iOEMInformation", "%s", HU_FLAG_STATIC, Conv::StringId, nullptr, 0 },
#endif
        // ups.delay.start (powercom-hid.c:620) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays (delayPaths(), US-066)
        // powercom-hid.c:621 UPS.PowerSummary.DelayBeforeStartup
        { "ups.timer.start", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", "%.0f", 0, Conv::PowercomStartup, nullptr, 0 },
        // ups.delay.shutdown (powercom-hid.c:637) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays (delayPaths(), US-066)
        // powercom-hid.c:638 UPS.PowerSummary.DelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", "%.0f", HU_FLAG_QUICK_POLL, Conv::PowercomShutdown, nullptr, 0 },
        // ups.delay.shutdown (powercom-hid.c:639) excluded: ups.delay.* comes from CommandCatalog::applyDefaultDelays (delayPaths(), US-066)
        // powercom-hid.c:640 UPS.PowerSummary.PCMDelayBeforeShutdown
        { "ups.timer.shutdown", 0, 0, "UPS.PowerSummary.0x00FF0057", "%.0f", HU_FLAG_QUICK_POLL, Conv::PowercomShutdown, nullptr, 0 },
        // powercom-hid.c:642 UPS.Input.Voltage
        { "input.voltage", 0, 0, "UPS.Input.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:643 UPS.Input.ConfigVoltage
        { "input.voltage.nominal", 0, 0, "UPS.Input.ConfigVoltage", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // powercom-hid.c:644 UPS.Input.Frequency
        { "input.frequency", 0, 0, "UPS.Input.Frequency", "%.1f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:646 UPS.Output.Voltage
        { "output.voltage", 0, 0, "UPS.Output.Voltage", "%.1f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:647 UPS.Output.ConfigVoltage
        { "output.voltage.nominal", 0, 0, "UPS.Output.ConfigVoltage", "%.0f", HU_FLAG_STATIC, Conv::Number, nullptr, 0 },
        // powercom-hid.c:648 UPS.Output.Frequency
        { "output.frequency", 0, 0, "UPS.Output.Frequency", "%.1f", 0, Conv::Number, nullptr, 0 },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:651 UPS.POWERCOM1
        { "unmapped.ups.powercom1", 0, 0, "UPS.0x0084002F", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:652 UPS.POWERCOM2
        { "unmapped.ups.powercom2", 0, 0, "UPS.0xFF860060", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:653 UPS.PowerSummary.Rechargeable
        { "unmapped.ups.powersummary.rechargeable", 0, 0, "UPS.PowerSummary.Rechargeable", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:654 UPS.ShutdownImminent
        { "unmapped.ups.shutdownimminent", 0, 0, "UPS.ShutdownImminent", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // powercom-hid.c:658 UPS.PowerSummary.AudibleAlarmControl, not used: CommandCatalog builds beeper.toggle from beeper.enable and beeper.disable
        { "beeper.toggle", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:659 UPS.PowerSummary.AudibleAlarmControl (written through encodeBeeperValue(), same 1)
        { "beeper.enable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:660 UPS.PowerSummary.AudibleAlarmControl (written through encodeBeeperValue(), same 0)
        { "beeper.disable", 0, 0, "UPS.PowerSummary.AudibleAlarmControl", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:661 UPS.Battery.Test
        { "test.battery.start.quick", 0, 0, "UPS.Battery.Test", "1", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:662 UPS.PowerSummary.DelayBeforeStartup, not exposed: shutdownCommandsSupported() is false (US-058, powercom_startup_info not ported)
        { "load.on.delay", 0, 0, "UPS.PowerSummary.DelayBeforeStartup", nullptr, HU_TYPE_CMD, Conv::PowercomStartup, nullptr, 0 },
        // powercom-hid.c:663 UPS.PowerSummary.DelayBeforeShutdown, not exposed: shutdownCommandsSupported() is false (US-058, powercom_shutdown_info not ported)
        { "shutdown.return", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", nullptr, HU_TYPE_CMD, Conv::PowercomShutdown, nullptr, 0 },
        // powercom-hid.c:664 UPS.PowerSummary.DelayBeforeShutdown, not exposed: shutdownCommandsSupported() is false (US-058, powercom_stayoff_info not ported)
        { "shutdown.stayoff", 0, 0, "UPS.PowerSummary.DelayBeforeShutdown", nullptr, HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:665 UPS.PowerSummary.PCMDelayBeforeStartup, not exposed: shutdownCommandsSupported() is false (US-058, powercom_startup_info not ported)
        { "load.on", 0, 0, "UPS.PowerSummary.0x00FF0056", "0", HU_TYPE_CMD, Conv::PowercomStartup, nullptr, 0 },
        // powercom-hid.c:666 UPS.PowerSummary.PCMDelayBeforeShutdown, not exposed: shutdownCommandsSupported() is false (US-058, powercom_stayoff_info not ported)
        { "load.off", 0, 0, "UPS.PowerSummary.0x00FF0057", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:667 UPS.PowerSummary.PCMDelayBeforeShutdown, not exposed: shutdownCommandsSupported() is false (US-058, powercom_shutdown_info not ported)
        { "shutdown.return", 0, 0, "UPS.PowerSummary.0x00FF0057", nullptr, HU_TYPE_CMD, Conv::PowercomShutdown, nullptr, 0 },
        // powercom-hid.c:668 UPS.PowerSummary.PCMDelayBeforeShutdown, not exposed: shutdownCommandsSupported() is false (US-058, powercom_stayoff_info not ported)
        { "shutdown.stayoff", 0, 0, "UPS.PowerSummary.0x00FF0057", nullptr, HU_TYPE_CMD, Conv::Number, nullptr, 0 },
        // powercom-hid.c:670 PowercomUPS.PowercomSerialNumber
        { "ups.serial", 0, 0, "0x00020004.0x00020086", "%s", 0, Conv::StringId, nullptr, 0 },
        // powercom-hid.c:671 PowercomUPS.PowercomManufacturerName
        { "ups.mfr", 0, 0, "0x00020004.0x00020087", "%s", 0, Conv::StringId, nullptr, 0 },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:673 PowercomUPS.PowercomDesignCapacity
        { "UPS.DesignCapacity", 0, 0, "0x00020004.0x00020083", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // powercom-hid.c:675 PowercomUPS.PowercomManufacturerDate
        { "ups.mfr.date", 0, 0, "0x00020004.0x00020085", "%s", 0, Conv::BatteryDate, nullptr, 0 },
        // powercom-hid.c:676 PowercomUPS.PowercomBatterySystem.PowercomTemperature
        { "battery.temperature", 0, 0, "0x00020004.0x00020010.0x00020036", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:677 UPS.Battery.Temperature
        { "battery.temperature", 0, 0, "UPS.Battery.Temperature", "%.1f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:678 PowercomUPS.PowercomBatterySystem.PowercomVoltage
        { "battery.charge", 0, 0, "0x00020004.0x00020010.0x00020030", "%.0f", 0, Conv::Number, nullptr, 0 },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:680 PowercomUPS.PowercomBatterySystem.PowercomSpecificationInfo
        { "UPS.BatterySystem.SpecificationInfo", 0, 0, "0x00020004.0x00020010.0x00020084", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // powercom-hid.c:682 PowercomUPS.PowercomPowerConverter.PowercomInput.PowercomFrequency
        { "input.frequency", 0, 0, "0x00020004.0x00020016.0x0002001A.0x00020032", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:683 PowercomUPS.PowercomPowerConverter.PowercomInput.PowercomVoltage
        { "input.voltage", 0, 0, "0x00020004.0x00020016.0x0002001A.0x00020030", "%.0f", 0, Conv::PowercomVoltage, nullptr, 0 },
        // powercom-hid.c:684 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomVoltage
        { "output.voltage", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020030", "%.0f", 0, Conv::PowercomVoltage, nullptr, 0 },
        // powercom-hid.c:685 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPercentLoad
        { "ups.load", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020035", "%.0f", 0, Conv::Number, nullptr, 0 },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:697 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomInternalChargeController
        { "UPS.PowerConverter.Output.InternalChargeController", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020081", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // powercom-hid.c:699 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomInternalChargeController
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020081", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomUpsfail, nullptr, 0 },
        // powercom-hid.c:700 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomInternalChargeController
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020081", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomReplacebatt, nullptr, 0 },
        // powercom-hid.c:701 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomInternalChargeController
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020081", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomTest, nullptr, 0 },
        // powercom-hid.c:702 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomInternalChargeController
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020081", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomShutdownimm, nullptr, 0 },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:714 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPrimaryBatterySupport
        { "UPS.PowerConverter.Output.PrimaryBatterySupport", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020082", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // powercom-hid.c:716 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPrimaryBatterySupport
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020082", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomOnline, nullptr, 0 },
        // powercom-hid.c:718 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPrimaryBatterySupport
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020082", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomLowbatt, nullptr, 0 },
        // powercom-hid.c:719 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPrimaryBatterySupport
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020082", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomTrim, nullptr, 0 },
        // powercom-hid.c:720 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPrimaryBatterySupport
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020082", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomBoost, nullptr, 0 },
        // powercom-hid.c:721 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomPrimaryBatterySupport
        { "BOOL", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020082", nullptr, HU_FLAG_QUICK_POLL, Conv::PowercomOverload, nullptr, 0 },
        // powercom-hid.c:722 PowercomUPS.PowercomPowerConverter.PowercomOutput.PowercomFrequency
        { "output.frequency", 0, 0, "0x00020004.0x00020016.0x0002001C.0x00020032", "%.0f", 0, Conv::Number, nullptr, 0 },
        // powercom-hid.c:723 PowercomUPS.PowercomPowerConverter.PowercomTest
        { "ups.test.result", 0, 0, "0x00020004.0x00020016.0x00020058", "%s", 0, Conv::Lookup, NUT_LKP(kTestRead) },
#if WITH_UNMAPPED_DATA_POINTS
        // powercom-hid.c:725 PowercomUPS.PowercomPowerConverter.PowercomShutdownRequested
        { "UPS.PowerConverter.ShutdownRequested", 0, 0, "0x00020004.0x00020016.0x00020068", "%.0f", 0, Conv::Number, nullptr, 0 },
#endif
        // ups.delay.shutdown (powercom-hid.c:727) excluded: ups.delay.* are the configured delays, not device values (US-058)
        // ups.delay.start (powercom-hid.c:728) excluded: ups.delay.* are the configured delays, not device values (US-058)
        // powercom-hid.c:729 PowercomUPS.PowercomPowerConverter.PowercomDelayBeforeShutdown, not exposed: shutdownCommandsSupported() is false (US-058)
        { "load.off", 0, 0, "0x00020004.0x00020016.0x00020057", "0", HU_TYPE_CMD, Conv::Number, nullptr, 0 },
    };
    n = sizeof(t) / sizeof(t[0]);
    return t;
}

} // namespace NutTable

#endif // NUT_TABLE_H
