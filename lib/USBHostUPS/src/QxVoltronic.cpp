/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx_voltronic.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 */

#include "QxProtocols.h"
#include "QxEngine.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Port of nut_repo/drivers/nutdrv_qx_voltronic.c (VOLTRONIC_VERSION "Voltronic 0.15"), in the
// order of that file; line numbers refer to it. The qx2nut table is a row by row copy of
// voltronic_qx2nut[] (974-1557) with the columns of item_t without the runtime answer and
// value (QxTypes.h): info_type, info_flags, info_rw, command, answer_len, leading, from, to,
// dfl, qxflags, preprocess_command, preprocess_answer, preprocess. DEFAULT_ONDELAY "180" and
// DEFAULT_OFFDELAY "30" of nutdrv_qx.h. The testing table (voltronic_testing, 1562-1679)
// lives in test/mocks/QxTestingTables.h.
//
// item->value -> qx.value(), item->qxflags of a row -> qx.flags()/addFlags()/clearFlags() on
// qx.row(item), find_nut_info() -> qx.findNutInfo() (a row index, -1 when not found),
// dstate_getinfo()/setinfo()/delinfo() -> qx.getinfo()/setinfo()/delinfo(),
// qx_process() -> qx.process(), ups_infoval_set() -> qx.infovalSet(), update_status() ->
// qx.updateStatus(), qx_status() -> qx.status(), buzzmode_set() -> qx.buzzmodeSet(),
// snprintf_dynamic(value, len, fmt, "%x", v) -> snprintf(value, len, fmt, v) on the format of
// a static const row (the table formats match the conversions the functions pass, as
// snprintf_dynamic() checks upstream). The upslogx()/upsdebugx() messages are kept as
// comments, as in the other ports: QxDriver logs the outcomes (protocol, communications) and
// the host every command and SET VAR.
//
// The module-level variables of upstream that the walk writes (the capability strings, the
// bypass limits, battery_number, output_phase_angle, work_range_type, 75-96) and the values
// of voltronic_r_eco_volt_max/min that voltronic_eco_volt() fills (702-714) live in the
// engine (VoltronicState, QxEngine::subdriverState()), zeroed with the engine as the
// statics are at start upstream.
//
// ups.conf options are not offered in this delivery (ADR 0013: options come when a tester
// needs them): voltronic_makevartable() (1752-1786) is not ported, getval() is NULL and
// testvar() false for every option (see getval()/testvar() below), so the code paths that
// depend on them stay as upstream runs them without options. Not offered: the flags
// reset_to_default and testing; the values bypass_alarm, battery_alarm, bypass_when_off,
// alarm_control, converter_mode, eco_mode, battery_open_status_check, bypass_forbidding,
// site_fault_detection, advanced_eco_mode, constant_phase_angle, limited_runtime_on_battery,
// max_bypass_volt, min_bypass_volt, max_bypass_freq, min_bypass_freq, work_range_type,
// output_phase_angle and battery_number. Without them the NONUT setvar rows stay skipped
// (or are skipped by the INIT walk, battery_number) and nothing is sent to change them.

namespace {

// Runtime state of the subdriver (the statics of nutdrv_qx_voltronic.c), all zero at start
struct VoltronicState {
    // Capability vars (75-87): "enabled", "disabled" or nullptr (not reported by the UPS)
    const char* bypass_alarm;
    const char* battery_alarm;
    const char* bypass_when_off;
    const char* alarm_control;
    const char* converter_mode;
    const char* eco_mode;
    const char* battery_open_status_check;
    const char* bypass_forbidding;
    const char* site_fault_detection;
    const char* advanced_eco_mode;
    const char* constant_phase_angle;
    const char* limited_runtime_on_battery;
    // "ups.conf settings" (89-96): the values the UPS reports, written by the walk
    long max_bypass_volt;
    long min_bypass_volt;
    long battery_number;
    long output_phase_angle;
    long work_range_type;
    double max_bypass_freq;
    double min_bypass_freq;
    // info_rw_t.value of voltronic_r_eco_volt_max/min, "" until voltronic_eco_volt() fills them
    char eco_volt_max[2][16];
    char eco_volt_min[2][16];
    // Those two tables as QxInfoRw on the values above, rebuilt by voltronicInfoRw()
    QxInfoRw r_eco_volt_max[3];
    QxInfoRw r_eco_volt_min[3];
};

static_assert(sizeof(VoltronicState) <= QxEngine::SUBDRIVER_STATE_SIZE, "VoltronicState too large");

VoltronicState& state(QxEngine& qx) {
    return qx.subdriverState<VoltronicState>();
}

// ups.conf options are not offered (see above): getval() of every option is NULL...
const char* getval(const char* var) {
    (void)var;
    return nullptr;
}

// ...and testvar() false
bool testvar(const char* var) {
    (void)var;
    return false;
}

bool eq(const char* a, const char* b) {
    return QxEngine::equalsIgnoreCase(a, b);
}

// d_equal() of include/nut_float.h:57
bool dEqual(double x, double y) {
    return fabs(x - y) <= DBL_EPSILON;
}

// dstate_getinfo() of a missing variable is NULL upstream and strtol/strtod/strcmp(NULL) would
// crash there; here it reads as an empty string (0)
const char* infoOrEmpty(QxEngine& qx, const char* name) {
    const char* v = qx.getinfo(name);
    return v ? v : "";
}

// strtol(dstate_getinfo("ups.firmware.aux")+1, NULL, 10): "P01" -> 1. The claim sets
// ups.firmware.aux; a missing or empty one (a NULL or out of bounds read upstream) gives 0
long upsProtocol(QxEngine& qx) {
    const char* v = qx.getinfo("ups.firmware.aux");
    if (!v || !*v) return 0;
    return strtol(v + 1, nullptr, 10);
}

// item = find_nut_info(name, flag, 0); item->qxflags &= ~QX_FLAG_SKIP. False when the row is
// not found ("Don't know what happened": the caller returns -1)
bool unskip(QxEngine& qx, const char* name, uint32_t flag) {
    int row = qx.findNutInfo(name, flag, 0);
    if (row < 0) return false;
    qx.clearFlags(row, QX_FLAG_SKIP);
    return true;
}

// snprintfcat() into a bounded buffer that also counts the length the text would have
// without the bound (the 4096-byte buffers of voltronic_warning() never truncate upstream)
struct Cat {
    char* buf;
    size_t size;
    size_t len;

    Cat(char* b, size_t s, size_t start) : buf(b), size(s), len(start) {}

    void add(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(nullptr, 0, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (len < size) {
            va_start(ap, fmt);
            vsnprintf(buf + len, size - len, fmt, ap);
            va_end(ap);
        }
        len += (size_t)n;
    }
};

// strtok_r() as POSIX defines it (the parsing of voltronic_capability() depends on its
// skipping of leading delimiters), here for every toolchain
char* tokenize(char* s, const char* delim, char** saveptr) {
    if (!s) s = *saveptr;
    if (!s) return nullptr;
    s += strspn(s, delim);
    if (!*s) {
        *saveptr = s;
        return nullptr;
    }
    char* end = s + strcspn(s, delim);
    if (*end) {
        *end = '\0';
        *saveptr = end + 1;
    } else {
        *saveptr = end;
    }
    return s;
}

} // namespace

/* == Ranges/enums == */

/* Range for ups.delay.start */
static const QxInfoRw VOLTRONIC_R_ONDELAY[] = {
    { "0", nullptr },
    { "599940", nullptr },
    { "", nullptr }
};

/* Range for ups.delay.shutdown */
static const QxInfoRw VOLTRONIC_R_OFFDELAY[] = {
    { "12", nullptr },
    { "5940", nullptr },
    { "", nullptr }
};

/* Enumlist for output phase angle */
static const QxInfoRw VOLTRONIC_E_PHASE[] = {
    { "000", nullptr },
    { "120", nullptr },
    { "180", nullptr },
    { "240", nullptr },
    { "", nullptr }
};

/* Preprocess range value for battery low voltage */
static int voltronicBattLow(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long val = strtol(value, nullptr, 10);
    const char *ovn = qx.getinfo("output.voltage.nominal"), *ocn = qx.getinfo("output.current.nominal");

    // "unable to get the value of output voltage nominal/output current nominal"
    if (!ovn || !ocn) return -1;

    if ((strtol(ovn, nullptr, 10) * strtol(ocn, nullptr, 10)) < 1000) {
        if (val == 24) return 0;
        else return -1;
    } else {
        if (val == 28) return 0;
        else return -1;
    }
}

/* Range for battery low voltage */
static const QxInfoRw VOLTRONIC_R_BATT_LOW[] = {
    { "20", nullptr },
    { "24", voltronicBattLow },
    { "28", voltronicBattLow },
    { "", nullptr }
};

/* Range for outlet.n.delay.shutdown */
static const QxInfoRw VOLTRONIC_R_OUTLET_DELAY[] = {
    { "0", nullptr },
    { "59940", nullptr },
    { "", nullptr }
};

/* Enumlist for device grid working range type */
static const QxInfoRw VOLTRONIC_E_WORK_RANGE[] = {
    { "Appliance", nullptr }, /* 00 */
    { "UPS", nullptr },       /* 01 */
    { "", nullptr }
};

/* Enumlist for battery type */
static const QxInfoRw VOLTRONIC_E_BATT_TYPE[] = {
    { "Li", nullptr },      /* 00 */
    { "Flooded", nullptr }, /* 01 */
    { "AGM", nullptr },     /* 02 */
    { "", nullptr }
};

/* Range for number of battery packs */
static const QxInfoRw VOLTRONIC_R_BATT_PACKS[] = {
    { "1", nullptr },
    { "99", nullptr },
    { "", nullptr }
};

/* Range for number of batteries */
static const QxInfoRw VOLTRONIC_R_BATT_NUMB[] = {
    { "1", nullptr },
    { "9", nullptr },
    { "", nullptr }
};

/* Preprocess range value for Bypass Mode maximum voltage */
static int voltronicBypassVoltMax(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long protocol = upsProtocol(qx), val = strtol(value, nullptr, 10), ivn;
    const char* involtnom = qx.getinfo("input.voltage.nominal");

    // "unable to get input.voltage.nominal"
    if (!involtnom) return -1;

    ivn = strtol(involtnom, nullptr, 10);

    switch (val) {
    case 60: /* P09 */
        if (protocol == 9) return 0;
        break;

    case 115: /* P02/P03/P10/P13/P14/P99 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 120: /* P01 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 1) return 0;
        break;

    case 132: /* P99 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 99) return 0;
        break;

    case 138: /* P02/P03/P10/P13/P14 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14) return 0;
        break;

    case 140: /* P01 ivn<200, P09 */
        if (protocol == 9) return 0;
        if (ivn >= 200) return -1;
        if (protocol == 1) return 0;
        break;

    case 230: /* P01 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 1) return 0;
        break;

    case 231: /* P02/P03/P10/P13/P14/P99 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 261: /* P99 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 99) return 0;
        break;

    case 264: /* P01 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 1) return 0;
        break;

    case 276: /* P02/P03/P10/P13/P14 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14) return 0;
        break;

    default:
        // "unknown value"
        break;
    }

    return -1;
}

/* Range for Bypass Mode maximum voltage */
static const QxInfoRw VOLTRONIC_R_BYPASS_VOLT_MAX[] = {
    { "60", voltronicBypassVoltMax },  /* P09 */
    { "115", voltronicBypassVoltMax }, /* P02/P03/P10/P13/P14/P99 ivn<200 */
    { "120", voltronicBypassVoltMax }, /* P01 ivn<200 */
    { "132", voltronicBypassVoltMax }, /* P99 ivn<200 */
    { "138", voltronicBypassVoltMax }, /* P02/P03/P10/P13/P14 ivn<200 */
    { "140", voltronicBypassVoltMax }, /* P01 ivn<200, P09 */
    { "230", voltronicBypassVoltMax }, /* P01 ivn>=200 */
    { "231", voltronicBypassVoltMax }, /* P02/P03/P10/P13/P14/P99 ivn>=200 */
    { "261", voltronicBypassVoltMax }, /* P99 ivn>=200 */
    { "264", voltronicBypassVoltMax }, /* P01 ivn>=200 */
    { "276", voltronicBypassVoltMax }, /* P02/P03/P10/P13/P14 ivn>=200 */
    { "", nullptr }
};

/* Preprocess range value for Bypass Mode minimum voltage */
static int voltronicBypassVoltMin(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long protocol = upsProtocol(qx), val = strtol(value, nullptr, 10), ivn;
    const char* involtnom = qx.getinfo("input.voltage.nominal");

    // "unable to get input.voltage.nominal"
    if (!involtnom) return -1;

    ivn = strtol(involtnom, nullptr, 10);

    switch (val) {
    case 50: /* P99 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 99) return 0;
        break;

    case 55: /* P02/P03/P10/P13/P14 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14) return 0;
        break;

    case 60:  /* P09 */
    case 140: /* P09 */
        if (protocol == 9) return 0;
        break;

    case 85: /* P01/P99 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 1 || protocol == 99) return 0;
        break;

    case 104: /* P02/P03/P10/P13/P14 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14) return 0;
        break;

    case 110: /* P02/P03/P10/P13/P14 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14) return 0;
        break;

    case 115: /* P01 ivn<200 */
        if (ivn >= 200) return -1;
        if (protocol == 1) return 0;
        break;

    case 149: /* P99 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 99) return 0;
        break;

    case 170: /* P01 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 1) return 0;
        break;

    case 209: /* P02/P03/P10/P13/P14/P99 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 220: /* P01 ivn>=200 */
        if (ivn < 200) return -1;
        if (protocol == 1) return 0;
        break;

    default:
        // "unknown value"
        break;
    }

    return -1;
}

/* Range for Bypass Mode minimum voltage */
static const QxInfoRw VOLTRONIC_R_BYPASS_VOLT_MIN[] = {
    { "50", voltronicBypassVoltMin },  /* P99 ivn<200 */
    { "55", voltronicBypassVoltMin },  /* P02/P03/P10/P13/P14 ivn<200 */
    { "60", voltronicBypassVoltMin },  /* P09 */
    { "85", voltronicBypassVoltMin },  /* P01/P99 ivn<200 */
    { "104", voltronicBypassVoltMin }, /* P02/P03/P10/P13/P14 ivn<200 */
    { "110", voltronicBypassVoltMin }, /* P02/P03/P10/P13/P14 ivn>=200 */
    { "115", voltronicBypassVoltMin }, /* P01 ivn<200 */
    { "140", voltronicBypassVoltMin }, /* P09 */
    { "149", voltronicBypassVoltMin }, /* P99 ivn>=200 */
    { "170", voltronicBypassVoltMin }, /* P01 ivn>=200 */
    { "209", voltronicBypassVoltMin }, /* P02/P03/P10/P13/P14/P99 ivn>=200 */
    { "220", voltronicBypassVoltMin }, /* P01 ivn>=200 */
    { "", nullptr }
};

/* Preprocess range value for Bypass Mode maximum frequency */
static int voltronicBypassFreqMax(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long protocol = upsProtocol(qx), val = strtol(value, nullptr, 10);
    double ofn;
    const char* outfreqnom = qx.getinfo("output.frequency.nominal");

    // "unable to get output.frequency.nominal"
    if (!outfreqnom) return -1;

    ofn = strtod(outfreqnom, nullptr);

    switch (val) {
    case 51: /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 2 || protocol == 3 || protocol == 9 || protocol == 10 || protocol == 13 ||
            protocol == 14 || protocol == 99)
            return 0;
        break;

    case 54: /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 60: /* P01/P09 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 61: /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 2 || protocol == 3 || protocol == 9 || protocol == 10 || protocol == 13 ||
            protocol == 14 || protocol == 99)
            return 0;
        break;

    case 64: /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 70: /* P01/P09 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    default:
        // "unknown value"
        break;
    }

    return -1;
}

/* Range for Bypass Mode maximum frequency */
static const QxInfoRw VOLTRONIC_R_BYPASS_FREQ_MAX[] = {
    { "51.0", voltronicBypassFreqMax }, /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "54.0", voltronicBypassFreqMax }, /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "60.0", voltronicBypassFreqMax }, /* P01/P09 ofn==50.0 */
    { "61.0", voltronicBypassFreqMax }, /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "64.0", voltronicBypassFreqMax }, /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "70.0", voltronicBypassFreqMax }, /* P01/P09 ofn==60.0 */
    { "", nullptr }
};

