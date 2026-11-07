#include "QxEngine.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Port of nut_repo/drivers/nutdrv_qx.c (DRIVER_VERSION 0.54); line numbers refer to that file.

namespace {

// d_equal() of include/nut_float.h:57
bool dEqual(double x, double y) { return fabs(x - y) <= DBL_EPSILON; }

// str_trim_m(value, "# ") of common/str.c: strips the characters of set at both ends
void trimChars(char* s, const char* set) {
    size_t len = strlen(s);
    while (len > 0 && strchr(set, s[len - 1])) s[--len] = '\0';
    size_t start = strspn(s, set);
    if (start > 0) memmove(s, s + start, len - start + 1);
}

// status_info[] of nutdrv_qx.c:191-206
struct StatusLookup {
    const char* status_str;
    QxStatusBit bit;
};
const StatusLookup STATUS_INFO[] = {
    { "OL", QX_OL },
    { "LB", QX_LB },
    { "RB", QX_RB },
    { "CHRG", QX_CHRG },
    { "DISCHRG", QX_DISCHRG },
    { "BYPASS", QX_BYPASS },
    { "CAL", QX_CALIB },
    { "OFF", QX_OFF },
    { "OVER", QX_OVER },
    { "TRIM", QX_TRIM },
    { "BOOST", QX_BOOST },
    { "FSD", QX_FSD },
};

// ups_status_set() (nutdrv_qx.c:4797-4838) becomes the ups.status.* flags that
// UPSData::computeUPSStatusString() reads, so ups.status keeps one computation (ADR 0013).
// bypass and fsd are Q*-only keys: the HID bypassauto/bypassman add bypass mode alarms and
// shutdown_imminent gives LB, neither of which nutdrv_qx does.
struct StatusKey {
    QxStatusBit bit;
    const char* key;
};
const StatusKey STATUS_KEYS[] = {
    { QX_OL, "ups.status.ac_present" },     // OL, otherwise OB
    { QX_LB, "ups.status.battery_low" },
    { QX_RB, "ups.status.replace_battery" }, // + "Replace battery!" (ups_alarm_set)
    { QX_CHRG, "ups.status.charging" },      // qualified by battery.charge there; no ported protocol sets it
    { QX_DISCHRG, "ups.status.discharging" },
    { QX_BYPASS, "ups.status.bypass" },
    { QX_CALIB, "ups.status.cal" },
    { QX_OFF, "ups.status.off" },
    { QX_OVER, "ups.status.overload" },
    { QX_TRIM, "ups.status.trim" },
    { QX_BOOST, "ups.status.boost" },
    { QX_FSD, "ups.status.fsd" },            // + "Shutdown imminent!" (ups_alarm_set)
};

} // namespace

bool QxEngine::equalsIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return a == b;
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ca != cb) return false;
    }
    return *a == *b;
}

QxEngine::QxEngine(const QxSubdriver& sub, IQxTransport& transport, const QxOptions& options)
    : _sub(&sub), _transport(transport), _options(options) {
    reset(sub, options);
}

void QxEngine::reset(const QxSubdriver& sub, const QxOptions& options) {
    _sub = &sub;
    _options = options;
    _rows = 0;
    for (const QxItem* it = sub.qx2nut; it && it->info_type; it++) _rows++;
    // clear() keeps the capacity: no allocation when the new table is not longer
    _flags.clear();
    _flags.reserve(_rows);
    for (size_t i = 0; i < _rows; i++) _flags.push_back(sub.qx2nut[i].qxflags);
    _data = nullptr;
    memset(_cmd, 0, sizeof(_cmd));
    memset(_rx, 0, sizeof(_rx));
    memset(_answer, 0, sizeof(_answer));
    memset(_value, 0, sizeof(_value));
    memset(_infoval, 0, sizeof(_infoval));
    memset(_prevCommand, 0, sizeof(_prevCommand));
    memset(_prevAnswer, 0, sizeof(_prevAnswer));
    memset(_ctlValue, 0, sizeof(_ctlValue));
    _status = 0;
    _alarms = "";
    _dataChanged = false;
    _lastPollMs = 0;
    _retry = 0;
    _reestablished = false;
    _batt = Batt();
    _battVoltageReportsOnePackConsidered = false;
}

// --- dstate access ---------------------------------------------------------------------

const char* QxEngine::getinfo(const char* name) const {
    if (!_data) return nullptr;
    const String* v = _data->find(name);
    return v ? v->c_str() : nullptr;
}

