/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx_innovatae.c:
 *   Copyright (C) 2025 Viktor Drobot <linux776@gmail.com>
 */

#include "QxProtocols.h"
#include "QxBlazerCommon.h"
#include "QxEngine.h"

// Row by row copy of nut_repo/drivers/nutdrv_qx_innovatae.c:28-148 (INNOVATAE_VERSION
// "INNOVATAE 0.01"). Columns of item_t without the runtime answer and value (QxTypes.h):
// info_type, info_flags, info_rw, command, answer_len, leading, from, to, dfl, qxflags,
// preprocess_command, preprocess_answer, preprocess. DEFAULT_ONDELAY "180" and
// DEFAULT_OFFDELAY "30" of nutdrv_qx.h. The testing table (innovatae_testing) lives in
// test/mocks/QxTestingTables.h.
//
// Note on the probing order (nutdrv_qx.c:95-117): innovatae claims with the status query
// only (blazer_claim_light, Q1) and comes before the q1 fallback, so a unit that answers
// just Q1 is claimed by this protocol, as in NUT.

/* qx2nut lookup table */
static const QxItem INNOVATAE_QX2NUT[] = {
    /* Static values */
    { "device.mfr", 0, nullptr, "", 0, '\0', 0, 0, "Ippon", QX_FLAG_STATIC | QX_FLAG_ABSENT, nullptr, nullptr, nullptr },

    /*
     * > [Q1\r]
     * < [(226.0 195.0 226.0 014 49.0 27.5 30.0 00001000\r]
     *    01234567890123456789012345678901234567890123456
     *    0         1         2         3         4
     */

    /* Common parameters */
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
     * < [#230.0 004 024.0 50.0\r]
     *    0123456789012345678901
     *    0         1         2
     */

    /* Nominal parameters (ratings) */
    { "input.voltage.nominal",   0, nullptr, "F\r", 22, '#', 1,  5,  "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.current.nominal",   0, nullptr, "F\r", 22, '#', 7,  9,  "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "battery.voltage.nominal", 0, nullptr, "F\r", 22, '#', 11, 15, "%.1f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.frequency.nominal", 0, nullptr, "F\r", 22, '#', 17, 20, "%.0f", QX_FLAG_STATIC, nullptr, nullptr, nullptr },

    /*
     * > [I\r]
     * < [#222222222222222222222222222R1.00.48  \r]
     *    012345678901234567890123456789012345678
     *    0         1         2         3
     */

    /* Firmware version */
    /* According to the Megatec Qx protocol implementation (nutdrv_qx_megatec.c) firmware version can be reported
     * via I command (positions 28-35) and answer length may vary between 38 and 39.
     * Referenced source suggests to use lesser number
     */
    { "ups.firmware", 0, nullptr, "I\r", 38, '#', 28, 35, "%s", QX_FLAG_STATIC, nullptr, nullptr, nullptr },

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
static void innovataeInitups(QxEngine& qx) {
    blazerInitupsLight(qx);
}

/* Subdriver interface */
const QxSubdriver QX_INNOVATAE = {
    "INNOVATAE 0.01",
    blazerClaimLight,
    INNOVATAE_QX2NUT,
    innovataeInitups,
    nullptr,
    "ACK",
    "NAK\r",
};