/* Preprocess range value for Bypass Mode minimum frequency */
static int voltronicBypassFreqMin(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long protocol = upsProtocol(qx), val = strtol(value, nullptr, 10);
    double ofn;
    const char* outfreqnom = qx.getinfo("output.frequency.nominal");

    // "unable to get output.frequency.nominal"
    if (!outfreqnom) return -1;

    ofn = strtod(outfreqnom, nullptr);

    switch (val) {
    case 40: /* P01/P09 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 46: /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 49: /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 2 || protocol == 3 || protocol == 9 || protocol == 10 || protocol == 13 ||
            protocol == 14 || protocol == 99)
            return 0;
        break;

    case 50: /* P01/P09 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 56: /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 59: /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 2 || protocol == 3 || protocol == 9 || protocol == 10 || protocol == 13 ||
            protocol == 14 || protocol == 99)
            return 0;
        break;

    default:
        // "unknown value"
        break;
    }

    return -1;
}

/* Range for Bypass Mode minimum frequency */
static const QxInfoRw VOLTRONIC_R_BYPASS_FREQ_MIN[] = {
    { "40.0", voltronicBypassFreqMin }, /* P01/P09 ofn==50.0 */
    { "46.0", voltronicBypassFreqMin }, /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "49.0", voltronicBypassFreqMin }, /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "50.0", voltronicBypassFreqMin }, /* P01/P09 ofn==60.0 */
    { "56.0", voltronicBypassFreqMin }, /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "59.0", voltronicBypassFreqMin }, /* P01/P09/P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "", nullptr }
};

/* Range for Eco Mode maximum voltage: filled at runtime by voltronic_eco_volt (here the
 * values are in VoltronicState, voltronicInfoRw() gives them to the engine) */
static const QxInfoRw VOLTRONIC_R_ECO_VOLT_MAX[] = {
    { "", nullptr },
    { "", nullptr },
    { "", nullptr }
};

/* Range for ECO Mode minimum voltage: filled at runtime by voltronic_eco_volt (as above) */
static const QxInfoRw VOLTRONIC_R_ECO_VOLT_MIN[] = {
    { "", nullptr },
    { "", nullptr },
    { "", nullptr }
};

/* Preprocess range value for ECO Mode minimum frequency */
static int voltronicEcoFreqMin(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long protocol = upsProtocol(qx), val = strtol(value, nullptr, 10);
    double ofn;
    const char* outfreqnom = qx.getinfo("output.frequency.nominal");

    // "unable to get output.frequency.nominal"
    if (!outfreqnom) return -1;

    ofn = strtod(outfreqnom, nullptr);

    switch (val) {
    case 40: /* P01/P09 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 46: /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 47: /* P01/P09 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 48: /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 50: /* P01/P09 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 56: /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 57: /* P01/P09 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 58: /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    default:
        // "unknown value"
        break;
    }

    return -1;
}

/* Range for ECO Mode minimum frequency */
static const QxInfoRw VOLTRONIC_R_ECO_FREQ_MIN[] = {
    { "40.0", voltronicEcoFreqMin }, /* P01/P09 ofn==50.0 */
    { "46.0", voltronicEcoFreqMin }, /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "47.0", voltronicEcoFreqMin }, /* P01/P09 ofn==50.0 */
    { "48.0", voltronicEcoFreqMin }, /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "50.0", voltronicEcoFreqMin }, /* P01/P09 ofn==60.0 */
    { "56.0", voltronicEcoFreqMin }, /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "57.0", voltronicEcoFreqMin }, /* P01/P09 ofn==60.0 */
    { "58.0", voltronicEcoFreqMin }, /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "", nullptr }
};

/* Preprocess range value for ECO Mode maximum frequency */
static int voltronicEcoFreqMax(QxEngine& qx, char* value, size_t len) {
    (void)len;
    long protocol = upsProtocol(qx), val = strtol(value, nullptr, 10);
    double ofn;
    const char* outfreqnom = qx.getinfo("output.frequency.nominal");

    // "unable to get output.frequency.nominal"
    if (!outfreqnom) return -1;

    ofn = strtod(outfreqnom, nullptr);

    switch (val) {
    case 52: /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 53: /* P01/P09 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 54: /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 60: /* P01/P09 ofn==50.0 */
        if (ofn != 50.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 62: /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 63: /* P01/P09 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    case 64: /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99)
            return 0;
        break;

    case 70: /* P01/P09 ofn==60.0 */
        if (ofn != 60.0) return -1;
        if (protocol == 1 || protocol == 9) return 0;
        break;

    default:
        // "unknown value"
        break;
    }

    return -1;
}

/* Range for ECO Mode maximum frequency */
static const QxInfoRw VOLTRONIC_R_ECO_FREQ_MAX[] = {
    { "52.0", voltronicEcoFreqMax }, /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "53.0", voltronicEcoFreqMax }, /* P01/P09 ofn==50.0 */
    { "54.0", voltronicEcoFreqMax }, /* P02/P03/P10/P13/P14/P99 ofn==50.0 */
    { "60.0", voltronicEcoFreqMax }, /* P01/P09 ofn==50.0 */
    { "62.0", voltronicEcoFreqMax }, /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "63.0", voltronicEcoFreqMax }, /* P01/P09 ofn==60.0 */
    { "64.0", voltronicEcoFreqMax }, /* P02/P03/P10/P13/P14/P99 ofn==60.0 */
    { "70.0", voltronicEcoFreqMax }, /* P01/P09 ofn==60.0 */
    { "", nullptr }
};

/* Enumlist for UPS capabilities that have a NUT var */
static const QxInfoRw VOLTRONIC_E_CAP[] = {
    { "no", nullptr },
    { "yes", nullptr },
    { "", nullptr }
};

/* Enumlist for NONUT capabilities */
static const QxInfoRw VOLTRONIC_E_CAP_NONUT[] = {
    { "enabled", nullptr },
    { "disabled", nullptr },
    { "", nullptr }
};

