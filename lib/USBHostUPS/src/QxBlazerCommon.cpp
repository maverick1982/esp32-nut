#include "QxBlazerCommon.h"
#include "QxEngine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Port of nut_repo/drivers/nutdrv_qx_blazer-common.c, in the order of that file.
// testvar() -> qx.options(), dstate_getinfo()/dstate_delinfo() -> qx.getinfo()/qx.delinfo(),
// item->value -> qx.value(), item->qxflags |= QX_FLAG_SKIP -> qx.addFlags(row, ...).
// The upslogx()/upsdebugx() messages are kept as comments: QxDriver (US-136) logs the outcomes
// (protocol, communications lost and re-established) and the host logs every command.

namespace {

// dstate_getinfo() of a missing variable is NULL upstream and strtol/strtod(NULL) would
// crash there; here it reads as an empty string (0)
const char* infoOrEmpty(QxEngine& qx, const char* name) {
    const char* v = qx.getinfo(name);
    return v ? v : "";
}

// Conversions of a printf format, for the validate_formatting_string() checks of
// test.battery.start: 0 = no conversion ("%%" excluded), 1 = exactly one integer conversion
// (flags and width allowed, as "%02d"), -1 = anything else
int integerConversion(const char* fmt) {
    int count = 0;
    bool integer = true;
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') continue;
        p++;
        if (*p == '%') continue;
        if (!*p) return -1;
        while (*p && strchr("-+ #0", *p)) p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p != 'd' && *p != 'i') integer = false;
        if (!*p) return -1;
        count++;
    }
    if (count == 0) return 0;
    return (count == 1 && integer) ? 1 : -1;
}

} // namespace

/* == Ranges == */

// Range for ups.delay.start
const QxInfoRw BLAZER_R_ONDELAY[] = {
    { "0", nullptr },
    { "599940", nullptr },
    { "", nullptr }
};

// Range for ups.delay.shutdown
const QxInfoRw BLAZER_R_OFFDELAY[] = {
    { "12", nullptr },
    { "600", nullptr },
    { "", nullptr }
};

/* == Support functions == */

// To tell whether the UPS is supported or not, we'll check both status (Q1/QS/D) and
// vendor (I/FW?) - provided that we were not told not to do it with the ups.conf flag
// 'novendor'.
bool blazerClaim(QxEngine& qx) {
    int item = qx.findNutInfo("input.voltage", 0, 0);

    // Don't know what happened
    if (item < 0) return false;

    // No reply/Unable to get value
    if (qx.process(item)) return false;

    // Unable to process value
    if (qx.infovalSet(item) != 1) return false;

    if (qx.options().novendor) return true;

    // Vendor
    item = qx.findNutInfo("ups.firmware", 0, 0);

    // Don't know what happened
    if (item < 0) {
        qx.delinfo("input.voltage");
        return false;
    }

    // No reply/Unable to get value
    if (qx.process(item)) {
        qx.delinfo("input.voltage");
        return false;
    }

    // Unable to process value
    if (qx.infovalSet(item) != 1) {
        qx.delinfo("input.voltage");
        return false;
    }

    return true;
}

// NOTE: this 'light' version only checks for status (Q1/QS/D/..)
bool blazerClaimLight(QxEngine& qx) {
    int item = qx.findNutInfo("input.voltage", 0, 0);

    // Don't know what happened
    if (item < 0) return false;

    // No reply/Unable to get value
    if (qx.process(item)) return false;

    // Unable to process value
    if (qx.infovalSet(item) != 1) return false;

    return true;
}

// blazer_makevartable(), blazer_makevartable_light(): not ported, the flags come from
// QxOptions

