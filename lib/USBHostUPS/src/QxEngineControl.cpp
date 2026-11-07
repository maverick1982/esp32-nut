#include "QxEngine.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Port of instcmd(), setvar() and the command list of nut_repo/drivers/nutdrv_qx.c
// (DRIVER_VERSION 0.54); line numbers refer to that file. Pure logic like the rest of the
// engine: the upslogx()/upsdebugx() messages are left to the host (US-136), which logs
// every command and SET VAR with its result.

namespace {
// Copy of one info_rw value for its preprocess: SMALLBUF upstream, but the values are short
// numbers or enum words and the copies live on the stack of the poll task
const size_t kInfoRwBuf = 64;
} // namespace

// --- instcmd() -------------------------------------------------------------------------

QxEngine::Instcmd QxEngine::instcmd(UPSData& data, const char* cmdname, const char* extradata) {
    DataScope scope(*this, &data);
    return doInstcmd(cmdname, extradata);
}

// nutdrv_qx.c:2802-2945. The aliases beeper.on/beeper.off are not ported: they are not in
// the UpsCommands.h catalog, so they never reach the driver (as with HidControl).
QxEngine::Instcmd QxEngine::doInstcmd(const char* cmdname, const char* extradata) {
    if (!cmdname) return Instcmd::Invalid;
    // upsd passes NULL for a command without value; the Web UI and the NUT server pass ""
    if (extradata && !*extradata) extradata = nullptr;

    // Retrieve item by command name
    int row = findNutInfo(cmdname, QX_FLAG_CMD, QX_FLAG_SKIP);

    // Check for fallback if not found
    if (row < 0) {
        if (equalsIgnoreCase(cmdname, "load.on")) return doInstcmd("load.on.delay", "0");

        if (equalsIgnoreCase(cmdname, "load.off")) return doInstcmd("load.off.delay", "0");

        if (equalsIgnoreCase(cmdname, "shutdown.return")) {
            // Ensure "ups.start.auto" is set to "yes", if supported
            if (getinfo("ups.start.auto") && doSetvar("ups.start.auto", "yes") != Setvar::Handled) {
                return Instcmd::Failed;
            }
            Instcmd ret = doInstcmd("load.on.delay", getinfo("ups.delay.start"));
            if (ret != Instcmd::Handled) return ret;
            return doInstcmd("load.off.delay", getinfo("ups.delay.shutdown"));
        }

        if (equalsIgnoreCase(cmdname, "shutdown.stayoff")) {
            // Ensure "ups.start.auto" is set to "no", if supported
            if (getinfo("ups.start.auto") && doSetvar("ups.start.auto", "no") != Setvar::Handled) {
                return Instcmd::Failed;
            }
            Instcmd ret = doInstcmd("load.on.delay", "-1");
            if (ret != Instcmd::Handled) return ret;
            return doInstcmd("load.off.delay", getinfo("ups.delay.shutdown"));
        }

        // "command %s unavailable"
        return Instcmd::Invalid;
    }

    const QxItem& it = item(row);

    // If extradata is empty, use the default value from the QX to NUT table, if any. Copied
    // first: extradata may point into the data (ups.delay.*), the copy outlives any change
    extradata = extradata ? extradata : it.dfl;
    snprintf(_ctlValue, sizeof(_ctlValue), "%s", extradata ? extradata : "");

    // Preprocess command. Upstream returns STAT_INSTCMD_CONVERSION_FAILED only with errno
    // EINVAL, which no ported preprocess sets: Failed (ConversionFailed stays for later ports)
    if (it.preprocess && it.preprocess(*this, it, _ctlValue, sizeof(_ctlValue))) return Instcmd::Failed;

    // No preprocess function -> nothing to do with extradata
    if (!it.preprocess) _ctlValue[0] = '\0';

    // Send the command, get the reply. A reply equal to the protocol's rejected string fails
    // in processAnswer()
    if (process(row, strlen(_ctlValue) > 0 ? _ctlValue : nullptr)) {
        clearWork();
        return Instcmd::Failed;
    }

    // We got a reply from the UPS: either subdriver->accepted (-> command handled) or the
    // command itself echoed back (-> command failed). No reply -> command handled
    bool handled = strlen(_value) == 0 || (_sub->accepted && equalsIgnoreCase(_value, _sub->accepted));
    clearWork();
    if (!handled) return Instcmd::Failed;

    // Set the status so that SEMI_STATIC vars are polled
    _dataChanged = true;
    return Instcmd::Handled;
}

// --- setvar() --------------------------------------------------------------------------

QxEngine::Setvar QxEngine::setvar(UPSData& data, const char* varname, const char* val) {
    DataScope scope(*this, &data);
    return doSetvar(varname, val);
}

