/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx_voltronic-qs-hex.c:
 *   Copyright (C) 2016 Daniele Pezzini <hyouko@gmail.com>
 */

#include "QxProtocols.h"
#include "QxBlazerCommon.h"
#include "QxEngine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Preprocess the raw binary QS\r answer into the ASCII hex/bitstring format:
 * Raw (P): # <AB:2B> ' ' <C:1B> ' ' <DE:2B> ' ' <F:1B> ' ' <G:1B> ' ' <HI:2B> ' ' <JKL:3B> ' ' <M:1B> ' ' <N:1B> ' ' <O:1B> \r
 * Escape sequences in raw bytes: 0x28 0x00->0x0D, 0x28 0x01->0x11, 0x28 0x02->0x13, 0x28 0x03->0x0A, 0x28 0x04->0x20
 * Decoded text: #XXXX XX XXXX XX XX XXXX XXXXXX XX XX BBBBBBBB
 */
static int qsHexPreprocessAnswer(QxEngine& qx, const QxItem& item, int len) {
    (void)item;
    char* ans = qx.answer();
    if (len < 27 || ans[0] != '#') return -1;

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(ans);
    uint8_t unescaped[32];
    int ulen = 0;

    for (int i = 0; i < len && ulen < (int)sizeof(unescaped); i++) {
        if (raw[i] == 0x28 && i + 1 < len && raw[i + 1] <= 0x04) {
            static const uint8_t map[5] = { 0x0D, 0x11, 0x13, 0x0A, 0x20 };
            unescaped[ulen++] = map[raw[i + 1]];
            i++;
        } else {
            unescaped[ulen++] = raw[i];
        }
    }

    if (ulen < 26 || unescaped[0] != '#') return -1;

    char decoded[64];
    int pos = 0;

    decoded[pos++] = '#';
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x%02x ", unescaped[1], unescaped[2]);     // AB (1..4)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x ", unescaped[4]);                       // C  (6..7)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x%02x ", unescaped[6], unescaped[7]);     // DE (9..12)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x ", unescaped[9]);                       // F  (14..15)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x ", unescaped[11]);                      // G  (17..18)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x%02x ", unescaped[13], unescaped[14]);   // HI (20..23)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x%02x%02x ", unescaped[16], unescaped[17], unescaped[18]); // JKL (25..30)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x ", unescaped[20]);                      // M  (32..33)
    pos += snprintf(decoded + pos, sizeof(decoded) - pos, "%02x ", unescaped[22]);                      // N  (35..36)

    uint8_t st = unescaped[24]; // O (38..45)
    for (int b = 7; b >= 0; b--) {
        decoded[pos++] = (st & (1U << b)) ? '1' : '0';
    }
    decoded[pos] = '\0';

    if ((size_t)pos >= QX_SMALLBUF) return -1;
    memcpy(ans, decoded, (size_t)pos + 1);
    return pos;
}

static int qsHexProcessHex(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* ans = qx.answer();
    double val = 0.0;

    if (strcmp(item.info_type, "input.voltage") == 0) {
        // AB * C / 51 / 256
        unsigned long ab = strtoul(ans + 1, nullptr, 16);
        unsigned long c  = strtoul(ans + 6, nullptr, 16);
        val = (double)ab * (double)c / 51.0 / 256.0;
    } else if (strcmp(item.info_type, "output.voltage") == 0) {
        // DE * F / 51 / 256
        unsigned long de = strtoul(ans + 9, nullptr, 16);
        unsigned long f  = strtoul(ans + 14, nullptr, 16);
        val = (double)de * (double)f / 51.0 / 256.0;
    } else if (strcmp(item.info_type, "ups.load") == 0) {
        // G
        unsigned long g = strtoul(qx.value(), nullptr, 16);
        val = (double)g;
    } else if (strcmp(item.info_type, "output.frequency") == 0) {
        // JKL / HI
        unsigned long hi  = strtoul(ans + 20, nullptr, 16);
        unsigned long jkl = strtoul(ans + 25, nullptr, 16);
        val = (hi > 0) ? ((double)jkl / (double)hi) : 0.0;
        if (val > 99.9) val = 99.9;
    } else if (strcmp(item.info_type, "battery.voltage") == 0) {
        // M * N / 510
        unsigned long m = strtoul(ans + 32, nullptr, 16);
        unsigned long n = strtoul(ans + 35, nullptr, 16);
        val = (double)m * (double)n / 510.0;
    }

    snprintf(value, valuelen, item.dfl ? item.dfl : "%.1f", val);
    return 0;
}