// Subdriver-specific initups
void blazerInitups(QxEngine& qx) {
    const bool nr = qx.options().norating;
    const bool nv = qx.options().novendor;
    const bool isb = qx.options().ignoresab;

    if (!nr && !nv && !isb) return;

    for (size_t r = 0; r < qx.rowCount(); r++) {
        const int row = (int)r;
        const QxItem& item = qx.item(row);

        if (!item.command) continue;

        // norating
        if (nr && QxEngine::equalsIgnoreCase(item.command, "F\r")) qx.addFlags(row, QX_FLAG_SKIP);

        // novendor
        if (nv && (QxEngine::equalsIgnoreCase(item.command, "I\r") || QxEngine::equalsIgnoreCase(item.command, "FW?\r"))) {
            qx.addFlags(row, QX_FLAG_SKIP);
        }

        // ignoresab ('Shutdown Active' bit)
        if (isb && QxEngine::equalsIgnoreCase(item.info_type, "ups.status") && item.from == 44 && item.to == 44) {
            qx.addFlags(row, QX_FLAG_SKIP);
        }
    }
}

// NOTE: this 'light' version only checks for status (Q1/QS/D/..) related items
void blazerInitupsLight(QxEngine& qx) {
    if (!qx.options().ignoresab) return;

    for (size_t r = 0; r < qx.rowCount(); r++) {
        const int row = (int)r;
        const QxItem& item = qx.item(row);

        if (!QxEngine::equalsIgnoreCase(item.info_type, "ups.status") || item.from != 44 || item.to != 44) continue;

        qx.addFlags(row, QX_FLAG_SKIP); // 'Shutdown Active' bit
        break;
    }
}

/* == Preprocess functions == */

// Preprocess setvars
int blazerProcessSetvar(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)qx;

    if (!strlen(value)) return -1; // value not given

    if (QxEngine::equalsIgnoreCase(item.info_type, "ups.delay.start")) {
        long ondelay = strtol(value, nullptr, 10);

        if (ondelay < 0) return -1; // "ondelay '%ld' should not be negative"

        // Truncate to minute
        ondelay -= (ondelay % 60);
        snprintf(value, valuelen, "%ld", ondelay);

    } else if (QxEngine::equalsIgnoreCase(item.info_type, "ups.delay.shutdown")) {
        long offdelay = strtol(value, nullptr, 10);

        if (offdelay < 0) return -1; // "offdelay '%ld' should not be negative"

        // Truncate to nearest settable value
        if (offdelay < 60) {
            offdelay -= (offdelay % 6);
        } else {
            offdelay -= (offdelay % 60);
        }

        snprintf(value, valuelen, "%ld", offdelay);

    } else {
        // Don't know what happened: unknown entry for pre-processing?
        return -1;
    }

    return 0;
}