// nutdrv_qx.c:2948-3256. The results stay those of upstream (ADR 0003), also STAT_SET_UNKNOWN
// for a value out of range ("TODO: HANDLED but FAILED, not UNKNOWN!"): in NUT upsd refuses it
// first with INVALID-VALUE, from the range published in LIST RW (US-136 for Q*).
QxEngine::Setvar QxEngine::doSetvar(const char* varname, const char* val) {
    if (!varname) return Setvar::Unknown;

    // Retrieve variable
    int row = findNutInfo(varname, QX_FLAG_SETVAR, QX_FLAG_SKIP);
    if (row < 0) return Setvar::Unknown; // "element %s unavailable"

    const QxItem& it = item(row);

    // A one-time setvar from ups.conf: upstream the INIT walk runs it and marks the row
    // QX_FLAG_SKIP, so find_nut_info() never finds it afterwards. No ported protocol has one.
    if (_flags[row] & QX_FLAG_NONUT) return Setvar::Unknown;

    // "value not given for %s"
    if (!val || !*val) return Setvar::Unknown;

    snprintf(_ctlValue, sizeof(_ctlValue), "%s", val);

    // Nothing to do (dstate_getinfo() of a missing variable would be NULL upstream)
    const char* current = getinfo(it.info_type);
    if (current && equalsIgnoreCase(current, _ctlValue)) return Setvar::Handled;

    if (_flags[row] & QX_FLAG_RANGE) {
        // Check if given value is in the range of accepted values (range)
        if (strspn(_ctlValue, "0123456789 .") != strlen(_ctlValue)) return Setvar::Unknown; // non numerical value
        long valuetoset = strtol(_ctlValue, nullptr, 10);
        if (!rangeContains(it, valuetoset)) return Setvar::Unknown; // value out of range
    } else if (_flags[row] & QX_FLAG_ENUM) {
        // Check if given value is in the range of accepted values (enum)
        if (!enumContains(it, _ctlValue)) return Setvar::Unknown; // value out of range
    } else if (it.info_flags & QX_ST_FLAG_STRING) {
        // Check if given value is not too long (string): the aux qx_set_var() sets from
        // info_rw[0]; none means "unable to find tree node"
        if (!it.info_rw || !it.info_rw[0].value) return Setvar::Unknown;
        long aux = strtol(it.info_rw[0].value, nullptr, 10);
        if (aux < 0) return Setvar::Unknown;
        if (aux < (long)strlen(_ctlValue)) return Setvar::Unknown; // value is too long
    }

    // Preprocess value: from NUT-compliant to UPS-compliant (errno EINVAL: see doInstcmd())
    if (it.preprocess && it.preprocess(*this, it, _ctlValue, sizeof(_ctlValue))) return Setvar::Unknown;

    // Handle server side variable
    if (_flags[row] & QX_FLAG_ABSENT) {
        setinfo(it.info_type, _ctlValue);
        return Setvar::Handled;
    }

    // No preprocess function -> nothing to do with val
    if (!it.preprocess) _ctlValue[0] = '\0';

    // Actual variable setting
    if (process(row, strlen(_ctlValue) > 0 ? _ctlValue : nullptr)) {
        clearWork();
        return Setvar::Unknown;
    }

    // Reply: subdriver->accepted (-> handled) or the command echoed back (-> failed, reported
    // as STAT_SET_UNKNOWN upstream). No reply -> handled
    bool handled = strlen(_value) == 0 || (_sub->accepted && equalsIgnoreCase(_value, _sub->accepted));
    clearWork();
    if (!handled) return Setvar::Unknown;

    // Set the status so that SEMI_STATIC vars are polled
    _dataChanged = true;
    return Setvar::Handled;
}

// The ranges of qx_set_var() (nutdrv_qx.c, "Set range"): consecutive pairs (from, to) of the
// info_rw values, skipping a value whose preprocess fails and a pair beyond INT_MAX
// ("settable range exceeds INT_MAX"). The preprocess runs on a copy: the tables are const.
bool QxEngine::rangeContains(const QxItem& item, long value) {
    const char* from = nullptr;
    char fromBuf[kInfoRwBuf];
    char buf[kInfoRwBuf];
    for (const QxInfoRw* rv = item.info_rw; rv && rv->value && strlen(rv->value) > 0; rv++) {
        snprintf(buf, sizeof(buf), "%s", rv->value);
        if (rv->preprocess && rv->preprocess(buf, sizeof(buf))) continue;

        if (!from) {
            snprintf(fromBuf, sizeof(fromBuf), "%s", buf);
            from = fromBuf;
            continue;
        }

        long lFrom = strtol(from, nullptr, 10);
        long lTo = strtol(buf, nullptr, 10);
        from = nullptr;
        if (lFrom > INT_MAX || lTo > INT_MAX) continue;
        // valuetoset is in the range
        if (lFrom <= value && value <= lTo) return true;
    }
    return false;
}

// The enum values of qx_set_var() (nutdrv_qx.c, "Set enum list"), compared as setvar() does
bool QxEngine::enumContains(const QxItem& item, const char* value) {
    char buf[kInfoRwBuf];
    for (const QxInfoRw* ev = item.info_rw; ev && ev->value && strlen(ev->value) > 0; ev++) {
        snprintf(buf, sizeof(buf), "%s", ev->value);
        if (ev->preprocess && ev->preprocess(buf, sizeof(buf))) continue;
        if (equalsIgnoreCase(buf, value)) return true;
    }
    return false;
}

// --- Command list ----------------------------------------------------------------------

bool QxEngine::commandOffered(const char* cmdname) const {
    if (!cmdname) return false;
    // dstate_addcmd() of the INIT walk: every QX_FLAG_CMD row it reaches (nutdrv_qx.c:4604-4608)
    if (findNutInfo(cmdname, QX_FLAG_CMD, QX_FLAG_SKIP) >= 0) return true;
    // upsdrv_initinfo(), nutdrv_qx.c:3644-3656: defaults with a delay value of '0' (= immediate)
    if (equalsIgnoreCase(cmdname, "load.off")) return findNutInfo("load.off.delay", QX_FLAG_CMD, QX_FLAG_SKIP) >= 0;
    if (equalsIgnoreCase(cmdname, "load.on")) return findNutInfo("load.on.delay", QX_FLAG_CMD, QX_FLAG_SKIP) >= 0;
    return false;
}