void QxEngine::setinfo(const char* name, const char* value) {
    if (!_data) return;
    String v(value);
    if (!_data->valueEquals(name, v)) _data->set(name, v); // write only what changes (ADR 0007)
}

void QxEngine::delinfo(const char* name) {
    if (_data && _data->find(name)) _data->remove(name);
}

// --- Exported functions ----------------------------------------------------------------

// find_nut_info(), nutdrv_qx.c:4840-4867
int QxEngine::findNutInfo(const char* varname, uint32_t flag, uint32_t noflag) const {
    for (size_t row = 0; row < _rows; row++) {
        if (!equalsIgnoreCase(_sub->qx2nut[row].info_type, varname)) continue;
        if (flag && ((_flags[row] & flag) != flag)) continue;
        if (noflag && (_flags[row] & noflag)) continue;
        return (int)row;
    }
    return -1;
}

// update_status(), nutdrv_qx.c:4290-4318
void QxEngine::updateStatus(const char* value) {
    bool clear = false;
    if (*value == '!') {
        value++;
        clear = true;
    }
    for (const auto& s : STATUS_INFO) {
        if (!equalsIgnoreCase(s.status_str, value)) continue;
        if (clear) _status &= ~QX_STATUS(s.bit);
        else _status |= QX_STATUS(s.bit);
        return;
    }
    // Unknown value: ignored, as upstream (debug message only)
}

// alarm_set() of dstate.c: texts separated by a space
void QxEngine::alarmSet(const char* text) {
    if (_alarms.length() > 0) _alarms += " ";
    _alarms += text;
}

// qx_process_answer(), nutdrv_qx.c:4869-4927 (errno is only used for debug messages there)
int QxEngine::processAnswer(int row, size_t len) {
    const QxItem& it = item(row);

    // Query rejected by the UPS
    if (_sub->rejected && equalsIgnoreCase(_answer, _sub->rejected)) return -1;

    // Short reply
    if (it.answer_len && len < it.answer_len) return -1;

    // Wrong leading character
    if (it.leading && _answer[0] != it.leading) return -1;

    // Check boundaries
    if (it.to && it.to < it.from) return -1;

    // Get value
    if (strlen(_answer)) {
        int n = it.to ? 1 + it.to - it.from : (int)strcspn(_answer, "\r") - it.from;
        snprintf(_value, sizeof(_value), "%.*s", n, _answer + it.from);
    } else {
        _value[0] = '\0';
    }
    return 0;
}

// qx_process(), nutdrv_qx.c:4930-4996
int QxEngine::process(int row, const char* command) {
    const QxItem& it = item(row);
    const char* src = command ? command : it.command;
    // Upstream allocates a larger buffer for commands of SMALLBUF bytes or more: no ported
    // protocol has one, so they fail here instead
    if (!src || strlen(src) >= sizeof(_cmd)) return -1;

    memset(_cmd, 0, sizeof(_cmd));
    int cmdLen = snprintf(_cmd, sizeof(_cmd), "%s", src);

    // Preprocess the command
    if (it.preprocess_command && (cmdLen = it.preprocess_command(*this, it, _cmd, sizeof(_cmd))) == -1) {
        return -1;
    }

    // Send the command
    memset(_rx, 0, sizeof(_rx));
    int len = _transport.command(_cmd, (size_t)cmdLen, _rx, sizeof(_rx));

    memset(_answer, 0, sizeof(_answer));
    if (len < 0) return -1;
    memcpy(_answer, _rx, sizeof(_rx)); // _answer[QX_SMALLBUF - 1] stays '\0'

    // Preprocess the answer
    if (it.preprocess_answer) {
        len = it.preprocess_answer(*this, it, len);
        if (len < 0) {
            // Clear the failed answer, so that the next rows with the same command do not reuse it
            memset(_answer, 0, sizeof(_answer));
            return -1;
        }
    }

    // Process the answer to get the value
    return processAnswer(row, (size_t)len);
}