// Preprocess instant commands
int blazerProcessCommand(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    if (QxEngine::equalsIgnoreCase(item.info_type, "shutdown.return")) {
        /* Sn: Shutdown after n minutes and then turn on when mains is back
         * SnRm: Shutdown after n minutes and then turn on after m minutes
         * Accepted values for n: .2 -> .9 , 01 -> 10
         * Accepted values for m: 0001 -> 9999
         * Note: "S01R0001" and "S01R0002" may not work on early (GE) firmware versions.
         * The failure mode is that the UPS turns off and never returns.
         * The fix is to push the return value up by 2, i.e. S01R0003, and it will return online properly.
         * (thus the default of ondelay=3 mins) */

        long offdelay = strtol(infoOrEmpty(qx, "ups.delay.shutdown"), nullptr, 10),
             ondelay = strtol(infoOrEmpty(qx, "ups.delay.start"), nullptr, 10) / 60;
        char buf[64] = ""; // SMALLBUF upstream; the longest value is ".%ldR%04ld"

        if (ondelay <= 0) {
            if (offdelay < 0) return -1; // "offdelay '%ld' should not be negative"

            if (offdelay < 60) {
                snprintf(buf, sizeof(buf), ".%ld", offdelay / 6);
            } else {
                snprintf(buf, sizeof(buf), "%02ld", offdelay / 60);
            }

        } else if (offdelay < 60) {
            if (offdelay < 0) return -1;

            snprintf(buf, sizeof(buf), ".%ldR%04ld", offdelay / 6, ondelay);

        } else {
            snprintf(buf, sizeof(buf), "%02ldR%04ld", offdelay / 60, ondelay);
        }

        // snprintf_dynamic(value, valuelen, item->command, "%s", buf): the command of a
        // static const row with one %s
        snprintf(value, valuelen, item.command, buf);

    } else if (QxEngine::equalsIgnoreCase(item.info_type, "shutdown.stayoff")) {
        /* SnR0000
         * Shutdown after n minutes and stay off
         * Accepted values for n: .2 -> .9 , 01 -> 10 */

        long offdelay = strtol(infoOrEmpty(qx, "ups.delay.shutdown"), nullptr, 10);
        char buf[64] = ""; // SMALLBUF upstream; the longest value is ".%ldR%04ld"

        if (offdelay < 0) return -1; // "offdelay '%ld' should not be negative"

        if (offdelay < 60) {
            snprintf(buf, sizeof(buf), ".%ld", offdelay / 6);
        } else {
            snprintf(buf, sizeof(buf), "%02ld", offdelay / 60);
        }

        snprintf(value, valuelen, item.command, buf);

    } else if (QxEngine::equalsIgnoreCase(item.info_type, "test.battery.start")) {
        long delay = strlen(value) > 0 ? strtol(value, nullptr, 10) : 600;

        // "battery test time '%ld' out of range [60..5940] seconds"
        if ((delay < 60) || (delay > 5940)) return -1;

        delay = delay / 60;

        // In various mapping tables, "%02d" is prevalent; actual value is range-checked
        // above to fit into a typical int
        const int conv = integerConversion(item.command);
        if (conv == 1) {
            // The most likely case
            snprintf(value, valuelen, item.command, (int)delay);
        } else if (conv == 0) {
            // A few mappings seem to just request the test without parameters
            snprintf(value, valuelen, "%s", item.command);
        } else {
            // Finally try the actual long int
            snprintf(value, valuelen, item.command, delay);
        }

    } else {
        // Don't know what happened: unknown entry for pre-processing?
        return -1;
    }

    return 0;
}

// Process status bits
int blazerProcessStatusBits(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* itemValue = qx.value();
    const char* val = "";

    // "unexpected value"
    if (strspn(itemValue, "01") != 1) return -1;

    switch (item.from) {
    case 38: // Utility Fail (Immediate)
        val = itemValue[0] == '1' ? "!OL" : "OL";
        break;

    case 39: // Battery Low
        val = itemValue[0] == '1' ? "LB" : "!LB";
        break;

    case 40: // Bypass/Boost or Buck Active
        if (itemValue[0] == '1') {
            double vi = strtod(infoOrEmpty(qx, "input.voltage"), nullptr);
            double vo = strtod(infoOrEmpty(qx, "output.voltage"), nullptr);

            if (vo < 0.5 * vi) {
                return -1; // "output voltage too low"
            } else if (vo < 0.95 * vi) {
                val = "TRIM";
            } else if (vo < 1.05 * vi) {
                val = "BYPASS";
            } else if (vo < 1.5 * vi) {
                val = "BOOST";
            } else {
                return -1; // "output voltage too high"
            }
        }
        break;

    case 41: // UPS Failed - ups.alarm
        if (itemValue[0] == '1') val = "UPS selftest failed!";
        break;

    case 42: // UPS Type - ups.type
        val = itemValue[0] == '1' ? "offline / line interactive" : "online";
        break;

    case 43: // Test in Progress
        val = itemValue[0] == '1' ? "CAL" : "!CAL";
        break;

    case 44: // Shutdown Active
        val = itemValue[0] == '1' ? "FSD" : "!FSD";
        break;

    case 45: // Beeper status - ups.beeper.status
        val = itemValue[0] == '1' ? "enabled" : "disabled";
        break;

    default:
        // Don't know what happened
        return -1;
    }

    snprintf(value, valuelen, "%s", val);

    return 0;
}