static int qsHexProcessStatusBits(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    const char* val = qx.value();
    if (!val || val[0] == '\0') return -1;

    switch (item.from) {
        case 38: /* Utility Fail (Immediate) */
            if (val[0] == '1') {
                qx.updateStatus("OB");
            } else {
                qx.updateStatus("OL");
            }
            break;
        case 39: /* Battery Low */
            if (val[0] == '1') qx.updateStatus("LB");
            break;
        case 40: /* Bypass/Boost or Buck Active */
            if (val[0] == '1') {
                const char* type = qx.getinfo("ups.type");
                if (type && strstr(type, "offline")) {
                    qx.updateStatus("BOOST");
                } else {
                    qx.updateStatus("BYPASS");
                }
            }
            break;
        case 41: /* UPS Failed */
            if (val[0] == '1') qx.alarmSet("UPS failed!");
            break;
        case 42: /* UPS Type (1 = offline / line interactive, 0 = online) */
            snprintf(value, valuelen, "%s", val[0] == '1' ? "offline / line interactive" : "online");
            return 0;
        case 43: /* Test in Progress */
            if (val[0] == '1') qx.updateStatus("CAL");
            break;
        case 44: /* Shutdown Active */
            if (val[0] == '1') qx.updateStatus("FSD");
            break;
        case 45: /* Beeper On */
            snprintf(value, valuelen, "%s", val[0] == '1' ? "enabled" : "disabled");
            return 0;
        default:
            return -1;
    }
    return 0;
}

static const QxItem VOLTRONIC_QS_HEX_QX2NUT[] = {
    // Decoded: #XXXX XX XXXX XX XX XXXX XXXXXX XX XX BBBBBBBB
    //          0123456789012345678901234567890123456789012345
    { "input.voltage",    0, nullptr, "QS\r", 27, '#', 1,  4,  "%.1f", 0, nullptr, qsHexPreprocessAnswer, qsHexProcessHex },
    { "output.voltage",   0, nullptr, "QS\r", 27, '#', 9,  12, "%.1f", 0, nullptr, qsHexPreprocessAnswer, qsHexProcessHex },
    { "ups.load",         0, nullptr, "QS\r", 27, '#', 17, 18, "%.0f", 0, nullptr, qsHexPreprocessAnswer, qsHexProcessHex },
    { "output.frequency", 0, nullptr, "QS\r", 27, '#', 20, 23, "%.1f", 0, nullptr, qsHexPreprocessAnswer, qsHexProcessHex },
    { "battery.voltage",  0, nullptr, "QS\r", 27, '#', 32, 33, "%.2f", 0, nullptr, qsHexPreprocessAnswer, qsHexProcessHex },

    /* Status bits (positions 38..45 in decoded string) */
    { "ups.status",        0, nullptr, "QS\r", 27, '#', 38, 38, nullptr, QX_FLAG_QUICK_POLL, nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.status",        0, nullptr, "QS\r", 27, '#', 39, 39, nullptr, QX_FLAG_QUICK_POLL, nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.status",        0, nullptr, "QS\r", 27, '#', 40, 40, nullptr, QX_FLAG_QUICK_POLL, nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.alarm",         0, nullptr, "QS\r", 27, '#', 41, 41, nullptr, 0,                  nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.type",          0, nullptr, "QS\r", 27, '#', 42, 42, "%s",    QX_FLAG_STATIC,     nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.status",        0, nullptr, "QS\r", 27, '#', 43, 43, nullptr, QX_FLAG_QUICK_POLL, nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.status",        0, nullptr, "QS\r", 27, '#', 44, 44, nullptr, QX_FLAG_QUICK_POLL, nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },
    { "ups.beeper.status", 0, nullptr, "QS\r", 27, '#', 45, 45, "%s",    0,                  nullptr, qsHexPreprocessAnswer, qsHexProcessStatusBits },

    /* Instant commands */
    { "beeper.toggle",    0, nullptr, "Q\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "load.off",         0, nullptr, "S00R0000\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "load.on",          0, nullptr, "C\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { "shutdown.return",  0, nullptr, "S%s\r",      0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, blazerProcessCommand },
    { "shutdown.stayoff", 0, nullptr, "S%sR0000\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, blazerProcessCommand },
    { "shutdown.stop",    0, nullptr, "C\r",        0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },

    /* End of structure. */
    { nullptr, 0, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr }
};

const QxSubdriver QX_VOLTRONIC_QS_HEX = {
    "Voltronic-QS-Hex 0.10",
    blazerClaimLight,
    VOLTRONIC_QS_HEX_QX2NUT,
    blazerInitupsLight,
    nullptr,
    "ACK",
    nullptr,
};