// ups_infoval_set(), nutdrv_qx.c:4999-5071
int QxEngine::infovalSet(int row) {
    const QxItem& it = item(row);
    char* value = _infoval;
    value[0] = '\0';

    if (it.preprocess) {
        // Process the value returned by the UPS to NUT standards
        if (it.preprocess(*this, it, value, sizeof(_infoval))) return -1;

        // Deal with status items
        if (!strncmp(it.info_type, "ups.status", 10)) {
            if (strlen(value) > 0) updateStatus(value);
            return 0;
        }

        // Deal with alarm items
        if (!strncmp(it.info_type, "ups.alarm", 9)) {
            if (strlen(value) > 0) alarmSet(value);
            return 0;
        }
    } else {
        snprintf(value, sizeof(_infoval), "%s", _value);

        // Either left/right filled with hashes, spaces or a mix of both
        if (_flags[row] & QX_FLAG_TRIM) trimChars(value, "# ");

        // A row without preprocess always has a dfl upstream; a missing one counts as "%s"
        if (it.dfl && !equalsIgnoreCase(it.dfl, "%s")) {
            if (strspn(value, "0123456789 .") != strlen(value)) return -1; // non numerical value
            double d = strtod(value, nullptr);
            // dfl is a single floating point conversion of a static const table (checked by
            // test_qx_protocols), as snprintf_dynamic() validates upstream
            snprintf(value, sizeof(_infoval), it.dfl, d);
        }
    }

    if (_flags[row] & QX_FLAG_NONUT) return 1;

    // Non significant value
    if (!strlen(value)) return -1;

    _flags[row] |= QX_FLAG_MAPPING_HANDLED;
    setinfo(it.info_type, value);

    // Fill batt.{chrg,runt}.act for guesstimation
    if (equalsIgnoreCase(it.info_type, "battery.charge")) _batt.chrgAct = strtol(value, nullptr, 10);
    else if (equalsIgnoreCase(it.info_type, "battery.runtime")) _batt.runtAct = strtol(value, nullptr, 10);

    return 1;
}

// --- Walk ------------------------------------------------------------------------------

void QxEngine::clearWork() {
    memset(_answer, 0, sizeof(_answer));
    memset(_value, 0, sizeof(_value));
}

// qx_ups_walk(), nutdrv_qx.c:4489-4783
bool QxEngine::walk(WalkMode mode) {
    // Clear batt.{chrg,runt}.act for guesstimation
    if (mode == WalkMode::FULL_UPDATE) {
        _batt.runtAct = -1;
        _batt.chrgAct = -1;
        _battVoltageReportsOnePackConsidered = false;
    }

    // Clear data from previous_item
    memset(_prevCommand, 0, sizeof(_prevCommand));
    memset(_prevAnswer, 0, sizeof(_prevAnswer));

    for (size_t r = 0; r < _rows; r++) {
        const int row = (int)r;
        const QxItem& it = item(row);

        if (_flags[row] & QX_FLAG_SKIP) continue;

        // Filter data according to mode
        switch (mode) {
        case WalkMode::INIT:
            // Special case for handling server side variables
            if (_flags[row] & QX_FLAG_ABSENT) {
                // Already set
                if (getinfo(it.info_type)) continue;
                setinfo(it.info_type, it.dfl ? it.dfl : "");
                // qx_set_var() (flags, range, enum): setvar() reads them from info_rw
                // (rangeContains, enumContains); publishing them in LIST RW is US-136
                continue;
            }
            // Allow duplicates for these NUT variables
            if (!strncmp(it.info_type, "ups.alarm", 9) || !strncmp(it.info_type, "ups.status", 10)) break;
            // This one doesn't exist yet
            if (getinfo(it.info_type) == nullptr) break;
            continue;

        case WalkMode::QUICK_UPDATE:
            // Quick update only deals with status and alarms!
            if (!(_flags[row] & QX_FLAG_QUICK_POLL)) continue;
            break;

        case WalkMode::FULL_UPDATE:
            // These don't need polling after initinfo()
            if (_flags[row] & (QX_FLAG_ABSENT | QX_FLAG_CMD | QX_FLAG_SETVAR | QX_FLAG_STATIC)) continue;
            // These need to be polled after user changes (setvar / instcmd)
            if ((_flags[row] & QX_FLAG_SEMI_STATIC) && !_dataChanged) continue;
            break;
        }

        // Instant commands: dstate_addcmd() upstream, here commandOffered() reads the flags
        if (_flags[row] & QX_FLAG_CMD) continue;

        // Setvars: the QX_FLAG_NONUT branch (setvar(NULL) + skip) is not needed by the
        // ported protocols
        if (_flags[row] & QX_FLAG_SETVAR) continue;

        int retcode;
        // Check whether the previous item uses the same command and then use its answer
        if (strlen(_prevCommand) > 0 && strlen(_prevAnswer) > 0 && it.command &&
            equalsIgnoreCase(_prevCommand, it.command)) {
            snprintf(_answer, sizeof(_answer), "%s", _prevAnswer);
            retcode = processAnswer(row, strlen(_answer));
        } else {
            // ..otherwise: execute command to get answer from the UPS
            retcode = process(row);
        }

        // Record item as previous_item
        snprintf(_prevCommand, sizeof(_prevCommand), "%s", it.command ? it.command : "");
        snprintf(_prevAnswer, sizeof(_prevAnswer), "%s", _answer);

        if (retcode) {
            clearWork();
            if (_flags[row] & QX_FLAG_QUICK_POLL) return false;
            // Skip this item from now on
            if (mode == WalkMode::INIT) _flags[row] |= QX_FLAG_SKIP;
            // Don't know what happened, try again later...
            continue;
        }

        // Process the value we got back (set status bits and set the value of other parameters)
        retcode = infovalSet(row);
        clearWork();

        if (retcode == -1) {
            if (_flags[row] & QX_FLAG_QUICK_POLL) return false;
            continue;
        }

        // qx_set_var() for retcode == 1 in INIT (flags, range, enum): no ported protocol has a
        // writable row read from the UPS; setvar() checks info_rw itself
    }

    // Update battery guesstimation (the runtimecal branch is not ported)
    if (mode == WalkMode::FULL_UPDATE && (dEqual(_batt.runtAct, -1) || dEqual(_batt.chrgAct, -1))) {
        battery();
    }

    return true;
}

