#include "QxProtocols.h"
#include "QxBlazerCommon.h"
#include "QxEngine.h"

// Row by row copy of nut_repo/drivers/nutdrv_qx_q1.c:30-123 (Q1_VERSION "Q1 0.08"), the
// fallback protocol. Columns of item_t without the runtime answer and value (QxTypes.h):
// info_type, info_flags, info_rw, command, answer_len, leading, from, to, dfl, qxflags,
// preprocess_command, preprocess_answer, preprocess. DEFAULT_ONDELAY "180" and
// DEFAULT_OFFDELAY "30" of nutdrv_qx.h. The testing table (q1_testing) lives in
// test/test_qx_protocols.

/* qx2nut lookup table */
static const QxItem Q1_QX2NUT[] = {

    /*
     * > [Q1\r]
     * < [(226.0 195.0 226.0 014 49.0 27.5 30.0 00001000\r]
     *    01234567890123456789012345678901234567890123456
     *    0         1         2         3         4
     */

    { "input.voltage",          0, nullptr, "Q1\r", 47, '(', 1,  5,  "%.1f", 0, nullptr, nullptr, nullptr },
    { "input.voltage.fault",    0, nullptr, "Q1\r", 47, '(', 7,  11, "%.1f", 0, nullptr, nullptr, nullptr },
    { "output.voltage",         0, nullptr, "Q1\r", 47, '(', 13, 17, "%.1f", 0, nullptr, nullptr, nullptr },
    { "ups.load",               0, nullptr, "Q1\r", 47, '(', 19, 21, "%.0f", 0, nullptr, nullptr, nullptr },
    { "input.frequency",        0, nullptr, "Q1\r", 47, '(', 23, 26, "%.1f", 0, nullptr, nullptr, nullptr },
    { "battery.voltage",        0, nullptr, "Q1\r", 47, '(', 28, 31, "%.2f", 0, nullptr, nullptr, QxEngine::qxMultiplyBattvolt },
    { "ups.temperature",        0, nullptr, "Q1\r", 47, '(', 33, 36, "%.1f", 0, nullptr, nullptr, nullptr },
    /* Status bits */
    { "ups.status",             0, nullptr, "Q1\r", 47, '(', 38, 38, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, blazerProcessStatusBits }, /* Utility Fail (Immediate) */
    { "ups.status",             0, nullptr, "Q1\r", 47, '(', 39, 39, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, blazerProcessStatusBits }, /* Battery Low */
    { "ups.status",             0, nullptr, "Q1\r", 47, '(', 40, 40, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, blazerProcessStatusBits }, /* Bypass/Boost or Buck Active */
    { "ups.alarm",              0, nullptr, "Q1\r", 47, '(', 41, 41, nullptr, 0,                  nullptr, nullptr, blazerProcessStatusBits }, /* UPS Failed */
    { "ups.type",               0, nullptr, "Q1\r", 47, '(', 42, 42, "%s",    QX_FLAG_STATIC,     nullptr, nullptr, blazerProcessStatusBits }, /* UPS Type */
    { "ups.status",             0, nullptr, "Q1\r", 47, '(', 43, 43, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, blazerProcessStatusBits }, /* Test in Progress */
    { "ups.status",             0, nullptr, "Q1\r", 47, '(', 44, 44, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, blazerProcessStatusBits }, /* Shutdown Active */
    { "ups.beeper.status",      0, nullptr, "Q1\r", 47, '(', 45, 45, "%s",    0,                  nullptr, nullptr, blazerProcessStatusBits }, /* Beeper status */

    /* Instant commands */
    { "beeper.toggle",            0, nullptr, "Q\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "load.off",                 0, nullptr, "S00R0000\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "load.on",                  0, nullptr, "C\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "shutdown.return",          0, nullptr, "S%s\r",      0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, blazerProcessCommand },
    { "shutdown.stayoff",         0, nullptr, "S%sR0000\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, blazerProcessCommand },
    { "shutdown.stop",            0, nullptr, "C\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "test.battery.start",       0, nullptr, "T%02d\r",    0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, blazerProcessCommand },
    { "test.battery.start.deep",  0, nullptr, "TL\r",       0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "test.battery.start.quick", 0, nullptr, "T\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "test.battery.stop",        0, nullptr, "CT\r",       0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },

    /* Server-side settable vars */
    { "ups.delay.start",    QX_ST_FLAG_RW, BLAZER_R_ONDELAY,  nullptr, 0, 0, 0, 0, "180", QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, blazerProcessSetvar },
    { "ups.delay.shutdown", QX_ST_FLAG_RW, BLAZER_R_OFFDELAY, nullptr, 0, 0, 0, 0, "30",  QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, blazerProcessSetvar },

    /* End of structure. */
    { nullptr, 0, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr }
};

/* Subdriver-specific initups */
static void q1Initups(QxEngine& qx) {
    blazerInitupsLight(qx);
}

/* Subdriver interface */
const QxSubdriver QX_Q1 = {
    "Q1 0.08",
    blazerClaimLight,
    Q1_QX2NUT,
    q1Initups,
    nullptr,
    "ACK",
    nullptr,
};
