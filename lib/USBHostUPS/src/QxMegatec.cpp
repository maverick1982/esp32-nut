#include "QxProtocols.h"
#include "QxBlazerCommon.h"
#include "QxEngine.h"

// Row by row copy of nut_repo/drivers/nutdrv_qx_megatec.c:28-173 (MEGATEC_VERSION
// "Megatec 0.10"). Columns of item_t without the runtime answer and value (QxTypes.h):
// info_type, info_flags, info_rw, command, answer_len, leading, from, to, dfl, qxflags,
// preprocess_command, preprocess_answer, preprocess. DEFAULT_ONDELAY "180" and
// DEFAULT_OFFDELAY "30" of nutdrv_qx.h. The testing table (megatec_testing) lives in
// test/test_qx_protocols.

/* qx2nut lookup table */
static const QxItem MEGATEC_QX2NUT[] = {

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

    /*
     * > [F\r]
     * < [#220.0 000 024.0 50.0\r]
     *    0123456789012345678901
     *    0         1         2
     */

    { "input.voltage.nominal",   0, nullptr, "F\r", 22, '#', 1,  5,  "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.current.nominal",   0, nullptr, "F\r", 22, '#', 7,  9,  "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "battery.voltage.nominal", 0, nullptr, "F\r", 22, '#', 11, 15, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.frequency.nominal", 0, nullptr, "F\r", 22, '#', 17, 20, "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },

    /*
     * Common Qx protocols might only return ups.firmware here:
     *
     * > [I\r]
     * < [#-------------   ------     VT12046Q  \r]
     *    012345678901234567890123456789012345678
     *    0         1         2         3
     *
     *  Megatec IC adds serial/runtime support function into the response:
     *
     *  pc->ups:
     * > [I\r]
     *  ups->pc:
     * < [#MegaTecabcedfhi k130v12345 v3.400000\r]
     *    01234567890123456789012345678901234567
     *    0         1         2         3
     * where:
     *  device.mfr=MegaTec
     *  ups.serial=abcedfhi
     *  device.model=k130v
     *  battery.runtime=12345
     *  ups.firmware=v3.400000
     *
     * NOTE: Per example above, this reply is 38 bytes
     *  long (including '\r') not 39 as in other dialects
     *  and the mappings below; which part is wrong - the
     *  example or code? The "answer_len" field should be
     *  about "expected min length of answer" so 38 here
     * should cover both variants.
     */
    { "device.mfr",      0, nullptr, "I\r", 38, '#', 1,  7,  "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr }, /* Shorter field than in other dialects */
    { "ups.serial",      0, nullptr, "I\r", 38, '#', 8,  15, "%s", 0,                             nullptr, nullptr, nullptr }, /* Megatec IC adds "ups.serial" support function */
    { "device.model",    0, nullptr, "I\r", 38, '#', 17, 21, "%s", QX_FLAG_STATIC | QX_FLAG_TRIM, nullptr, nullptr, nullptr }, /* Shorter field than in other dialects */
    { "battery.runtime", 0, nullptr, "I\r", 38, '#', 22, 26, "%s", QX_FLAG_TRIM,                  nullptr, nullptr, nullptr }, /* Megatec IC adds "ups.runtime" support function */

    /* Notes:
     * 1) Per example above, this should end at 36 not 37 as in other dialects;
     *    we say 0 to go to end of line, wherever that is in fact;
     * 2) The QX_FLAG_ABSENT allows the driver init to not ignore devices which
     *    return all-spaces here, and the driver trims that - and treats the
     *    absent version immediately as the lack of protocol support. The effect
     *    should be similar to specifying "novendor" which bypasses the query
     *    altogether. TOTHINK: Set a default like "UNKNOWN"? [#3436]
     */
    { "ups.firmware",    0, nullptr, "I\r", 38, '#', 28, 0,  "%s", QX_FLAG_STATIC | QX_FLAG_TRIM | QX_FLAG_ABSENT, nullptr, nullptr, nullptr },

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
static void megatecInitups(QxEngine& qx) {
    blazerInitups(qx);
}

/* Subdriver interface */
const QxSubdriver QX_MEGATEC = {
    "Megatec 0.10",
    blazerClaim,
    MEGATEC_QX2NUT,
    megatecInitups,
    nullptr,
    "ACK",
    nullptr,
};