// --- Phases ----------------------------------------------------------------------------

bool QxEngine::claim(UPSData& data) {
    DataScope scope(*this, &data);
    return _sub->claim ? _sub->claim(*this) : false;
}

void QxEngine::initUps() {
    if (_sub->initups) _sub->initups(*this);
}

// upsdrv_initinfo(), nutdrv_qx.c:3604-3680. Not ported: the ups.conf values ondelay,
// offdelay and pollfreq (the NUT defaults stay), analyze_mapping_usage() (debug),
// driver.parameter.pollfreq and the handlers. The load.off/load.on commands of 3644-3656
// are derived by commandOffered().
bool QxEngine::initInfo(UPSData& data, uint32_t nowMs) {
    DataScope scope(*this, &data);

    setinfo("driver.version.data", _sub->name);

    // Initialise data
    if (!walk(WalkMode::INIT)) return false;

    // Init battery guesstimation
    initBattery();

    _lastPollMs = nowMs;

    // Subdriver initinfo
    if (_sub->initinfo) _sub->initinfo(*this);

    return true;
}

QxEngine::Update QxEngine::walkFailed() {
    // "Communications with the UPS lost: status read failed!"
    if (_retry < QX_MAXTRIES || _retry == QX_MAXTRIES) {
        _retry++;
        return Update::Failed;
    }
    return Update::Stale; // dstate_datastale()
}

// upsdrv_updateinfo(), nutdrv_qx.c:3525-3601
QxEngine::Update QxEngine::updateInfo(UPSData& data, uint32_t nowMs) {
    DataScope scope(*this, &data);
    _reestablished = false;

    // Do a full update (polling) every pollfreq or upon data change (i.e. setvar/instcmd).
    // Upstream compares whole seconds (now > lastpoll + pollfreq); here milliseconds, safe
    // across the millis() wrap.
    bool full = (uint32_t)(nowMs - _lastPollMs) > DEFAULT_POLLFREQ_S * 1000UL || _dataChanged;

    if (full) {
        // Clear ups_status and the alarms (alarm_init())
        _status = 0;
        _alarms = "";
        if (!walk(WalkMode::FULL_UPDATE)) return walkFailed();
        _lastPollMs = nowMs;
        _dataChanged = false;
    } else {
        // Quick poll data only to see if the UPS is still connected
        if (!walk(WalkMode::QUICK_UPDATE)) return walkFailed();
    }

    // ups_status_set() + status_commit()
    commitStatus();

    // ups_alarm_set() + alarm_commit(), only after a full update as upstream: the RB/FSD
    // texts come from the flags just written, after the texts of the protocol rows
    if (full) data.applyAlarmVariables(_alarms);

    // "Communications with the UPS re-established"
    if (_retry > QX_MAXTRIES) _reestablished = true;
    _retry = 0;

    return Update::Ok;
}

void QxEngine::commitStatus() {
    for (const auto& s : STATUS_KEYS) setinfo(s.key, (_status & QX_STATUS(s.bit)) ? "1" : "0");
}

// --- Battery guesstimation (nutdrv_qx.c:210-300, 444-680) ------------------------------