/* Preprocess functions */
static int voltronicProcessSetvar(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicProcessCommand(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicCapability(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicCapabilitySet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicCapabilitySetNonut(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicCapabilityReset(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicEcoVolt(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicEcoVoltRange(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicEcoFreq(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicBypass(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicBattNumb(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicBattRuntime(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicProtocol(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicFault(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicWarning(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicMode(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicStatus(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicOutputPowerfactor(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicSerialNumb(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicOutlet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicOutletDelay(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicOutletDelaySet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicP31b(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicP31bSet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicP31g(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicP31gSet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicPhase(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicPhaseSet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
static int voltronicParallel(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);


/* == qx2nut lookup table == */
static const QxItem VOLTRONIC_QX2NUT[] = {
    /* Query UPS for protocol
     * > [QPI\r]
     * < [(PI00\r]
     *    012345
     *    0
     */

    { "ups.firmware.aux", 0, nullptr, "QPI\r", 6, '(', 1, 4, "%s", QX_FLAG_STATIC, nullptr, nullptr, voltronicProtocol },

    /* Query UPS for ratings
     * > [QRI\r]
     * < [(230.0 004 024.0 50.0\r]
     *    0123456789012345678901
     *    0         1         2
     */

    { "output.voltage.nominal", 0, nullptr, "QRI\r", 22, '(', 1, 5, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "output.current.nominal", 0, nullptr, "QRI\r", 22, '(', 7, 9, "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "battery.voltage.nominal", 0, nullptr, "QRI\r", 22, '(', 11, 15, "%.1f", QX_FLAG_SEMI_STATIC, nullptr, nullptr, nullptr }, /* as *per battery pack*: the value will change when the number of batteries is changed (battery_number through BATNn) */
    { "output.frequency.nominal", 0, nullptr, "QRI\r", 22, '(', 17, 20, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },

    /* Query UPS for ratings
     * > [QMD\r]
     * < [(#######OLHVT1K0 ###1000 80 1/1 230 230 02 12.0\r] <- Some UPS may reply with spaces instead of hashes
     *    012345678901234567890123456789012345678901234567
     *    0         1         2         3         4
     */

    { "device.model", 0, nullptr, "QMD\r", 48, '(', 1, 15, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr },
    { "ups.power.nominal", 0, nullptr, "QMD\r", 48, '(', 17, 23, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr },
    { "output.powerfactor", 0, nullptr, "QMD\r", 48, '(', 25, 26, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, voltronicOutputPowerfactor },
    { "input.phases", 0, nullptr, "QMD\r", 48, '(', 28, 28, "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "output.phases", 0, nullptr, "QMD\r", 48, '(', 30, 30, "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.voltage.nominal", 0, nullptr, "QMD\r", 48, '(', 32, 34, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "output.voltage.nominal", 0, nullptr, "QMD\r", 48, '(', 36, 38, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr }, /* redundant with value from QRI */
    // { "battery_number", QX_ST_FLAG_RW, VOLTRONIC_R_BATT_NUMB, "QMD\r", 48, '(', 40, 41, "%d", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_NONUT, nullptr, nullptr, voltronicBattNumb }, /* redundant with value from QBV */
    // { "battery.voltage.nominal", 0, nullptr, "QMD\r", 48, '(', 43, 46, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr }, /* as *per battery* vs. *per pack* reported by QRI */

    /* Query UPS for ratings
     * > [F\r]
     * < [#220.0 000 024.0 50.0\r]
     *    0123456789012345678901
     *    0         1         2
     */

    { "input.voltage.nominal", 0, nullptr, "F\r", 22, '#', 1, 5, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.current.nominal", 0, nullptr, "F\r", 22, '#', 7, 9, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "battery.voltage.nominal", 0, nullptr, "F\r", 22, '#', 11, 15, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.frequency.nominal", 0, nullptr, "F\r", 22, '#', 17, 20, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },

    /* Query UPS for manufacturer
     * > [QMF\r]
     * < [(#######BOH\r] <- I don't know if it has a fixed length (-> so min length = ( + \r = 2). Hashes may be replaced by spaces
     *    012345678901
     *    0         1
     */

    { "device.mfr", 0, nullptr, "QMF\r", 2, '(', 1, 0, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr },

    /* Query UPS for firmware version
     * > [QVFW\r]
     * < [(VERFW:00322.02\r]
     *    0123456789012345
     *    0         1
     */

    { "ups.firmware", 0, nullptr, "QVFW\r", 16, '(', 7, 14, "%s", QX_FLAG_STATIC, nullptr, nullptr, nullptr },

    /* Query UPS for serial number
     * > [QID\r]
     * < [(12345679012345\r] <- As far as I know it hasn't a fixed length -> min length = ( + \r = 2
     *    0123456789012345
     *    0         1
     */

    { "device.serial", 0, nullptr, "QID\r", 2, '(', 1, 0, "%s", QX_FLAG_STATIC, nullptr, nullptr, voltronicSerialNumb },

    /* Query UPS for vendor infos
     * > [I\r]
     * < [#-------------   ------     VT12046Q  \r]
     *    012345678901234567890123456789012345678
     *    0         1         2         3
     */

    { "device.mfr", 0, nullptr, "I\r", 39, '#', 1, 15, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr },
    { "device.model", 0, nullptr, "I\r", 39, '#', 17, 26, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr },
    { "ups.firmware", 0, nullptr, "I\r", 39, '#', 28, 37, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr },

    /* Query UPS for status
     * > [QGS\r]
     * < [(234.9 50.0 229.8 50.0 000.0 000 369.1 ---.- 026.5 ---.- 018.8 100000000001\r]
     *    0123456789012345678901234567890123456789012345678901234567890123456789012345
     *    0         1         2         3         4         5         6         7
     */

    { "input.voltage", 0, nullptr, "QGS\r", 76, '(', 1, 5, "%.1f", 0, nullptr, nullptr, nullptr },
    { "input.frequency", 0, nullptr, "QGS\r", 76, '(', 7, 10, "%.1f", 0, nullptr, nullptr, nullptr },
    { "output.voltage", 0, nullptr, "QGS\r", 76, '(', 12, 16, "%.1f", 0, nullptr, nullptr, nullptr },
    { "output.frequency", 0, nullptr, "QGS\r", 76, '(', 18, 21, "%.1f", 0, nullptr, nullptr, nullptr },
    { "output.current", 0, nullptr, "QGS\r", 76, '(', 23, 27, "%.1f", 0, nullptr, nullptr, nullptr },
    { "ups.load", 0, nullptr, "QGS\r", 76, '(', 29, 31, "%.0f", 0, nullptr, nullptr, nullptr },
    // { "unknown.1", 0, nullptr, "QGS\r", 76, '(', 33, 37, "%.1f", 0, nullptr, nullptr, nullptr }, /* Unknown */
    // { "unknown.2", 0, nullptr, "QGS\r", 76, '(', 39, 43, "%.1f", 0, nullptr, nullptr, nullptr }, /* Unknown */
    { "battery.voltage", 0, nullptr, "QGS\r", 76, '(', 45, 49, "%.2f", 0, nullptr, nullptr, QxEngine::qxMultiplyBattvolt },
    // { "unknown.3", 0, nullptr, "QGS\r", 76, '(', 51, 55, "%.1f", 0, nullptr, nullptr, nullptr }, /* Unknown */
    { "ups.temperature", 0, nullptr, "QGS\r", 76, '(', 57, 61, "%.1f", 0, nullptr, nullptr, nullptr },
    { "ups.type", 0, nullptr, "QGS\r", 76, '(', 63, 64, "%s", QX_FLAG_SEMI_STATIC, nullptr, nullptr, voltronicStatus },
    { "ups.status", 0, nullptr, "QGS\r", 76, '(', 65, 65, "%s", QX_FLAG_QUICK_POLL, nullptr, nullptr, voltronicStatus }, /* Utility Fail (Immediate) */
    { "ups.status", 0, nullptr, "QGS\r", 76, '(', 66, 66, "%s", QX_FLAG_QUICK_POLL, nullptr, nullptr, voltronicStatus }, /* Battery Low */
    { "ups.status", 0, nullptr, "QGS\r", 76, '(', 67, 67, "%s", QX_FLAG_QUICK_POLL, nullptr, nullptr, voltronicStatus }, /* Bypass/Boost or Buck Active */
    { "ups.alarm", 0, nullptr, "QGS\r", 76, '(', 67, 67, "%s", 0, nullptr, nullptr, voltronicStatus }, /* Bypass/Boost or Buck Active */
    { "ups.alarm", 0, nullptr, "QGS\r", 76, '(', 68, 68, "%s", 0, nullptr, nullptr, voltronicStatus }, /* UPS Fault */
    // { "unknown.4", 0, nullptr, "QGS\r", 76, '(', 69, 69, "%s", 0, nullptr, nullptr, voltronicStatus }, /* Unknown */
    { "ups.status", 0, nullptr, "QGS\r", 76, '(', 70, 70, "%s", QX_FLAG_QUICK_POLL, nullptr, nullptr, voltronicStatus }, /* Test in Progress */
    { "ups.status", 0, nullptr, "QGS\r", 76, '(', 71, 71, "%s", QX_FLAG_QUICK_POLL, nullptr, nullptr, voltronicStatus }, /* Shutdown Active */
    { "ups.beeper.status", 0, nullptr, "QGS\r", 76, '(', 72, 72, "%s", 0, nullptr, nullptr, voltronicStatus }, /* Beeper status - ups.beeper.status */
    // { "unknown.5", 0, nullptr, "QGS\r", 76, '(', 73, 73, "%s", 0, nullptr, nullptr, voltronicStatus }, /* Unknown */
    // { "unknown.6", 0, nullptr, "QGS\r", 76, '(', 74, 74, "%s", 0, nullptr, nullptr, voltronicStatus }, /* Unknown */

    /* Query UPS for actual working mode
     * > [QMOD\r]
     * < [(S\r]
     *    012
     *    0
     */

    { "ups.alarm", 0, nullptr, "QMOD\r", 3, '(', 1, 1, "%s", 0, nullptr, nullptr, voltronicMode },
    { "ups.status", 0, nullptr, "QMOD\r", 3, '(', 1, 1, "%s", 0, nullptr, nullptr, voltronicMode },

    /* Query UPS for faults and their type. Unskipped when a fault is found in 12bit flag of QGS, otherwise you'll get a fake reply.
     * > [QFS\r]
     * < [(OK\r] <- No fault
     *    0123
     *    0
     * < [(14 212.1 50.0 005.6 49.9 006 010.6 343.8 ---.- 026.2 021.8 01101100\r] <- Fault type + Short status
     *    012345678901234567890123456789012345678901234567890123456789012345678
     *    0         1         2         3         4         5         6
     */

    { "ups.alarm", 0, nullptr, "QFS\r", 4, '(', 1, 2, "%s", QX_FLAG_SKIP, nullptr, nullptr, voltronicFault },

    /* Query UPS for warnings and their type
     * > [QWS\r]
     * < [(0000000100000000000000000000000000000000000000000000000000000000\r]
     *    012345678901234567890123456789012345678901234567890123456789012345
     *    0         1         2         3         4         5         6
     */

    { "ups.alarm", 0, nullptr, "QWS\r", 66, '(', 1, 64, "%s", 0, nullptr, nullptr, voltronicWarning },

    /* Query UPS for actual infos about battery
     * > [QBV\r]
     * < [(026.5 02 01 068 255\r]
     *    012345678901234567890
     *    0         1         2
     *
     * NOTE: PowerWalker VFI 1500 CG PF1 as of
     * https://github.com/networkupstools/nut/issues/2765
     * reported a longer last component, so we do not expect
     * an exact ending location here, just "to end of reply":
     * < [(041.0 03 01 100 00037\r]
     *    01234567890123456789012
     *    0         1         2
     */

    { "battery.voltage", 0, nullptr, "QBV\r", 21, '(', 1, 5, "%.2f", 0, nullptr, nullptr, QxEngine::qxMultiplyBattvolt },
    { "battery_number", QX_ST_FLAG_RW, VOLTRONIC_R_BATT_NUMB, "QBV\r", 21, '(', 7, 9, "%d", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_NONUT, nullptr, nullptr, voltronicBattNumb }, /* Number of batteries that make a pack */
    { "battery.packs", QX_ST_FLAG_RW, VOLTRONIC_R_BATT_PACKS, "QBV\r", 21, '(', 10, 11, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE, nullptr, nullptr, nullptr }, /* Number of battery packs in parallel */
    { "battery.charge", 0, nullptr, "QBV\r", 21, '(', 13, 15, "%.0f", 0, nullptr, nullptr, nullptr },
    { "battery.runtime", 0, nullptr, "QBV\r", 21, '(', 17, 0, "%.0f", 0, nullptr, nullptr, voltronicBattRuntime },

    /* Query UPS for last seen min/max load level
     * > [QLDL\r]
     * < [(021 023\r] <- minimum load level - maximum load level
     *    012345678
     *    0
     */

    { "output.power.minimum.percent", 0, nullptr, "QLDL\r", 9, '(', 1, 3, "%.0f", 0, nullptr, nullptr, nullptr },
    { "output.power.maximum.percent", 0, nullptr, "QLDL\r", 9, '(', 5, 7, "%.0f", 0, nullptr, nullptr, nullptr },

    /* Query UPS for multi-phase voltages/frequencies
     * > [Q3**\r]
     * < [(123.4 123.4 123.4 123.4 123.4 123.4\r] <- Q3PV
     * < [(123.4 123.4 123.4 123.4 123.4 123.4\r] <- Q3OV
     * < [(123 123 123\r] <- Q3PC
     * < [(123 123 123\r] <- Q3OC
     * < [(123 123 123\r] <- Q3LD
     * < [(123.4 123.4 123.4\r] <- Q3YV - P09 protocol
     * < [(123.4 123.4 123.4 123.4 123.4 123.4\r] <- Q3YV - P10/P03 protocols
     *    0123456789012345678901234567890123456
     *    0         1         2         3
     *
     * P09 = 2-phase input/2-phase output
     * Q3PV (Input Voltage L1 | Input Voltage L2 | Input Voltage L3 | Input Voltage L1-L2 | Input Voltage L1-L3 | Input Voltage L2-L3
     * Q3OV (Output Voltage L1 | Output Voltage L2 | Output Voltage L3 | Output Voltage L1-L2 | Output Voltage L1-L3 | Output Voltage L2-L3
     * Q3PC (Input Current L1 | Input Current L2 | Input Current L3
     * Q3OC (Output Current L1 | Output Current L2 | Output Current L3
     * Q3LD (Output Load Level L1 | Output Load Level L2 | Output Load Level L3
     * Q3YV (Output Bypass Voltage L1 | Output Bypass Voltage L2 | Output Bypass Voltage L3
     *
     * P10 = 3-phase input/3-phase output / P03 = 3-phase input/ 1-phase output
     * Q3PV (Input Voltage L1 | Input Voltage L2 | Input Voltage L3 | Input Voltage L1-L2 | Input Voltage L2-L3 | Input Voltage L1-L3
     * Q3OV (Output Voltage L1 | Output Voltage L2 | Output Voltage L3 | Output Voltage L1-L2 | Output Voltage L2-L3 | Output Voltage L1-L3
     * Q3PC (Input Current L1 | Input Current L2 | Input Current L3
     * Q3OC (Output Current L1 | Output Current L2 | Output Current L3
     * Q3LD (Output Load Level L1 | Output Load Level L2 | Output Load Level L3
     * Q3YV (Output Bypass Voltage L1 | Output Bypass Voltage L2 | Output Bypass Voltage L3 | Output Bypass Voltage L1-L2 | Output Bypass Voltage L2-L3 | Output Bypass Voltage L1-L3
     *
     */

    /* From Q3PV */
    { "input.L1-N.voltage", 0, nullptr, "Q3PV\r", 37, '(', 1, 5, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L2-N.voltage", 0, nullptr, "Q3PV\r", 37, '(', 7, 11, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L3-N.voltage", 0, nullptr, "Q3PV\r", 37, '(', 13, 17, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L1-L2.voltage", 0, nullptr, "Q3PV\r", 37, '(', 19, 23, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L2-L3.voltage", 0, nullptr, "Q3PV\r", 37, '(', 25, 29, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L1-L3.voltage", 0, nullptr, "Q3PV\r", 37, '(', 31, 35, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    // { "input.L1-L3.voltage", 0, nullptr, "Q3PV\r", 37, '(', 25, 29, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 *//* Commented out because P09 should be two-phase input/output UPSes */
    // { "input.L2-L3.voltage", 0, nullptr, "Q3PV\r", 37, '(', 31, 35, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 *//* Commented out because P09 should be two-phase input/output UPSes */

    /* From Q3PC */
    { "input.L1.current", 0, nullptr, "Q3PC\r", 13, '(', 1, 3, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L2.current", 0, nullptr, "Q3PC\r", 13, '(', 5, 7, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "input.L3.current", 0, nullptr, "Q3PC\r", 13, '(', 9, 11, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* From Q3OV */
    { "output.L1-N.voltage", 0, nullptr, "Q3OV\r", 37, '(', 1, 5, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L2-N.voltage", 0, nullptr, "Q3OV\r", 37, '(', 7, 11, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L3-N.voltage", 0, nullptr, "Q3OV\r", 37, '(', 13, 17, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L1-L2.voltage", 0, nullptr, "Q3OV\r", 37, '(', 19, 23, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L2-L3.voltage", 0, nullptr, "Q3OV\r", 37, '(', 25, 29, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L1-L3.voltage", 0, nullptr, "Q3OV\r", 37, '(', 31, 35, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    // { "output.L1-L3.voltage", 0, nullptr, "Q3OV\r", 37, '(', 25, 29, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 *//* Commented out because P09 should be two-phase input/output UPSes */
    // { "output.L2-L3.voltage", 0, nullptr, "Q3OV\r", 37, '(', 31, 35, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 *//* Commented out because P09 should be two-phase input/output UPSes */

    /* From Q3OC */
    { "output.L1.current", 0, nullptr, "Q3OC\r", 13, '(', 1, 3, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L2.current", 0, nullptr, "Q3OC\r", 13, '(', 5, 7, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L3.current", 0, nullptr, "Q3OC\r", 13, '(', 9, 11, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* From Q3LD */
    { "output.L1.power.percent", 0, nullptr, "Q3LD\r", 13, '(', 1, 3, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L2.power.percent", 0, nullptr, "Q3LD\r", 13, '(', 5, 7, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.L3.power.percent", 0, nullptr, "Q3LD\r", 13, '(', 9, 11, "%.0f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* From Q3YV */
    { "output.bypass.L1-N.voltage", 0, nullptr, "Q3YV\r", 37, '(', 1, 5, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.bypass.L2-N.voltage", 0, nullptr, "Q3YV\r", 37, '(', 7, 11, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.bypass.L3-N.voltage", 0, nullptr, "Q3YV\r", 37, '(', 13, 17, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.bypass.L1-N.voltage", 0, nullptr, "Q3YV\r", 19, '(', 1, 5, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 */
    { "output.bypass.L2-N.voltage", 0, nullptr, "Q3YV\r", 19, '(', 7, 11, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 */
    // { "output.bypass.L3-N.voltage", 0, nullptr, "Q3YV\r", 19, '(', 13, 17, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr }, /* P09 *//* Commented out because P09 should be two-phase input/output UPSes */
    { "output.bypass.L1-L2.voltage", 0, nullptr, "Q3YV\r", 37, '(', 19, 23, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.bypass.L2-L3.voltage", 0, nullptr, "Q3YV\r", 37, '(', 25, 29, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "output.bypass.L1-L3.voltage", 0, nullptr, "Q3YV\r", 37, '(', 31, 35, "%.1f", QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* Query UPS for capability - total options available: 23; only those whom the UPS is capable of are reported as Enabled or Disabled
     * > [QFLAG\r]
     * < [(EpashcDbroegfl\r]
     *    0123456789012345
     *    0         1 * min length = ( + E + D + \r = 4
     */

    { "ups.start.auto", QX_ST_FLAG_RW, VOLTRONIC_E_CAP, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM, nullptr, nullptr, voltronicCapability },
    { "battery.protection", QX_ST_FLAG_RW, VOLTRONIC_E_CAP, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM, nullptr, nullptr, voltronicCapability },
    { "battery.energysave", QX_ST_FLAG_RW, VOLTRONIC_E_CAP, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM, nullptr, nullptr, voltronicCapability },
    { "ups.start.battery", QX_ST_FLAG_RW, VOLTRONIC_E_CAP, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM, nullptr, nullptr, voltronicCapability },
    { "outlet.0.switchable", QX_ST_FLAG_RW, VOLTRONIC_E_CAP, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM, nullptr, nullptr, voltronicCapability },
    /* Not available in NUT */
    { "bypass_alarm", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "battery_alarm", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "bypass_when_off", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "alarm_control", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "converter_mode", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "eco_mode", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "battery_open_status_check", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "bypass_forbidding", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "site_fault_detection", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "advanced_eco_mode", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "constant_phase_angle", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },
    { "limited_runtime_on_battery", 0, nullptr, "QFLAG\r", 4, '(', 1, 0, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicCapability },

    /*   Enable or   Disable or Reset to safe default values capability options
     * > [PEX\r]  > [PDX\r]  > [PF\r]
     * < [(ACK\r]  < [(ACK\r]  < [(ACK\r]
     *    01234     01234     01234
     *    0      0      0
     */

    { "ups.start.auto", 0, VOLTRONIC_E_CAP, "P%sR\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySet },
    { "battery.protection", 0, VOLTRONIC_E_CAP, "P%sS\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySet },
    { "battery.energysave", 0, VOLTRONIC_E_CAP, "P%sG\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySet },
    { "ups.start.battery", 0, VOLTRONIC_E_CAP, "P%sC\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySet },
    { "outlet.0.switchable", 0, VOLTRONIC_E_CAP, "P%sJ\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySet },
    /* Not available in NUT */
    { "reset_to_default", 0, nullptr, "PF\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilityReset },
    { "bypass_alarm", 0, VOLTRONIC_E_CAP_NONUT, "P%sP\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "battery_alarm", 0, VOLTRONIC_E_CAP_NONUT, "P%sB\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "bypass_when_off", 0, VOLTRONIC_E_CAP_NONUT, "P%sO\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "alarm_control", 0, VOLTRONIC_E_CAP_NONUT, "P%sA\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "converter_mode", 0, VOLTRONIC_E_CAP_NONUT, "P%sV\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "eco_mode", 0, VOLTRONIC_E_CAP_NONUT, "P%sE\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "battery_open_status_check", 0, VOLTRONIC_E_CAP_NONUT, "P%sD\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "bypass_forbidding", 0, VOLTRONIC_E_CAP_NONUT, "P%sF\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "site_fault_detection", 0, VOLTRONIC_E_CAP_NONUT, "P%sL\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "advanced_eco_mode", 0, VOLTRONIC_E_CAP_NONUT, "P%sN\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "constant_phase_angle", 0, VOLTRONIC_E_CAP_NONUT, "P%sQ\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },
    { "limited_runtime_on_battery", 0, VOLTRONIC_E_CAP_NONUT, "P%sW\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicCapabilitySetNonut },

    /* Query UPS for programmable outlet (1-4) status
     * > [QSK1\r]
     * < [(1\r] <- if outlet is on -> (1 , if off -> (0 ; (NAK -> outlet is not programmable
     *    012
     *    0
     */

    { "outlet.1.switchable", 0, nullptr, "QSK1\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.1.status", 0, nullptr, "QSK1\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.2.switchable", 0, nullptr, "QSK2\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.2.status", 0, nullptr, "QSK2\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.3.switchable", 0, nullptr, "QSK3\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.3.status", 0, nullptr, "QSK3\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.4.switchable", 0, nullptr, "QSK4\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },
    { "outlet.4.status", 0, nullptr, "QSK4\r", 3, '(', 1, 1, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutlet },

    /* Query UPS for programmable outlet n (1-4) delay time before it shuts down the load when on battery mode
     * > [QSKT1\r]
     * < [(008\r] <- if delay time is set (by PSK[1-4]n) it'll report n (minutes) otherwise it'll report (NAK (also if outlet is not programmable)
     *    01234
     *    0
     */

    { "outlet.1.delay.shutdown", QX_ST_FLAG_RW, VOLTRONIC_R_OUTLET_DELAY, "QSKT1\r", 5, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelay },
    { "outlet.2.delay.shutdown", QX_ST_FLAG_RW, VOLTRONIC_R_OUTLET_DELAY, "QSKT2\r", 5, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelay },
    { "outlet.3.delay.shutdown", QX_ST_FLAG_RW, VOLTRONIC_R_OUTLET_DELAY, "QSKT3\r", 5, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelay },
    { "outlet.4.delay.shutdown", QX_ST_FLAG_RW, VOLTRONIC_R_OUTLET_DELAY, "QSKT4\r", 5, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelay },

    /* Set delay time for programmable outlets
     * > [PSK1nnn\r] n = 0..9
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "outlet.1.delay.shutdown", 0, VOLTRONIC_R_OUTLET_DELAY, "PSK1%03d\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelaySet },
    { "outlet.2.delay.shutdown", 0, VOLTRONIC_R_OUTLET_DELAY, "PSK2%03d\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelaySet },
    { "outlet.3.delay.shutdown", 0, VOLTRONIC_R_OUTLET_DELAY, "PSK3%03d\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelaySet },
    { "outlet.4.delay.shutdown", 0, VOLTRONIC_R_OUTLET_DELAY, "PSK4%03d\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicOutletDelaySet },

    /* Query UPS for ECO Mode voltage limits
     * > [QHE\r]
     * < [(242 218\r]
     *    012345678
     *    0
     */

    { "input.transfer.high", QX_ST_FLAG_RW, VOLTRONIC_R_ECO_VOLT_MAX, "QHE\r", 9, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoVolt },
    { "input.transfer.low", QX_ST_FLAG_RW, VOLTRONIC_R_ECO_VOLT_MIN, "QHE\r", 9, '(', 5, 7, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoVolt },
    { "input.transfer.low.min", 0, nullptr, "QHE\r", 9, '(', 5, 7, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoVoltRange },
    { "input.transfer.low.max", 0, nullptr, "QHE\r", 9, '(', 5, 7, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoVoltRange },
    { "input.transfer.high.min", 0, nullptr, "QHE\r", 9, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoVoltRange },
    { "input.transfer.high.max", 0, nullptr, "QHE\r", 9, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoVoltRange },

    /* Set ECO Mode voltage limits
     * > [HEHnnn\r]  > [HELnnn\r]  n = 0..9
     * < [(ACK\r]  < [(ACK\r]
     *    01234     01234
     *    0      0
     */

    { "input.transfer.high", 0, VOLTRONIC_R_ECO_VOLT_MAX, "HEH%03.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },
    { "input.transfer.low", 0, VOLTRONIC_R_ECO_VOLT_MIN, "HEL%03.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },

    /* Query UPS for ECO Mode frequency limits
     * > [QFRE\r]
     * < [(53.0 47.0\r]
     *    01234567890
     *    0         1
     */

    { "input.frequency.high", QX_ST_FLAG_RW, VOLTRONIC_R_ECO_FREQ_MAX, "QFRE\r", 11, '(', 1, 4, "%.1f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoFreq },
    { "input.frequency.low", QX_ST_FLAG_RW, VOLTRONIC_R_ECO_FREQ_MIN, "QFRE\r", 11, '(', 6, 9, "%.1f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicEcoFreq },

    /* Set ECO Mode frequency limits
     * > [FREHnn.n\r] > [FRELnn.n\r]  n = 0..9
     * < [(ACK\r]  < [(ACK\r]
     *    01234     01234
     *    0      0
     */

    { "input.frequency.high", 0, VOLTRONIC_R_ECO_FREQ_MAX, "FREH%04.1f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },
    { "input.frequency.low", 0, VOLTRONIC_R_ECO_FREQ_MIN, "FREL%04.1f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },

    /* Query UPS for Bypass Mode voltage limits
     * > [QBYV\r]
     * < [(264 170\r]
     *    012345678
     *    0
     */

    { "max_bypass_volt", QX_ST_FLAG_RW, VOLTRONIC_R_BYPASS_VOLT_MAX, "QBYV\r", 9, '(', 1, 3, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicBypass },
    { "min_bypass_volt", QX_ST_FLAG_RW, VOLTRONIC_R_BYPASS_VOLT_MIN, "QBYV\r", 9, '(', 5, 7, "%.0f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicBypass },

    /* Set Bypass Mode voltage limits
     * > [PHVnnn\r]  > [PLVnnn\r]  n = 0..9
     * < [(ACK\r]  < [(ACK\r]
     *    01234     01234
     *    0      0
     */

    { "max_bypass_volt", 0, VOLTRONIC_R_BYPASS_VOLT_MAX, "PHV%03.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },
    { "min_bypass_volt", 0, VOLTRONIC_R_BYPASS_VOLT_MIN, "PLV%03.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },

    /* Query UPS for Bypass Mode frequency limits
     * > [QBYF\r]
     * < [(53.0 47.0\r]
     *    01234567890
     *    0         1
     */

    { "max_bypass_freq", QX_ST_FLAG_RW, VOLTRONIC_R_BYPASS_FREQ_MAX, "QBYF\r", 11, '(', 1, 4, "%.1f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicBypass },
    { "min_bypass_freq", QX_ST_FLAG_RW, VOLTRONIC_R_BYPASS_FREQ_MIN, "QBYF\r", 11, '(', 6, 9, "%.1f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicBypass },

    /* Set Bypass Mode frequency limits
     * > [PGFnn.n\r] > [PSFnn.n\r]  n = 0..9
     * < [(ACK\r]  < [(ACK\r]
     *    01234     01234
     *    0      0
     */

    { "max_bypass_freq", 0, VOLTRONIC_R_BYPASS_FREQ_MAX, "PGF%04.1f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },
    { "min_bypass_freq", 0, VOLTRONIC_R_BYPASS_FREQ_MIN, "PSF%04.1f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicProcessSetvar },

    /* Set number of batteries that make a pack to n (integer, 1-9). NOTE: changing the number of batteries will change the UPS's estimation on battery charge/runtime
     * > [BATNn\r]
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "battery_number", 0, VOLTRONIC_R_BATT_NUMB, "BATN%1.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE | QX_FLAG_NONUT, nullptr, nullptr, voltronicProcessSetvar },

    /* Set number of battery packs in parallel to n (integer, 01-99). NOTE: changing the number of battery packs will change the UPS's estimation on battery charge/runtime
     * > [BATGNn\r]
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "battery.packs", 0, VOLTRONIC_R_BATT_PACKS, "BATGN%02.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, voltronicProcessSetvar },

    /* Query UPS for battery type (Only P31)
     * > [QBT\r]
     * < [(01\r] <- 00="Li", 01="Flooded" or 02="AGM"
     *    0123
     *    0
     */

    { "battery.type", QX_ST_FLAG_RW, VOLTRONIC_E_BATT_TYPE, "QBT\r", 4, '(', 1, 2, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicP31b },

    /* Set battery type (Only P31)
     * > [PBTnn\r]  nn = 00/01/02
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "battery.type", 0, VOLTRONIC_E_BATT_TYPE, "PBT%02.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_SKIP, nullptr, nullptr, voltronicP31bSet },

    /* Query UPS for device grid working range (Only P31)
     * > [QGR\r]
     * < [(01\r] <- 00=Appliance, 01=UPS
     *    0123
     *    0
     */

    { "work_range_type", QX_ST_FLAG_RW, VOLTRONIC_E_WORK_RANGE, "QGR\r", 4, '(', 1, 2, "%s", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicP31g },

    /* Set device grid working range type (Only P31)
     * > [PBTnn\r]  nn = 00/01
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "work_range_type", 0, VOLTRONIC_E_WORK_RANGE, "PGR%02.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicP31gSet },

    /* Query UPS for battery low voltage
     * > [RE0\r]
     * < [#20\r]
     *    012
     *    0
     */

    { "battery.voltage.low", QX_ST_FLAG_RW, VOLTRONIC_R_BATT_LOW, "RE0\r", 3, '#', 1, 2, "%.1f", QX_FLAG_SEMI_STATIC | QX_FLAG_RANGE, nullptr, nullptr, nullptr },

    /* Set voltage for battery low to n (integer, 20..24/20..28). NOTE: changing the battery low voltage will change the UPS's estimation on battery charge/runtime
     * > [W0En\r]
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "battery.voltage.low", 0, VOLTRONIC_R_BATT_LOW, "W0E%02.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, voltronicProcessSetvar },

    /* Query UPS for Phase Angle
     * > [QPD\r]
     * < [(000 120\r] <- Input Phase Angle - Output Phase Angle
     *    012345678
     *    0
     */

    { "input_phase_angle", 0, nullptr, "QPD\r", 9, '(', 1, 3, "%03d", QX_FLAG_SEMI_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicPhase },
    { "output_phase_angle", QX_ST_FLAG_RW, VOLTRONIC_E_PHASE, "QPD\r", 9, '(', 5, 7, "%03d", QX_FLAG_SEMI_STATIC | QX_FLAG_ENUM | QX_FLAG_NONUT, nullptr, nullptr, voltronicPhase },

    /* Set output phase angle
     * > [PPDn\r]  n = (000, 120, 180 or 240)
     * < [(ACK\r]
     *    01234
     *    0
     */

    { "output_phase_angle", 0, VOLTRONIC_E_PHASE, "PPD%03.0f\r", 5, '(', 1, 3, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, voltronicPhaseSet },

    /* Query UPS for master/slave for a system of UPSes in parallel
     * > [QPAR\r]
     * < [(001\r] <- 001 for master UPS, 002 and 003 for slave UPSes
     *    01234
     *    0
     */

    { "voltronic_parallel", 0, nullptr, "QPAR\r", 5, '(', 1, 3, "%s", QX_FLAG_STATIC | QX_FLAG_NONUT, nullptr, nullptr, voltronicParallel },

    /* Query UPS for ??
     * > [QBDR\r]
     * < [(1234\r] <- unknown reply - My UPS (NAK at me
     *    012345
     *    0
     */

    { "unknown.7", 0, nullptr, "QBDR\r", 5, '(', 1, 0, "%s", QX_FLAG_STATIC | QX_FLAG_NONUT | QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* Instant commands */
    { "load.off", 0, nullptr, "SOFF\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "load.on", 0, nullptr, "SON\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },

    { "shutdown.return", 0, nullptr, "S%s\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, voltronicProcessCommand },
    { "shutdown.stayoff", 0, nullptr, "S%sR0000\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, voltronicProcessCommand },
    { "shutdown.stop", 0, nullptr, "CS\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },

    { "test.battery.start", 0, nullptr, "T%s\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, voltronicProcessCommand },
    { "test.battery.start.deep", 0, nullptr, "TL\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "test.battery.start.quick", 0, nullptr, "T\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "test.battery.stop", 0, nullptr, "CT\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },

    { "beeper.toggle", 0, nullptr, "BZ%s\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD, nullptr, nullptr, voltronicProcessCommand },
    /* Enable/disable beeper: unskipped if the UPS can control alarm (capability) */
    { "beeper.enable", 0, nullptr, "PEA\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "beeper.disable", 0, nullptr, "PDA\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* Outlet control: unskipped if the outlets are manageable */
    { "outlet.1.load.off", 0, nullptr, "SKOFF1\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.1.load.on", 0, nullptr, "SKON1\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.2.load.off", 0, nullptr, "SKOFF2\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.2.load.on", 0, nullptr, "SKON2\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.3.load.off", 0, nullptr, "SKOFF3\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.3.load.on", 0, nullptr, "SKON3\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.4.load.off", 0, nullptr, "SKOFF4\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "outlet.4.load.on", 0, nullptr, "SKON4\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* Bypass: unskipped if the UPS is capable of ECO Mode */
    { "bypass.start", 0, nullptr, "PEE\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },
    { "bypass.stop", 0, nullptr, "PDE\r", 5, '(', 1, 3, nullptr, QX_FLAG_CMD | QX_FLAG_SKIP, nullptr, nullptr, nullptr },

    /* Server-side settable vars */
    { "ups.delay.start", QX_ST_FLAG_RW, VOLTRONIC_R_ONDELAY, nullptr, 0, 0, 0, 0, "180", QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, voltronicProcessSetvar },
    { "ups.delay.shutdown", QX_ST_FLAG_RW, VOLTRONIC_R_OFFDELAY, nullptr, 0, 0, 0, 0, "30", QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, voltronicProcessSetvar },

    /* End of structure. */
    { nullptr, 0, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr }
};


/* == Support functions == */

/* This function allows the subdriver to "claim" a device: return 1 if the device is supported
 * by this subdriver, else 0. */
static bool voltronicClaim(QxEngine& qx) {
    int queryResult;

    /* We need at least QGS and QPI to run this subdriver */

    int item = qx.findNutInfo("input.voltage", 0, 0);

    /* Don't know what happened - should have looked up in the mapping table here! */
    if (item < 0) return false;

    /* No reply/Unable to get value */
    if ((queryResult = qx.process(item))) {
        // "failed to get 'input.voltage'"

        if (qx.timedOut()) {
            // "Sometimes the device is laggy, and we could have posted many queries and the
            // buffer is full of replies to them (or it is still producing the answers); try to
            // sleep, flush it and ask again": arbitrary 5 s delay for the device to maybe
            // produce answers to earlier voltage requests, then retry the query, buffers will
            // be flushed then
            qx.sleepMs(5000);
            queryResult = qx.process(item);
        }

        if (queryResult) {
            /* Not a known timeout/zero-read initially, or still a bad response */
            return false;
        }
    }

    /* Unable to process value */
    if (qx.infovalSet(item) != 1) return false;

    /* UPS Protocol */
    item = qx.findNutInfo("ups.firmware.aux", 0, 0);

    /* Don't know what happened */
    if (item < 0) {
        qx.delinfo("input.voltage");
        return false;
    }

    /* No reply/Unable to get value */
    if (qx.process(item)) {
        qx.delinfo("input.voltage");
        return false;
    }

    /* Unable to process value/Protocol out of range */
    if (qx.infovalSet(item) != 1) {
        qx.delinfo("input.voltage");
        return false;
    }

    return true;
}

// voltronic_makevartable() (1752-1786): not ported, ups.conf options are not offered

/* Unskip vars according to protocol used by the UPS */
static void voltronicMassiveUnskip(QxEngine& qx, const long protocol) {
    for (size_t r = 0; r < qx.rowCount(); r++) {
        const int row = (int)r;
        const QxItem& item = qx.item(row);
        const char* type = item.info_type;
        const uint32_t qxflags = qx.flags(row);

        if (!item.command) continue;

        if (/* == Multiphase UPSes == */
            /* P09 */
            /* (the commented out conditions: "Not unskipped because P09 should be 2-phase input/output") */
            (protocol == 9 && (
            /*  (eq(type, "input.L1-L3.voltage") && item.from == 25) ||  */
            /*  (eq(type, "input.L2-L3.voltage") && item.from == 31) ||  */
            /*  (eq(type, "output.L1-L3.voltage") && item.from == 25) || */
            /*  (eq(type, "output.L2-L3.voltage") && item.from == 31) || */
                (eq(type, "output.bypass.L1-N.voltage") && item.answer_len == 19) ||
                (eq(type, "output.bypass.L2-N.voltage") && item.answer_len == 19) /* ||
                (eq(type, "output.bypass.L3-N.voltage") && item.answer_len == 19) */
            )) ||
            /* P10 */
            (protocol == 10 && (
                eq(type, "output.L3-N.voltage") ||
                (eq(type, "output.L2-L3.voltage") && item.from == 25) ||
                (eq(type, "output.L1-L3.voltage") && item.from == 31) ||
                (eq(type, "output.bypass.L1-N.voltage") && item.answer_len == 37) ||
                (eq(type, "output.bypass.L2-N.voltage") && item.answer_len == 37) ||
                (eq(type, "output.bypass.L3-N.voltage") && item.answer_len == 37) ||
                eq(type, "output.bypass.L1-L2.voltage") ||
                eq(type, "output.bypass.L2-L3.voltage") ||
                eq(type, "output.bypass.L1-L3.voltage") ||
                eq(type, "output.L3.current") ||
                eq(type, "output.L3.power.percent")
            )) ||
            /* P09-P10 */
            ((protocol == 9 || protocol == 10) && (
                eq(type, "output.L1-N.voltage") ||
                eq(type, "output.L2-N.voltage") || /*
                eq(type, "output.L3-N.voltage") || */ /* Not unskipped because P09 should be 2-phase input/output */
                eq(type, "output.L1-L2.voltage") ||
                eq(type, "output.L1.current") ||
                eq(type, "output.L2.current") || /*
                eq(type, "output.L3.current") || */ /* Not unskipped because P09 should be 2-phase input/output */
                eq(type, "output.L1.power.percent") ||
                eq(type, "output.L2.power.percent") /* ||
                eq(type, "output.L3.power.percent") */ /* Not unskipped because P09 should be 2-phase input/output */
            )) ||
            /* P03-P09-P10 */
            ((protocol == 3 || protocol == 9 || protocol == 10) && (
                eq(type, "input.L1-N.voltage") ||
                eq(type, "input.L2-N.voltage") || /*
                eq(type, "input.L3-N.voltage") || */ /* Not unskipped because P09 should be 2-phase input/output */
                eq(type, "input.L1-L2.voltage") ||
                eq(type, "input.L1.current") ||
                eq(type, "input.L2.current") /* ||
                eq(type, "input.L3.current") */ /* Not unskipped because P09 should be 2-phase input/output */
            )) ||
            /* P03-P10 */
            ((protocol == 3 || protocol == 10) && (
                eq(type, "input.L3-N.voltage") ||
                (eq(type, "input.L2-L3.voltage") && item.from == 25) ||
                (eq(type, "input.L1-L3.voltage") && item.from == 31) ||
                eq(type, "input.L3.current")
            )) ||
            /* == P31 battery type/device grid working range == */
            (protocol == 31 && (
                eq(type, "battery.type") ||
                (eq(type, "work_range_type") && !(qxflags & QX_FLAG_SETVAR)) ||
                (eq(type, "work_range_type") && (qxflags & QX_FLAG_SETVAR) && getval(type))
            )) ||
            /* == ByPass limits: all but P00/P08/P31 == */
            (protocol != 0 && protocol != 8 && protocol != 31 && (
                (eq(type, "max_bypass_volt") && !(qxflags & QX_FLAG_SETVAR)) ||
                (eq(type, "min_bypass_volt") && !(qxflags & QX_FLAG_SETVAR)) ||
                (eq(type, "max_bypass_freq") && !(qxflags & QX_FLAG_SETVAR)) ||
                (eq(type, "min_bypass_freq") && !(qxflags & QX_FLAG_SETVAR))
            )) ||
            /* == Reset capabilities options to safe default values == */
            (eq(type, "reset_to_default") && testvar("reset_to_default")) ||
            /* == QBDR (unknown) == */
            (eq(type, "unknown.7") && testvar("testing"))
        ) {
            qx.clearFlags(row, QX_FLAG_SKIP);
        }
    }
}

// Not in upstream: the runtime info_rw of a row (QxSubdriver::info_rw). Upstream
// voltronic_eco_volt() writes the values of voltronic_r_eco_volt_max/min into the table
// itself; here they are in VoltronicState, and the rows whose info_rw is one of those two
// tables get them from here. Every other row keeps its static table.
static const QxInfoRw* voltronicInfoRw(QxEngine& qx, const QxItem& item) {
    VoltronicState& s = state(qx);
    if (item.info_rw == VOLTRONIC_R_ECO_VOLT_MAX) {
        s.r_eco_volt_max[0] = { s.eco_volt_max[0], nullptr };
        s.r_eco_volt_max[1] = { s.eco_volt_max[1], nullptr };
        s.r_eco_volt_max[2] = { "", nullptr };
        return s.r_eco_volt_max;
    }
    if (item.info_rw == VOLTRONIC_R_ECO_VOLT_MIN) {
        s.r_eco_volt_min[0] = { s.eco_volt_min[0], nullptr };
        s.r_eco_volt_min[1] = { s.eco_volt_min[1], nullptr };
        s.r_eco_volt_min[2] = { "", nullptr };
        return s.r_eco_volt_min;
    }
    return item.info_rw;
}


/* == Preprocess functions == */

/* *SETVAR(/NONUT)* Preprocess setvars */
static int voltronicProcessSetvar(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    VoltronicState& s = state(qx);
    double val;

    // "value not given for %s"
    if (!strlen(value)) return -1;

    val = strtod(value, nullptr);

    if (eq(item.info_type, "ups.delay.start")) {
        /* Truncate to minute */
        val -= ((int)val % 60);

        snprintf(value, valuelen, "%.0f", val);

        return 0;

    } else if (eq(item.info_type, "ups.delay.shutdown")) {
        /* Truncate to nearest setable value */
        if (val < 60) {
            val -= ((int)val % 6);
        } else {
            val -= ((int)val % 60);
        }

        snprintf(value, valuelen, "%.0f", val);

        return 0;

    } else if (eq(item.info_type, "max_bypass_freq")) {
        if (dEqual(val, s.max_bypass_freq)) {
            // upslogx(LOG_INFO, "%s is already set to %.1f", item->info_type, val)
            return -1;
        }

    } else if (eq(item.info_type, "min_bypass_freq")) {
        if (dEqual(val, s.min_bypass_freq)) {
            // upslogx(LOG_INFO, "%s is already set to %.1f", item->info_type, val)
            return -1;
        }

    } else if (eq(item.info_type, "max_bypass_volt")) {
        if (dEqual(val, (double)s.max_bypass_volt)) {
            // upslogx(LOG_INFO, "%s is already set to %.0f", item->info_type, val)
            return -1;
        }

    } else if (eq(item.info_type, "min_bypass_volt")) {
        if (dEqual(val, (double)s.min_bypass_volt)) {
            // upslogx(LOG_INFO, "%s is already set to %.0f", item->info_type, val)
            return -1;
        }

    } else if (eq(item.info_type, "battery_number")) {
        if (dEqual(val, (double)s.battery_number)) {
            // upslogx(LOG_INFO, "%s is already set to %.0f", item->info_type, val)
            return -1;
        }
    }

    // snprintf_dynamic(value, valuelen, item->command, "%f", val): one floating point
    // conversion in the command of every row that gets here
    snprintf(value, valuelen, item.command, val);

    return 0;
}

/* *CMD* Preprocess instant commands */
static int voltronicProcessCommand(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    // SMALLBUF upstream; the longest value is ".%ldR%04ld"
    char buf[64] = "";

    if (eq(item.info_type, "shutdown.return")) {
        /* Sn: Shutdown after n minutes and then turn on when mains is back
         * SnRm: Shutdown after n minutes and then turn on after m minutes
         * Accepted values for n: .2 -> .9 , 01 -> 99
         * Accepted values for m: 0001 -> 9999 */

        long offdelay = strtol(infoOrEmpty(qx, "ups.delay.shutdown"), nullptr, 10),
             ondelay = strtol(infoOrEmpty(qx, "ups.delay.start"), nullptr, 10) / 60;

        if (ondelay == 0) {
            if (offdelay < 60) {
                snprintf(buf, sizeof(buf), ".%ld", offdelay / 6);
            } else {
                snprintf(buf, sizeof(buf), "%02ld", offdelay / 60);
            }

        } else if (offdelay < 60) {
            snprintf(buf, sizeof(buf), ".%ldR%04ld", offdelay / 6, ondelay);

        } else {
            snprintf(buf, sizeof(buf), "%02ldR%04ld", offdelay / 60, ondelay);
        }

    } else if (eq(item.info_type, "shutdown.stayoff")) {
        /* SnR0000
         * Shutdown after n minutes and stay off
         * Accepted values for n: .2 -> .9 , 01 -> 99 */

        long offdelay = strtol(infoOrEmpty(qx, "ups.delay.shutdown"), nullptr, 10);

        if (offdelay < 60) {
            snprintf(buf, sizeof(buf), ".%ld", offdelay / 6);
        } else {
            snprintf(buf, sizeof(buf), "%02ld", offdelay / 60);
        }

    } else if (eq(item.info_type, "test.battery.start")) {
        /* Accepted values for test time: .2 -> .9 (.2=12sec ..), 01 -> 99 (minutes)
         * -> you have to invoke test.battery.start + number of seconds [12..5940] */

        long delay;

        if (strlen(value) != strspn(value, "0123456789")) {
            // upslogx(LOG_ERR, "%s: non numerical value [%s]", item->info_type, value)
            return -1;
        }

        delay = (strlen(value) > 0) ? strtol(value, nullptr, 10) : 600;

        if ((delay < 12) || (delay > 5940)) {
            // upslogx(LOG_ERR, "%s: battery test time '%ld' out of range [12..5940] seconds", ...)
            return -1;
        }

        /* test time < 1 minute */
        if (delay < 60) {
            delay = delay / 6;
            snprintf(buf, sizeof(buf), ".%ld", delay);

        /* test time > 1 minute */
        } else {
            delay = delay / 60;
            snprintf(buf, sizeof(buf), "%02ld", delay);
        }

    } else if (eq(item.info_type, "beeper.toggle")) {
        const char* beeper_status = infoOrEmpty(qx, "ups.beeper.status");

        /* If the UPS is beeping then we can call BZOFF; if we previously set BZOFF we can call
         * BZON, provided that the beeper is not disabled */

        /* The UPS can disable/enable alarm (from UPS capability) */
        if (state(qx).alarm_control) {
            if (!strcmp(beeper_status, "enabled")) {
                snprintf(buf, sizeof(buf), "OFF");

            } else if (!strcmp(beeper_status, "muted")) {
                snprintf(buf, sizeof(buf), "ON");

            /* Beeper disabled */
            } else {
                // upslogx(LOG_INFO, "The beeper is already disabled")
                return -1;
            }

        /* The UPS can't disable/enable alarm (from UPS capability) */
        } else {
            if (!strcmp(beeper_status, "enabled")) {
                snprintf(buf, sizeof(buf), "OFF");
            } else if (!strcmp(beeper_status, "disabled")) {
                snprintf(buf, sizeof(buf), "ON");
            }
        }

    } else {
        /* Don't know what happened: unknown entry for pre-processing? */
        return -1;
    }

    // snprintf_dynamic(value, valuelen, item->command, "%s", buf): one %s in these commands
    snprintf(value, valuelen, item.command, buf);

    return 0;
}

/* UPS capabilities */
static int voltronicCapability(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    VoltronicState& s = state(qx);
    // Zero-filled: rawval+1 below is read even for an empty value
    char rawval[QX_SMALLBUF] = "";
    char *enabled, *disabled, *saveptr = nullptr;
    const char* val = nullptr;
    const char* itemValue = qx.value();

    snprintf(rawval, sizeof(rawval), "%s", itemValue);

    enabled = tokenize(rawval + 1, "D", &saveptr);
    disabled = tokenize(nullptr, "", &saveptr); // strtok_r(NULL, "\0", &saveptr)

    // "parsing failed"
    if (!enabled && !disabled) return -1;

    const char* en = enabled ? enabled : "";
    const char* dis = disabled ? disabled : "";

    /* NONUT items */
    if (eq(item.info_type, "bypass_alarm")) {
        if (strchr(en, 'p')) {
            val = s.bypass_alarm = "enabled";
        } else if (strchr(dis, 'p')) {
            val = s.bypass_alarm = "disabled";
        }

    } else if (eq(item.info_type, "battery_alarm")) {
        if (strchr(en, 'b')) {
            val = s.battery_alarm = "enabled";
        } else if (strchr(dis, 'b')) {
            val = s.battery_alarm = "disabled";
        }

    } else if (eq(item.info_type, "bypass_when_off")) {
        if (strchr(en, 'o')) {
            val = s.bypass_when_off = "enabled";
        } else if (strchr(dis, 'o')) {
            val = s.bypass_when_off = "disabled";
        }

    } else if (eq(item.info_type, "alarm_control")) {
        if (strchr(itemValue, 'a')) {
            if (strchr(en, 'a')) {
                const char* beeper = qx.getinfo("ups.beeper.status");

                val = s.alarm_control = "enabled";

                if (!beeper || strcmp(beeper, "muted")) qx.setinfo("ups.beeper.status", "enabled");

            } else if (strchr(dis, 'a')) {
                val = s.alarm_control = "disabled";
                qx.setinfo("ups.beeper.status", "disabled");
            }

            /* Unskip beeper.{enable,disable} instcmds */
            if (!unskip(qx, "beeper.enable", QX_FLAG_CMD)) return -1;
            if (!unskip(qx, "beeper.disable", QX_FLAG_CMD)) return -1;
        }

    } else if (eq(item.info_type, "converter_mode")) {
        if (strchr(en, 'v')) {
            val = s.converter_mode = "enabled";
        } else if (strchr(dis, 'v')) {
            val = s.converter_mode = "disabled";
        }

    } else if (eq(item.info_type, "eco_mode")) {
        if (strchr(itemValue, 'e')) {
            if (strchr(en, 'e')) {
                val = s.eco_mode = "enabled";
            } else if (strchr(dis, 'e')) {
                val = s.eco_mode = "disabled";
            }

            /* Unskip bypass.{start,stop} instcmds */
            if (!unskip(qx, "bypass.start", QX_FLAG_CMD)) return -1;
            if (!unskip(qx, "bypass.stop", QX_FLAG_CMD)) return -1;

            /* Unskip input.transfer.{high,low} */
            if (!unskip(qx, "input.transfer.high", QX_FLAG_SEMI_STATIC)) return -1;
            if (!unskip(qx, "input.transfer.low", QX_FLAG_SEMI_STATIC)) return -1;

            /* Unskip input.frequency.{high,low} */
            if (!unskip(qx, "input.frequency.high", QX_FLAG_SEMI_STATIC)) return -1;
            if (!unskip(qx, "input.frequency.low", QX_FLAG_SEMI_STATIC)) return -1;
        }

    } else if (eq(item.info_type, "battery_open_status_check")) {
        if (strchr(en, 'd')) {
            val = s.battery_open_status_check = "enabled";
        } else if (strchr(dis, 'd')) {
            val = s.battery_open_status_check = "disabled";
        }

    } else if (eq(item.info_type, "bypass_forbidding")) {
        if (strchr(en, 'f')) {
            val = s.bypass_forbidding = "enabled";
        } else if (strchr(dis, 'f')) {
            val = s.bypass_forbidding = "disabled";
        }

    } else if (eq(item.info_type, "site_fault_detection")) {
        if (strchr(en, 'l')) {
            val = s.site_fault_detection = "enabled";
        } else if (strchr(dis, 'l')) {
            val = s.site_fault_detection = "disabled";
        }

    } else if (eq(item.info_type, "advanced_eco_mode")) {
        if (strchr(en, 'n')) {
            val = s.advanced_eco_mode = "enabled";
        } else if (strchr(dis, 'n')) {
            val = s.advanced_eco_mode = "disabled";
        }

    } else if (eq(item.info_type, "constant_phase_angle")) {
        if (strchr(en, 'q')) {
            val = s.constant_phase_angle = "enabled";
        } else if (strchr(dis, 'q')) {
            val = s.constant_phase_angle = "disabled";
        }

    } else if (eq(item.info_type, "limited_runtime_on_battery")) {
        if (strchr(en, 'w')) {
            val = s.limited_runtime_on_battery = "enabled";
        } else if (strchr(dis, 'w')) {
            val = s.limited_runtime_on_battery = "disabled";
        }

    // 'h', 't', 'k', 'i', 'm', 'z': unknown/unused (commented out upstream, 2302-2331)

    /* Items with a NUT variable */
    } else if (eq(item.info_type, "ups.start.auto")) {
        if (strchr(en, 'r')) {
            val = "yes";
        } else if (strchr(dis, 'r')) {
            val = "no";
        }

    } else if (eq(item.info_type, "battery.protection")) {
        if (strchr(en, 's')) {
            val = "yes";
        } else if (strchr(dis, 's')) {
            val = "no";
        }

    } else if (eq(item.info_type, "battery.energysave")) {
        if (strchr(en, 'g')) {
            val = "yes";
        } else if (strchr(dis, 'g')) {
            val = "no";
        }

    } else if (eq(item.info_type, "ups.start.battery")) {
        if (strchr(en, 'c')) {
            val = "yes";
        } else if (strchr(dis, 'c')) {
            val = "no";
        }

    } else if (eq(item.info_type, "outlet.0.switchable")) {
        if (strchr(en, 'j')) {
            char buf[32]; // SMALLBUF upstream; "outlet.%d.switchable" for 1..4

            val = "yes";

            /* Unskip outlet.n.{switchable,status} */
            for (int i = 1; i <= 4; i++) {
                snprintf(buf, sizeof(buf), "outlet.%d.switchable", i);
                if (!unskip(qx, buf, 0)) return -1;

                snprintf(buf, sizeof(buf), "outlet.%d.status", i);
                if (!unskip(qx, buf, 0)) return -1;
            }

        } else if (strchr(dis, 'j')) {
            val = "no";
        }
    }

    /* UPS doesn't have that capability */
    if (!val) return -1;

    snprintf(value, valuelen, item.dfl, val);

    /* This item doesn't have a NUT var and we were not asked by the user to change its value */
    if ((qx.flags(qx.row(item)) & QX_FLAG_NONUT) && !getval(item.info_type)) return 0;

    /* Unskip setvar */
    if (!unskip(qx, item.info_type, QX_FLAG_SETVAR)) return -1;

    return 0;
}

/* *SETVAR* Set UPS capability options */
static int voltronicCapabilitySet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)qx;

    if (eq(value, "yes")) {
        snprintf(value, valuelen, item.command, "E");
        return 0;
    }

    if (eq(value, "no")) {
        snprintf(value, valuelen, item.command, "D");
        return 0;
    }

    /* At this point value should have been already checked against enum so this shouldn't happen.. however.. */
    // upslogx(LOG_ERR, "%s: given value [%s] is not acceptable. Enter either 'yes' or 'no'.", ...)

    return -1;
}

/* *SETVAR/NONUT* Change UPS capability according to user configuration in ups.conf */
static int voltronicCapabilitySetNonut(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const VoltronicState& s = state(qx);
    const char* match = nullptr;
    const struct {
        const char* type;  /* Name of the option */
        const char* match; /* Value reported by the UPS */
    } capability[] = {
        { "bypass_alarm", s.bypass_alarm },
        { "battery_alarm", s.battery_alarm },
        { "bypass_when_off", s.bypass_when_off },
        { "alarm_control", s.alarm_control },
        { "converter_mode", s.converter_mode },
        { "eco_mode", s.eco_mode },
        { "battery_open_status_check", s.battery_open_status_check },
        { "bypass_forbidding", s.bypass_forbidding },
        { "site_fault_detection", s.site_fault_detection },
        { "advanced_eco_mode", s.advanced_eco_mode },
        { "constant_phase_angle", s.constant_phase_angle },
        { "limited_runtime_on_battery", s.limited_runtime_on_battery },
        { nullptr, nullptr }
    };

    for (int i = 0; capability[i].type; i++) {
        if (!eq(item.info_type, capability[i].type)) continue;

        match = capability[i].match;

        break;
    }

    /* UPS doesn't have that capability */
    if (!match) return -1;

    /* At this point value should have been already checked by nutdrv_qx's own setvar so this
     * shouldn't happen.. however.. */
    if (eq(value, match)) {
        // upslogx(LOG_INFO, "%s is already %s", item->info_type, match)
        return -1;
    }

    if (eq(value, "disabled")) {
        snprintf(value, valuelen, item.command, "D");
    } else if (eq(value, "enabled")) {
        snprintf(value, valuelen, item.command, "E");
    } else {
        /* At this point value should have been already checked against enum so this shouldn't happen.. however.. */
        // upslogx(LOG_ERR, "%s: [%s] is not within acceptable values [enabled/disabled]", ...)
        return -1;
    }

    return 0;
}

/* *SETVAR/NONUT* Reset capability options and their limits to safe default values */
static int voltronicCapabilityReset(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    /* Nothing to do */
    if (!testvar("reset_to_default")) return -1;

    /* UPS capability options can be reset only when the UPS is in 'Standby Mode' (=OFF) (from QMOD) */
    if (!(qx.status() & QX_STATUS(QX_OFF))) {
        // upslogx(LOG_ERR, "%s: UPS capability options can be reset only when the UPS is in
        // Standby Mode (i.e. ups.status = 'OFF').", item->info_type)
        return -1;
    }

    snprintf(value, valuelen, "%s", item.command);

    return 0;
}

/* Voltage limits for ECO Mode */
static int voltronicEcoVolt(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    VoltronicState& s = state(qx);
    const long protocol = upsProtocol(qx);
    const char* itemValue = qx.value();
    int ovn;
    const char* outvoltnom;
    char buf[64]; // SMALLBUF upstream; "input.transfer.{high,low}.{min,max}"
    /* Range of accepted values for maximum voltage for ECO Mode */
    struct {
        int lower; /* Lower limit */
        int upper; /* Upper limit */
    } max;
    /* Range of accepted values for minimum voltage for ECO Mode */
    struct {
        int lower; /* Lower limit */
        int upper; /* Upper limit */
    } min;

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    snprintf(value, valuelen, item.dfl, strtod(itemValue, nullptr));

    outvoltnom = qx.getinfo("output.voltage.nominal");

    /* Since query for ratings (QRI) is not mandatory to run this driver, skip next steps if we
     * can't get the value of output voltage nominal */
    if (!outvoltnom) {
        /* We return 0 since we have the value and all's ok: simply we can't set its range so
         * we won't unskip SETVAR item and .{min,max} */
        return 0;
    }

    { /* scoping */
        long l = strtol(outvoltnom, nullptr, 10);
        if (l > INT_MAX || l < 0) {
            /* See comments above */
            return 0;
        }
        ovn = (int)l;
    }

    /* For P01/P09 */
    if (protocol == 1 || protocol == 9) {
        if (ovn >= 200) {
            min.lower = ovn - 24;
            min.upper = ovn - 7;
            max.lower = ovn + 7;
            max.upper = ovn + 24;
        } else {
            min.lower = ovn - 12;
            min.upper = ovn - 3;
            max.lower = ovn + 3;
            max.upper = ovn + 12;
        }

    /* For P02/P03/P10/P13/P14/P99 */
    } else if (protocol == 2 || protocol == 3 || protocol == 10 || protocol == 13 || protocol == 14 || protocol == 99) {
        if (ovn >= 200) {
            min.lower = ovn - 24;
            min.upper = ovn - 11;
            max.lower = ovn + 11;
            max.upper = ovn + 24;
        } else {
            min.lower = ovn - 12;
            min.upper = ovn - 5;
            max.lower = ovn + 5;
            max.upper = ovn + 12;
        }

    /* ECO mode not supported */
    } else {
        // "the UPS doesn't seem to support ECO Mode"
        /* We return 0 since we have the value and all's ok: simply we can't set its range so
         * we won't unskip SETVAR item and .{min,max} */
        return 0;
    }

    if (eq(item.info_type, "input.transfer.high")) {
        /* Fill voltronic_r_eco_volt_max */
        snprintf(s.eco_volt_max[0], sizeof(s.eco_volt_max[0]), "%d", max.lower);
        snprintf(s.eco_volt_max[1], sizeof(s.eco_volt_max[1]), "%d", max.upper);

    } else if (eq(item.info_type, "input.transfer.low")) {
        /* Fill voltronic_r_eco_volt_min */
        snprintf(s.eco_volt_min[0], sizeof(s.eco_volt_min[0]), "%d", min.lower);
        snprintf(s.eco_volt_min[1], sizeof(s.eco_volt_min[1]), "%d", min.upper);
    }

    /* Unskip input.transfer.{high,low}.{min,max} */
    snprintf(buf, sizeof(buf), "%s.min", item.info_type);
    if (!unskip(qx, buf, QX_FLAG_SEMI_STATIC)) return -1;

    snprintf(buf, sizeof(buf), "%s.max", item.info_type);
    if (!unskip(qx, buf, QX_FLAG_SEMI_STATIC)) return -1;

    /* Unskip input.transfer.{high,low} setvar */
    if (!unskip(qx, item.info_type, QX_FLAG_SETVAR)) return -1;

    return 0;
}

/* Voltage limits for ECO Mode (max, min) */
static int voltronicEcoVoltRange(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* buf;
    int i;

    if (eq(item.info_type, "input.transfer.low.min")) {
        buf = "input.transfer.low";
        i = 0;

    } else if (eq(item.info_type, "input.transfer.low.max")) {
        buf = "input.transfer.low";
        i = 1;

    } else if (eq(item.info_type, "input.transfer.high.min")) {
        buf = "input.transfer.high";
        i = 0;

    } else if (eq(item.info_type, "input.transfer.high.max")) {
        buf = "input.transfer.high";
        i = 1;

    } else {
        /* Don't know what happened */
        return -1;
    }

    int from = qx.findNutInfo(buf, QX_FLAG_SEMI_STATIC, 0);

    /* Don't know what happened */
    if (from < 0) return -1;

    const QxInfoRw* rw = qx.infoRw(qx.item(from));

    /* Value is set at runtime by voltronic_eco_volt, so if it's still unset something went wrong */
    if (!rw || !rw[i].value || !strlen(rw[i].value)) return -1;

    snprintf(value, valuelen, "%s", rw[i].value);

    return 0;
}

/* Frequency limits for ECO Mode */
static int voltronicEcoFreq(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    snprintf(value, valuelen, item.dfl, strtod(itemValue, nullptr));

    /* Unskip input.transfer.{high,low} setvar */
    if (!unskip(qx, item.info_type, QX_FLAG_SETVAR)) return -1;

    return 0;
}

/* *NONUT* Voltage/frequency limits for Bypass Mode */
static int voltronicBypass(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    VoltronicState& s = state(qx);
    const char* itemValue = qx.value();
    double val;

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    if (eq(item.info_type, "max_bypass_volt")) {
        val = s.max_bypass_volt = strtol(itemValue, nullptr, 10);

    } else if (eq(item.info_type, "min_bypass_volt")) {
        val = s.min_bypass_volt = strtol(itemValue, nullptr, 10);

    } else if (eq(item.info_type, "max_bypass_freq")) {
        val = s.max_bypass_freq = strtod(itemValue, nullptr);

    } else if (eq(item.info_type, "min_bypass_freq")) {
        val = s.min_bypass_freq = strtod(itemValue, nullptr);

    } else {
        /* Don't know what happened */
        return -1;
    }

    snprintf(value, valuelen, item.dfl, val);

    /* No user-provided value to change.. */
    if (!getval(item.info_type)) return 0;

    /* Unskip {min,max}_bypass_volt setvar */
    if (!unskip(qx, item.info_type, QX_FLAG_SETVAR)) return -1;

    return 0;
}

/* *NONUT* Number of batteries */
static int voltronicBattNumb(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    VoltronicState& s = state(qx);
    const char* itemValue = qx.value();

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    s.battery_number = strtol(itemValue, nullptr, 10);

    // "battery number out of range"
    if (s.battery_number > INT_MAX) return -1;

    snprintf(value, valuelen, item.dfl, (int)s.battery_number);

    /* No user-provided value to change.. */
    if (!getval(item.info_type)) return 0;

    /* Unskip battery_number setvar */
    if (!unskip(qx, "battery_number", QX_FLAG_SETVAR)) return -1;

    return 0;
}

/* Battery runtime */
static int voltronicBattRuntime(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    double runtime;

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    /* Battery runtime is reported by the UPS in minutes, NUT expects seconds */
    runtime = strtod(itemValue, nullptr) * 60;

    snprintf(value, valuelen, item.dfl, runtime);

    return 0;
}

/* Protocol used by the UPS */
static int voltronicProtocol(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)item;
    const char* itemValue = qx.value();
    long protocol;

    // strncasecmp(item->value, "PI", 2): "invalid start characters"
    if (!((itemValue[0] == 'P' || itemValue[0] == 'p') && (itemValue[1] == 'I' || itemValue[1] == 'i'))) return -1;

    /* Here we exclude non numerical value and other non accepted protocols (hence the restricted comparison target) */
    if (strspn(itemValue + 2, "01234789") != strlen(itemValue + 2)) {
        // upslogx(LOG_ERR, "Protocol [%s] is not supported by this driver", item->value)
        return -1;
    }

    protocol = strtol(itemValue + 2, nullptr, 10);

    switch (protocol) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 8:
    case 9:
    case 10:
    case 13:
    case 14:
    case 31:
    case 71:
    case 99:
        break;

    default:
        // upslogx(LOG_ERR, "Protocol [PI%02ld] is not supported by this driver", protocol)
        return -1;
    }

    snprintf(value, valuelen, "P%02ld", protocol);

    /* Unskip vars according to protocol */
    voltronicMassiveUnskip(qx, protocol);

    return 0;
}

/* Fault reported by the UPS:
 * When the UPS is queried for status (QGS), if it reports a fault (6th bit of 12bit flag of
 * the reply to QGS set to 1), the driver unskips the QFS item in qx2nut array: this function
 * processes the reply to QFS query */
static int voltronicFault(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    long protocol = upsProtocol(qx);
    const char* itemValue = qx.value();
    // LARGEBUF upstream; the longest text is "Unknown fault [%s]" of the 2 characters of the value
    char alarm[128];

    // upslogx(LOG_INFO, "Checking for faults..")

    if (eq(itemValue, "OK")) {
        snprintf(value, valuelen, item.dfl, "No fault found");
        // upslogx(LOG_INFO, "%s", value)
        qx.addFlags(qx.row(item), QX_FLAG_SKIP);
        return 0;
    }

    if ((strspn(itemValue, "0123456789ABC") != 2) ||
        ((itemValue[0] != '1') && (strspn(itemValue + 1, "0123456789") != 1))) {
        snprintf(alarm, sizeof(alarm), "Unknown fault [%s]", itemValue);

    /* P31 UPSes */
    } else if (protocol == 31) {
        if (strpbrk(itemValue + 1, "ABC")) {
            snprintf(alarm, sizeof(alarm), "Unknown fault [%s]", itemValue);

        } else {
            switch (strtol(itemValue, nullptr, 10)) {
            case 1:
                snprintf(alarm, sizeof(alarm), "%s", "Fan failure.");
                break;
            case 2:
                snprintf(alarm, sizeof(alarm), "%s", "Over temperature fault.");
                break;
            case 3:
                snprintf(alarm, sizeof(alarm), "%s", "Battery voltage is too high.");
                break;
            case 4:
                snprintf(alarm, sizeof(alarm), "%s", "Battery voltage too low.");
                break;
            case 5:
                snprintf(alarm, sizeof(alarm), "%s", "Inverter relay short-circuited.");
                break;
            case 6:
                snprintf(alarm, sizeof(alarm), "%s", "Inverter voltage over maximum value.");
                break;
            case 7:
                snprintf(alarm, sizeof(alarm), "%s", "Overload fault.");
                qx.updateStatus("OVER");
                break;
            case 8:
                snprintf(alarm, sizeof(alarm), "%s", "Bus voltage exceeds its upper limit.");
                break;
            case 9:
                snprintf(alarm, sizeof(alarm), "%s", "Bus soft start fail.");
                break;
            case 10:
                snprintf(alarm, sizeof(alarm), "%s", "Unknown fault [Fault code: 10]");
                break;
            case 51:
                snprintf(alarm, sizeof(alarm), "%s", "Over current fault.");
                break;
            case 52:
                snprintf(alarm, sizeof(alarm), "%s", "Bus voltage below its under limit.");
                break;
            case 53:
                snprintf(alarm, sizeof(alarm), "%s", "Inverter soft start fail.");
                break;
            case 54:
                snprintf(alarm, sizeof(alarm), "%s", "Self test fail.");
                break;
            case 55:
                snprintf(alarm, sizeof(alarm), "%s", "Output DC voltage exceeds its upper limit.");
                break;
            case 56:
                snprintf(alarm, sizeof(alarm), "%s", "Battery open fault.");
                break;
            case 57:
                snprintf(alarm, sizeof(alarm), "%s", "Current sensor fault.");
                break;
            case 58:
                snprintf(alarm, sizeof(alarm), "%s", "Battery short.");
                break;
            case 59:
                snprintf(alarm, sizeof(alarm), "%s", "Inverter voltage below its lower limit.");
                break;
            default:
                snprintf(alarm, sizeof(alarm), "Unknown fault [%s]", itemValue);
                break;
            }
        }

    /* All other UPSes */
    } else {
        switch (strtol(itemValue, nullptr, 10)) {
        case 1:
            switch (itemValue[1]) {
            case 'A':
                snprintf(alarm, sizeof(alarm), "%s", "L1 inverter negative power out of acceptable range.");
                break;
            case 'B':
                snprintf(alarm, sizeof(alarm), "%s", "L2 inverter negative power out of acceptable range.");
                break;
            case 'C':
                snprintf(alarm, sizeof(alarm), "%s", "L3 inverter negative power out of acceptable range.");
                break;
            default:
                snprintf(alarm, sizeof(alarm), "%s", "Bus voltage not within default setting.");
                break;
            }
            break;
        case 2:
            snprintf(alarm, sizeof(alarm), "%s", "Bus voltage over maximum value.");
            break;
        case 3:
            snprintf(alarm, sizeof(alarm), "%s", "Bus voltage below minimum value.");
            break;
        case 4:
            snprintf(alarm, sizeof(alarm), "%s", "Bus voltage differences out of acceptable range.");
            break;
        case 5:
            snprintf(alarm, sizeof(alarm), "%s", "Bus voltage of slope rate drops too fast.");
            break;
        case 6:
            snprintf(alarm, sizeof(alarm), "%s", "Over current in PFC input inductor.");
            break;
        case 11:
            snprintf(alarm, sizeof(alarm), "%s", "Inverter voltage not within default setting.");
            break;
        case 12:
            snprintf(alarm, sizeof(alarm), "%s", "Inverter voltage over maximum value.");
            break;
        case 13:
            snprintf(alarm, sizeof(alarm), "%s", "Inverter voltage below minimum value.");
            break;
        case 14:
            snprintf(alarm, sizeof(alarm), "%s", "Inverter short-circuited.");
            break;
        case 15:
            snprintf(alarm, sizeof(alarm), "%s", "L2 phase inverter short-circuited.");
            break;
        case 16:
            snprintf(alarm, sizeof(alarm), "%s", "L3 phase inverter short-circuited.");
            break;
        case 17:
            snprintf(alarm, sizeof(alarm), "%s", "L1L2 inverter short-circuited.");
            break;
        case 18:
            snprintf(alarm, sizeof(alarm), "%s", "L2L3 inverter short-circuited.");
            break;
        case 19:
            snprintf(alarm, sizeof(alarm), "%s", "L3L1 inverter short-circuited.");
            break;
        case 21:
            snprintf(alarm, sizeof(alarm), "%s", "Battery SCR short-circuited.");
            break;
        case 22:
            snprintf(alarm, sizeof(alarm), "%s", "Line SCR short-circuited.");
            break;
        case 23:
            snprintf(alarm, sizeof(alarm), "%s", "Inverter relay open fault.");
            break;
        case 24:
            snprintf(alarm, sizeof(alarm), "%s", "Inverter relay short-circuited.");
            break;
        case 25:
            snprintf(alarm, sizeof(alarm), "%s", "Input and output wires oppositely connected.");
            break;
        case 26:
            snprintf(alarm, sizeof(alarm), "%s", "Battery oppositely connected.");
            break;
        case 27:
            snprintf(alarm, sizeof(alarm), "%s", "Battery voltage is too high.");
            break;
        case 28:
            snprintf(alarm, sizeof(alarm), "%s", "Battery voltage too low.");
            break;
        case 29:
            snprintf(alarm, sizeof(alarm), "%s", "Failure for battery fuse being open-circuited.");
            break;
        case 31:
            snprintf(alarm, sizeof(alarm), "%s", "CAN-bus communication fault.");
            break;
        case 32:
            snprintf(alarm, sizeof(alarm), "%s", "Host signal circuit fault.");
            break;
        case 33:
            snprintf(alarm, sizeof(alarm), "%s", "Synchronous signal circuit fault.");
            break;
        case 34:
            snprintf(alarm, sizeof(alarm), "%s", "Synchronous pulse signal circuit fault.");
            break;
        case 35:
            snprintf(alarm, sizeof(alarm), "%s", "Parallel cable disconnected.");
            break;
        case 36:
            snprintf(alarm, sizeof(alarm), "%s", "Load unbalanced.");
            break;
        case 41:
            snprintf(alarm, sizeof(alarm), "%s", "Over temperature fault.");
            break;
        case 42:
            snprintf(alarm, sizeof(alarm), "%s", "Communication failure between CPUs in control board.");
            break;
        case 43:
            snprintf(alarm, sizeof(alarm), "%s", "Overload fault.");
            qx.updateStatus("OVER");
            break;
        case 44:
            snprintf(alarm, sizeof(alarm), "%s", "Fan failure.");
            break;
        case 45:
            snprintf(alarm, sizeof(alarm), "%s", "Charger failure.");
            break;
        case 46:
            snprintf(alarm, sizeof(alarm), "%s", "Model fault.");
            break;
        case 47:
            snprintf(alarm, sizeof(alarm), "%s", "MCU communication fault.");
            break;
        default:
            snprintf(alarm, sizeof(alarm), "Unknown fault [%s]", itemValue);
            break;
        }
    }

    snprintf(value, valuelen, item.dfl, alarm);
    // upslogx(LOG_INFO, "Fault found: %s", alarm)

    qx.addFlags(qx.row(item), QX_FLAG_SKIP);

    return 0;
}

// The text of the warning of bit i, the switch of voltronic_warning() (3280-3586) without
// its update_status() calls (bits 7, 8 and 55, made by the caller); nullptr for an unknown
// bit (the default case)
static const char* voltronicWarningText(int i) {
    switch (i) {
    case 0: return "Battery disconnected.";
    case 1: return "Neutral not connected.";
    case 2: return "Site fault.";
    case 3: return "Phase sequence incorrect.";
    case 4: return "Phase sequence incorrect in bypass.";
    case 5: return "Input frequency unstable in bypass.";
    case 6: return "Battery overcharged.";
    case 7: return "Low battery.";
    case 8: return "Overload alarm.";
    case 9: return "Fan alarm.";
    case 10: return "EPO enabled.";
    case 11: return "Unable to turn on UPS.";
    case 12: return "Over temperature alarm.";
    case 13: return "Charger alarm.";
    case 14: return "Remote auto shutdown.";
    case 15: return "L1 input fuse not working.";
    case 16: return "L2 input fuse not working.";
    case 17: return "L3 input fuse not working.";
    case 18: return "Positive PFC abnormal in L1.";
    case 19: return "Negative PFC abnormal in L1.";
    case 20: return "Positive PFC abnormal in L2.";
    case 21: return "Negative PFC abnormal in L2.";
    case 22: return "Positive PFC abnormal in L3.";
    case 23: return "Negative PFC abnormal in L3.";
    case 24: return "Abnormal in CAN-bus communication.";
    case 25: return "Abnormal in synchronous signal circuit.";
    case 26: return "Abnormal in synchronous pulse signal circuit.";
    case 27: return "Abnormal in host signal circuit.";
    case 28: return "Male connector of parallel cable not connected well.";
    case 29: return "Female connector of parallel cable not connected well.";
    case 30: return "Parallel cable not connected well.";
    case 31: return "Battery connection not consistent in parallel systems.";
    case 32: return "AC connection not consistent in parallel systems.";
    case 33: return "Bypass connection not consistent in parallel systems.";
    case 34: return "UPS model types not consistent in parallel systems.";
    case 35: return "Capacity of UPSes not consistent in parallel systems.";
    case 36: return "Auto restart setting not consistent in parallel systems.";
    case 37: return "Battery cell over charge.";
    case 38: return "Battery protection setting not consistent in parallel systems.";
    case 39: return "Battery detection setting not consistent in parallel systems.";
    case 40: return "Bypass not allowed setting not consistent in parallel systems.";
    case 41: return "Converter setting not consistent in parallel systems.";
    case 42: return "High loss point for frequency in bypass mode not consistent in parallel systems.";
    case 43: return "Low loss point for frequency in bypass mode not consistent in parallel systems.";
    case 44: return "High loss point for voltage in bypass mode not consistent in parallel systems.";
    case 45: return "Low loss point for voltage in bypass mode not consistent in parallel systems.";
    case 46: return "High loss point for frequency in AC mode not consistent in parallel systems.";
    case 47: return "Low loss point for frequency in AC mode not consistent in parallel systems.";
    case 48: return "High loss point for voltage in AC mode not consistent in parallel systems.";
    case 49: return "Low loss point for voltage in AC mode not consistent in parallel systems.";
    case 50: return "Warning for locking in bypass mode after 3 consecutive overloads within 30 min.";
    case 51: return "Warning for three-phase AC input current unbalance.";
    case 52: return "Warning for a three-phase input current unbalance detected in battery mode.";
    case 53: return "Warning for Inverter inter-current unbalance.";
    case 54: return "Programmable outlets cut off pre-alarm.";
    case 55: return "Warning for Battery replace.";
    case 56: return "Abnormal warning on input phase angle.";
    case 57: return "Warning!! Cover of maintain switch is open.";
    case 61: return "EEPROM operation error.";
    default: return nullptr; // "Unknown warning from UPS [bit: #%02d]"
    }
}

// unk+1 of voltronic_warning(): " #NN, #NN, ..." of the unknown bits set in bits
static void voltronicAppendUnknownWarnings(Cat& out, const char* bits) {
    bool first = true;
    for (int i = 0; i < (int)strlen(bits); i++) {
        if (bits[i] != '1' || voltronicWarningText(i)) continue;
        out.add(first ? " #%02d" : ", #%02d", i + 1);
        first = false;
    }
}

/* Warnings reported by the UPS */
// The text is the one of upstream, built with bounded buffers: upstream composes both the
// explicit (warns) and the cryptic (bitwarns) form in 4096-byte buffers and keeps one; here
// the explicit form is built in a buffer of ST_MAX_VALUE_LEN with its full length counted,
// the cryptic one directly into value (whose size, SMALLBUF, bounds both forms upstream too)
static int voltronicWarning(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)item;
    static const size_t ST_MAX_VALUE_LEN = 256; // include/extstate.h:35
    const char* itemValue = qx.value();
    char warnsBuf[ST_MAX_VALUE_LEN];
    Cat warns(warnsBuf, sizeof(warnsBuf), 0);
    bool known = false, unknown = false;
    warnsBuf[0] = '\0';

    // "invalid reply from the UPS"
    if (strspn(itemValue, "01") != strlen(itemValue)) return -1;

    /* No warnings */
    if (strspn(itemValue, "0") == strlen(itemValue)) return 0;

    snprintf(value, valuelen, "UPS warnings:");

    for (int i = 0; i < (int)strlen(itemValue); i++) {
        if (itemValue[i] != '1') continue;

        const char* warn = voltronicWarningText(i);

        if (i == 7) qx.updateStatus("LB");
        else if (i == 8) qx.updateStatus("OVER");
        else if (i == 55) qx.updateStatus("RB");

        // upslogx(LOG_INFO, "Warning from UPS: %s", warn)

        if (!warn) { /* Unknown warnings */
            unknown = true;
        } else { /* Known warnings */
            warns.add(known ? " %s" : "%s", warn);
            known = true;
        }
    }

    /* There's some known warning, at least */
    if (known) {
        /* We have both known and unknown warnings */
        if (unknown) {
            /* Appending unknown ones to known ones; removing leading comma from unk - 'explicit' */
            warns.add(" Unknown warnings [bit:");
            voltronicAppendUnknownWarnings(warns, itemValue);
            warns.add("]");
        }

    /* We have only unknown warnings */
    } else if (unknown) {
        /* Removing leading comma from unk */
        warns.add("Unknown warnings [bit:");
        voltronicAppendUnknownWarnings(warns, itemValue);
        warns.add("]");

    } else {
        /* Don't know what happened */
        return -1;
    }

    Cat out(value, valuelen, strlen(value));

    /* If grand total of warnings doesn't exceed value of alarm (=ST_MAX_VALUE_LEN) minus some
     * space (32) for other alarms.. */
    if ((ST_MAX_VALUE_LEN - 32) > warns.len) {
        /* ..then be explicit.. */
        out.add(" %s", warnsBuf);
    /* ..otherwise.. */
    } else {
        /* ..be cryptic */
        out.add(" ");
        if (known) {
            bool first = true;
            for (int i = 0; i < (int)strlen(itemValue); i++) {
                if (itemValue[i] != '1' || !voltronicWarningText(i)) continue;
                out.add(first ? "Known (see log or manual) [bit: #%02d" : ", #%02d", i + 1);
                first = false;
            }
            if (unknown) {
                out.add("]; Unknown warnings [bit:");
                voltronicAppendUnknownWarnings(out, itemValue);
                out.add("]");
            } else {
                out.add("]");
            }
        } else {
            out.add("Unknown warnings [bit:");
            voltronicAppendUnknownWarnings(out, itemValue);
            out.add("]");
        }
    }

    return 0;
}

/* Working mode reported by the UPS */
static int voltronicMode(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char *status = nullptr, *alarm = nullptr, *buzzword = nullptr;

    switch (qx.value()[0]) {
    case 'P':
        alarm = "UPS is going ON.";
        break;

    case 'S':
        status = "OFF";
        break;

    case 'Y':
        status = "BYPASS";
        break;

    case 'L':
        status = "OL";
        break;

    case 'B':
        status = "!OL";
        break;

    case 'T':
        status = "CAL";
        break;

    case 'F':
        alarm = "Fault reported by UPS.";
        break;

    case 'E':
        /* From man page: When input voltage/frequency are within acceptable
         * range, the UPS will bypass voltage to output for energy saving.
         * PFC and INVERTER are still active at this mode. */

        /* FIXME: Any char for advanced ECO mode? (PFC and INVERTER are off, the UPS will
         * bypass voltage to output for energy saving)*/
        buzzword = "vendor:voltronic:ECO-inverter-on";
        break;

    case 'C':
        /* From man page: When input frequency is within 40 Hz to 70 Hz,
         * the UPS can be set at a constant output frequency, 50 Hz or 60 Hz.
         * The UPS will still charge battery under this mode. */

        buzzword = "vendor:voltronic:freq-converter-on";
        break;

    case 'D':
        alarm = "UPS is shutting down!";
        status = "FSD";
        break;

    default:
        // "invalid reply from the UPS"
        return -1;
    }

    if (alarm && eq(item.info_type, "ups.alarm")) {
        snprintf(value, valuelen, item.dfl, alarm);
    } else if (status && eq(item.info_type, "ups.status")) {
        snprintf(value, valuelen, item.dfl, status);
    }

    if (buzzword) qx.buzzmodeSet(buzzword);

    return 0;
}

/* Process status bits */
static int voltronicStatus(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    const char* val = "";

    /* PI71: type field returns "--", status bits return "-"; treat as defaults */
    if (!strcmp(itemValue, "--") && item.from == 63) {
        snprintf(value, valuelen, "%s", "online");
        return 0;
    }
    if (!strcmp(itemValue, "-")) {
        /* single-dash status bit: treat as inactive (0) */
        snprintf(value, valuelen, "%s", "");
        return 0;
    }
    // "unexpected value"
    if (strspn(itemValue, "01") != strlen(itemValue)) return -1;

    switch (item.from) {
    case 63: /* UPS Type - ups.type */
    {
        long type = strtol(itemValue, nullptr, 10);

        if (!type) /* 00 -> Offline */
            val = "offline";
        else if (type == 1) /* 01 -> Line-interactive */
            val = "line-interactive";
        else if (type == 10) /* 10 -> Online */
            val = "online";
        else {
            // "invalid type"
            return -1;
        }
    }
        break;

    case 65: /* Utility Fail (Immediate) - ups.status */
        if (itemValue[0] == '1')
            val = "!OL";
        else
            val = "OL";
        break;

    case 66: /* Battery Low - ups.status */
        if (itemValue[0] == '1')
            val = "LB";
        else
            val = "!LB";
        break;

    case 67: /* Bypass/Boost or Buck Active - ups.{status,alarm} */
        if (itemValue[0] == '1') {
            double vi, vo;

            vi = strtod(infoOrEmpty(qx, "input.voltage"), nullptr);
            vo = strtod(infoOrEmpty(qx, "output.voltage"), nullptr);

            if (vo < 0.5 * vi) {
                // "output voltage too low"
                return -1;
            }

            if (vo < 0.95 * vi) {
                val = "TRIM";
            } else if (vo < 1.05 * vi) {
                long prot = upsProtocol(qx);

                if (!prot || prot == 8) { /* ups.alarm */
                    if (eq(item.info_type, "ups.alarm")) val = "UPS is in AVR Mode.";

                } else { /* ups.status */
                    if (eq(item.info_type, "ups.status")) val = "BYPASS";
                }

            } else if (vo < 1.5 * vi) {
                val = "BOOST";
            } else {
                // "output voltage too high"
                return -1;
            }
        }
        break;

    case 68: /* UPS Fault - ups.alarm */
        // As upstream (3872): the bit is compared with the integer 1, not with '1', so this
        // branch is never taken and the QFS row stays skipped
        if (itemValue[0] == 1) {
            for (size_t r = 0; r < qx.rowCount(); r++) {
                const QxItem& faultitem = qx.item((int)r);

                if (!faultitem.command) continue;

                if (eq(faultitem.command, "QFS\r")) {
                    qx.clearFlags((int)r, QX_FLAG_SKIP);
                    break;
                }
            }

            val = "UPS Fault!";
        }
        break;

    /*  case 69: */ /* unknown */
    /*      break;*/

    case 70: /* Test in Progress - ups.status */
        if (itemValue[0] == '1')
            val = "CAL";
        else
            val = "!CAL";
        break;

    case 71: /* Shutdown Active - ups.status */
        if (itemValue[0] == '1')
            val = "FSD";
        else
            val = "!FSD";
        break;

    case 72: /* Beeper status - ups.beeper.status */
        /* The UPS has the ability to enable/disable the alarm (from UPS capability) */
        if (state(qx).alarm_control) {
            const char* beeper = qx.getinfo("ups.beeper.status");

            if (!beeper || !eq(beeper, "disabled")) {
                if (itemValue[0] == '0') /* Beeper On */
                    val = "enabled";
                else
                    val = "muted";
            }

        /* The UPS lacks the ability to enable/disable the alarm (from UPS capability) */
        } else {
            if (itemValue[0] == '0') /* Beeper On */
                val = "enabled";
            else
                val = "disabled";
        }
        break;

    /*  case 73: */ /* unknown */
    /*      break;*/
    /*  case 74: */ /* unknown */
    /*      break;*/

    default:
        /* Don't know what happened */
        return -1;
    }

    snprintf(value, valuelen, "%s", val);

    return 0;
}

/* Output power factor */
static int voltronicOutputPowerfactor(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    double opf;

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    /* UPS report a value expressed in % so -> output.powerfactor*100 e.g. opf = 0,8 -> ups = 80 */
    opf = strtod(itemValue, nullptr) * 0.01;

    snprintf(value, valuelen, item.dfl, opf);

    return 0;
}

/* UPS serial number */
static int voltronicSerialNumb(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();

    /* If the UPS report a 00..0 serial we'll log it but we won't store it in device.serial */
    if (strspn(itemValue, "0") == strlen(itemValue)) {
        // upslogx(LOG_INFO, "%s: UPS reported a non-significant serial [%s]", item->info_type, item->value)
        return -1;
    }

    snprintf(value, valuelen, item.dfl, itemValue);

    return 0;
}

/* Outlet status */
static int voltronicOutlet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char *status, *switchable;
    char number = item.info_type[7];
    char buf[32]; // SMALLBUF upstream; "outlet.%c.delay.shutdown"

    switch (qx.value()[0]) {
    case '1':
        switchable = "yes";
        status = "on";
        break;

    case '0':
        switchable = "yes";
        status = "off";
        break;

    default:
        // "invalid reply from the UPS"
        return -1;
    }

    if (strstr(item.info_type, "switchable")) {
        snprintf(value, valuelen, item.dfl, switchable);
    } else if (strstr(item.info_type, "status")) {
        snprintf(value, valuelen, item.dfl, status);
    } else {
        /* Don't know what happened */
        return -1;
    }

    /* Unskip outlet.n.delay.shutdown */
    snprintf(buf, sizeof(buf), "outlet.%c.delay.shutdown", number);
    if (!unskip(qx, buf, QX_FLAG_SEMI_STATIC)) return -1;

    /* Unskip outlet.n.load.on */
    snprintf(buf, sizeof(buf), "outlet.%c.load.on", number);
    if (!unskip(qx, buf, QX_FLAG_CMD)) return -1;

    /* Unskip outlet.n.load.off */
    snprintf(buf, sizeof(buf), "outlet.%c.load.off", number);
    if (!unskip(qx, buf, QX_FLAG_CMD)) return -1;

    return 0;
}

/* Outlet delay time */
static int voltronicOutletDelay(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    char number = item.info_type[7];
    char buf[32]; // SMALLBUF upstream; "outlet.%c.delay.shutdown"
    double val;

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    /* UPS reports minutes, NUT expects seconds */
    val = strtod(itemValue, nullptr) * 60;

    snprintf(value, valuelen, item.dfl, val);

    /* Unskip outlet.n.delay.shutdown setvar */
    snprintf(buf, sizeof(buf), "outlet.%c.delay.shutdown", number);
    if (!unskip(qx, buf, QX_FLAG_SETVAR)) return -1;

    return 0;
}

/* *SETVAR* Outlet delay time */
static int voltronicOutletDelaySet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)qx;
    long delay = strtol(value, nullptr, 10);

    // "invalid delay %ld sec set for UPS"
    if ((delay / 60) > INT_MAX) return -1;

    /* From seconds to minute */
    delay = delay / 60;

    snprintf(value, valuelen, item.command, (int)delay);

    return 0;
}

/* Type of battery */
static int voltronicP31b(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    long val;

    // "invalid battery type reported by the UPS"
    if ((itemValue[0] != '0') || (strspn(itemValue + 1, "012") != 1)) return -1;

    val = strtol(itemValue, nullptr, 10);

    // "invalid battery type reported by the UPS" (val > SIZE_MAX upstream too)
    if (val < 0) return -1;

    snprintf(value, valuelen, item.dfl, qx.infoRw(item)[(size_t)val].value);

    return 0;
}

/* *SETVAR* Type of battery */
static int voltronicP31bSet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const QxInfoRw* rw = qx.infoRw(item);
    int i;

    if (!rw) return -1;

    for (i = 0; strlen(rw[i].value) > 0; i++) {
        if (eq(rw[i].value, value)) break;
    }

    /* At this point value should already be checked against enum so this shouldn't happen.. however.. */
    if (!strlen(rw[i].value)) {
        // upslogx(LOG_ERR, "%s: value [%s] out of range", item->info_type, value)
        return -1;
    }

    snprintf(value, valuelen, "%d", i);

    return voltronicProcessSetvar(qx, item, value, valuelen);
}

/* *NONUT* Actual device grid working range type for P31 UPSes */
static int voltronicP31g(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    long val;

    // "invalid device grid working range reported by the UPS"
    if ((itemValue[0] != '0') || (strspn(itemValue + 1, "01") != 1)) return -1;

    val = strtol(itemValue, nullptr, 10);

    // "invalid device grid working range reported by the UPS" (val > SIZE_MAX upstream too)
    if (val < 0) return -1;

    snprintf(value, valuelen, item.dfl, qx.infoRw(item)[(size_t)val].value);

    state(qx).work_range_type = val;

    return 0;
}

/* *SETVAR/NONUT* Device grid working range type for P31 UPSes */
static int voltronicP31gSet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const QxInfoRw* rw = qx.infoRw(item);
    int i;

    if (!rw) return -1;

    for (i = 0; strlen(rw[i].value) > 0; i++) {
        if (eq(rw[i].value, value)) break;
    }

    /* At this point value should have been already checked against enum so this shouldn't happen.. however.. */
    if (!strlen(rw[i].value)) {
        // upslogx(LOG_ERR, "%s: value [%s] out of range", item->info_type, value)
        return -1;
    }

    if (i == state(qx).work_range_type) {
        // upslogx(LOG_INFO, "%s is already set to %s", item->info_type, item->info_rw[i].value)
        return -1;
    }

    snprintf(value, valuelen, "%d", i);

    return voltronicProcessSetvar(qx, item, value, valuelen);
}

/* *NONUT* UPS actual input/output phase angles */
static int voltronicPhase(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    long angle;

    // "non numerical value"
    if (strspn(itemValue, "0123456789 .") != strlen(itemValue)) return -1;

    angle = strtol(itemValue, nullptr, 10);

    if (eq(item.info_type, "output_phase_angle")) {
        state(qx).output_phase_angle = angle;

        /* User-provided value to change.. */
        if (getval(item.info_type)) {
            /* Unskip output_phase_angle setvar */
            if (!unskip(qx, item.info_type, QX_FLAG_SETVAR)) return -1;
        }
    }

    // "phase angle out of range"
    if (angle < 0 || angle > INT_MAX) return -1;

    snprintf(value, valuelen, item.dfl, (int)angle);

    return 0;
}

/* *SETVAR/NONUT* Output phase angle */
static int voltronicPhaseSet(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const QxInfoRw* rw = qx.infoRw(item);
    int i;

    if (!rw) return -1;

    for (i = 0; strlen(rw[i].value) > 0; i++) {
        if (eq(rw[i].value, value)) break;
    }

    /* At this point value should have been already checked against enum so this shouldn't happen.. however.. */
    if (!strlen(rw[i].value)) {
        // upslogx(LOG_ERR, "%s: value [%s] out of range", item->info_type, value)
        return -1;
    }

    if (strtol(rw[i].value, nullptr, 10) == state(qx).output_phase_angle) {
        // upslogx(LOG_INFO, "%s is already set to %s", item->info_type, item->info_rw[i].value)
        return -1;
    }

    snprintf(value, valuelen, "%d", i);

    return voltronicProcessSetvar(qx, item, value, valuelen);
}

/* *NONUT* UPS is master/slave in a system of UPSes in parallel */
static int voltronicParallel(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)item;
    const char* itemValue = qx.value();
    const char* type;

    // "non numerical value"
    if (strlen(itemValue) != strspn(itemValue, "0123456789")) return -1;

    /* 001 for master UPS, 002 and 003 for slave UPSes */
    switch (strtol(itemValue, nullptr, 10)) {
    case 1:
        type = "master";
        break;

    case 2:
    case 3:
        type = "slave";
        break;

    default:
        // "invalid reply from the UPS"
        return -1;
    }

    snprintf(value, valuelen, "This UPS is *%s* in a system of UPSes in parallel", type);

    return 0;
}


/* == Subdriver interface == */
const QxSubdriver QX_VOLTRONIC = {
    "Voltronic 0.15",
    voltronicClaim,
    VOLTRONIC_QX2NUT,
    nullptr,
    nullptr,
    "ACK",
    "(NAK\r",
    voltronicInfoRw,
};