int QxEngine::qxMultiplyBattvolt(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)item;
    float s = 0;

    // Adjusted here or not, this method was called at all and other code should not
    // multiply again!
    qx._battVoltageReportsOnePackConsidered = true;
    if (!qx._battVoltageReportsOnePack || qx._batt.packs < 2) {
        // This device already reports the sum-total voltage for the battery assembly
        snprintf(value, valuelen, "%s", qx.value());
        return 0;
    }

    if (sscanf(qx.value(), "%f", &s) != 1) return -1;
    snprintf(value, valuelen, "%.2f", s * qx._batt.packs);
    return 0;
}

int QxEngine::qxMultiplyX1000(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)item;
    float s = 0;
    if (sscanf(qx.value(), "%f", &s) != 1) return -1;
    snprintf(value, valuelen, "%.2f", s * 1000.0);
    return 0;
}

int QxEngine::qxMultiplyM2s(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)item;
    float s = 0;
    if (sscanf(qx.value(), "%f", &s) != 1) return -1;
    snprintf(value, valuelen, "%.0f", s * 60.0);
    return 0;
}

// qx_battery(), nutdrv_qx.c:446-477: fill batt.volt.act and guesstimate the battery charge
// if it isn't already available
int QxEngine::battery() {
    const char* val = getinfo("battery.voltage");
    if (!val) return -1;

    _batt.voltAct = strtod(val, nullptr);
    if (!_battVoltageReportsOnePackConsidered) _batt.voltAct *= _batt.packs;

    if (dEqual(_batt.chrgAct, -1) && _batt.voltLow > 0 && _batt.voltHigh > _batt.voltLow) {
        _batt.chrgAct = 100 * (_batt.voltAct - _batt.voltLow) / (_batt.voltHigh - _batt.voltLow);
        if (_batt.chrgAct < 0) _batt.chrgAct = 0;
        if (_batt.chrgAct > 100) _batt.chrgAct = 100;

        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", _batt.chrgAct);
        setinfo("battery.charge", buf);
    }
    return 0;
}

// qx_initbattery(), nutdrv_qx.c:501-680. Not ported: the ups.conf options
// battery_voltage_reports_one_pack and runtimecal (without runtimecal upstream returns
// before the runtime estimate) and the LOG_INFO lines.
void QxEngine::initBattery() {
    const char* val;
    bool battPacksKnown = false;
    char buf[24];

    if ((val = getinfo("battery.voltage.high"))) _batt.voltHigh = strtod(val, nullptr);
    if ((val = getinfo("battery.voltage.low"))) _batt.voltLow = strtod(val, nullptr);
    if ((val = getinfo("battery.voltage.nominal"))) _batt.voltNom = strtod(val, nullptr);

    // No battery.voltage.{low,high} but a nominal battery voltage: guesstimate them
    // ("No values for battery high/low voltages", "Using 'guesstimation'")
    if (!dEqual(_batt.voltNom, -1) && (dEqual(_batt.voltLow, -1) || dEqual(_batt.voltHigh, -1))) {
        _batt.voltLow = 104 * _batt.voltNom / 120;
        _batt.voltHigh = 130 * _batt.voltNom / 120;
        snprintf(buf, sizeof(buf), "%.2f", _batt.voltLow);
        setinfo("battery.voltage.low", buf);
        snprintf(buf, sizeof(buf), "%.2f", _batt.voltHigh);
        setinfo("battery.voltage.high", buf);
    }

    val = getinfo("battery.packs");
    if (val && strspn(val, "0123456789 .") == strlen(val)) {
        _batt.packs = strtod(val, nullptr);
        battPacksKnown = true;
    }

    // Guesstimation: init values if not provided by device/overrides
    if (!getinfo("battery.charge") || !getinfo("battery.runtime")) {
        if (!battPacksKnown) {
            if (!battery() && !dEqual(_batt.voltNom, -1)) {
                static const double packs[] = { 120, 100, 80, 60, 48, 36, 30, 24, 18, 12, 8, 6, 4, 3, 2, 1, 0.5, -1 };
                // The battery voltage will quickly return to at least the nominal value
                // after discharging them. For overlapping battery.voltage.low/high ranges
                // therefore choose the one with the highest multiplier.
                for (int i = 0; packs[i] > 0; i++) {
                    if (packs[i] * _batt.voltAct > 1.25 * _batt.voltNom) continue;
                    if (packs[i] * _batt.voltAct < 0.8 * _batt.voltNom) break; // "Can't autodetect"
                    _batt.packs = packs[i]; // "Autodetected %.0f as number of battery packs"
                    break;
                }
            }
        }

        // Update batt.{chrg,volt}.act
        battery();
        // runtimecal not set: "Battery runtime will not be calculated"
    }
}
