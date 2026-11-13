/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *                 2016 Eaton
 *   Based on:
 *    usbhid-ups.c - Copyright (C)
 *      2003-2012 Arnaud Quette <arnaud.quette@gmail.com>
 *      2005      John Stamp <kinsayder@hotmail.com>
 *      2005-2006 Peter Selinger <selinger@users.sourceforge.net>
 *      2007-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *    blazer.c - Copyright (C)
 *      2008-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *      2012      Arnaud Quette <ArnaudQuette@Eaton.com>
 *    blazer_ser.c - Copyright (C)
 *      2008      Arjen de Korte <adkorte-guest@alioth.debian.org>
 *    blazer_usb.c - Copyright (C)
 *      2003-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *      2011-2012 Arnaud Quette <arnaud.quette@free.fr>
 *    Masterguard additions
 *      2020-2021 Edgar Fuß, Mathematisches Institut der Universität Bonn <ef@math.uni-bonn.de>
 *    Armac (Richcomm-variant) additions
 *      2021      Tomasz Fortuna <bla@thera.be>
 *
 * qxGetStringAscii() follows libusb_get_string_descriptor_ascii() of libusb
 * (https://libusb.info/, libusb/descriptor.c), LGPL-2.1-or-later:
 *   Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 *                 2001 Johannes Erdfelt <johannes@erdfelt.com>
 */

#include "QxUsbKrauler.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// The krauler family of nut_repo/drivers/nutdrv_qx.c (US-137). The upsdebugx() lines are not
// ported: every command, its answer and the primitive error go to the Q* exchanges of the
// diagnostics export (QxDriver::Recorder).

namespace {

const uint8_t DT_STRING = 0x03; // LIBUSB_DT_STRING

// strcspn(buf, "\r") == 10 && !strncasecmp(buf, "UPS No Ack", 10)
bool isUpsNoAck(const char* buf) {
    static const char NO_ACK[] = "UPS No Ack";
    if (strcspn(buf, "\r") != 10) return false;
    for (size_t i = 0; i < 10; i++) {
        char a = buf[i], b = NO_ACK[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// The "if (buflen > INT_MAX)" guard of every *_command()
size_t capBuflen(size_t buflen) {
    return buflen > (size_t)INT_MAX ? (size_t)(INT_MAX - 1) : buflen;
}

// "Echo the unknown command back": no USB transfer, the engine sees the echo as a failure
int echo(const char* cmd, char* buf, size_t buflen) {
    return snprintf(buf, buflen, "%s", cmd);
}

// Megatec command -> string index of fabula and hunnox
struct IndexRow {
    const char* str;
    int index;
};

const IndexRow FABULA_COMMANDS[] = {
    { "Q1\r", 0x03 }, // Status
    { "F\r",  0x0d }, // Ratings
    { "I\r",  0x0c }, // Vendor infos
    { "Q\r",  0x07 }, // Beeper toggle
    { "C\r",  0x0a }, // Cancel shutdown/Load on [0x(0..F)A]
    { nullptr, 0 }
};

const IndexRow HUNNOX_COMMANDS[] = {
    { "Q1\r", 0x03 }, // Status
    { "F\r",  0x0d }, // Ratings
    { "I\r",  0x0c }, // Vendor infos
    { "Q\r",  0x07 }, // Beeper toggle
    { "C\r",  0x0a }, // Cancel shutdown/Load on [0x(0..F)A]
    { "BL\r", 0xf3 }, // Battery charge
    { nullptr, 0 }
};

// The command lookup and the shutdown encoding shared by fabula_command() and
// hunnox_command() (nutdrv_qx.c:1452-1500, 1582-1626): the string index, 0 when the command
// cannot be encoded
int fabulaIndex(const char* cmd, size_t cmdlen, const IndexRow* commands) {
    int index = 0;
    for (int i = 0; commands[i].str; i++) {
        if (strncmp(cmd, commands[i].str, cmdlen)) continue;
        index = commands[i].index;
        break;
    }
    if (index) return index;

    int val2 = -1;
    double val1 = -1;

    // Shutdowns
    if (sscanf(cmd, "S%lfR%d\r", &val1, &val2) == 2 || sscanf(cmd, "S%lf\r", &val1) == 1) {
        double delay;

        // 0x(1+)0 -> shutdown.stayoff (SnR0000)
        // 0x(1+)8 -> shutdown.return (Sn[Rm], m != 0)
        //   [delay before restart is always 10 seconds]
        // +0x10 (16dec) = next megatec delay
        //   (min .5 = hex 0x1*; max 10 = hex 0xF*) -> n < 1 ? -> n += .1; n >= 1 ? -> n += 1

        // delay: [.5..10] (-> seconds: [30..600])
        delay = val1 < .5 ? .5 : val1 > 10 ? 10 : val1;

        if (delay < 1)
            index = (int)(16 + round((delay - .5) * 10) * 16);
        else
            index = (int)(96 + (delay - 1) * 16);

        // shutdown.return (Sn[Rm], m != 0); "S<n>\r" leaves val2 at -1
        if (val2)
            index += 8;
    }
    return index;
}

// "If the reply lacks the expected terminating CR, add it (if there's enough space)"
int addMissingCr(char* buf, size_t buflen, int ret) {
    if (ret && memchr(buf, '\r', (size_t)ret) == NULL) {
        if ((size_t)ret < buflen - 1) {
            buf[ret++] = '\r';
            buf[ret] = 0;
        }
    }
    return ret;
}

} // namespace

namespace {

// Copies the first bytes received into a QxStringFailure field
void keepBytes(uint8_t* dst, const uint8_t* src, int received) {
    size_t n = received > 0 ? (size_t)received : 0;
    if (n > QxStringFailure::BYTES_MAX) n = QxStringFailure::BYTES_MAX;
    memset(dst, 0, QxStringFailure::BYTES_MAX);
    if (n) memcpy(dst, src, n);
}

} // namespace

int qxGetStringAscii(IQxUsbPrimitives& usb, uint8_t index, char* buf, size_t buflen, QxStringFailure* failure) {
    // libusb_get_string_descriptor_ascii(), as libusb 1.0.2x: the langid list is read with a
    // 255-byte buffer like the string itself, and again for every command (no cache). The
    // most recent libusb asks only 4 bytes of string 0: switch to that if a tester's bridge
    // fails on this read. One byte more than libusb's tbuf[255]: with bLength 255 the loop
    // below reads tbuf[255] (libusb reads past its buffer there).
    uint8_t tbuf[256];
    memset(tbuf, 0, sizeof(tbuf));

    // There's no point in trying to read descriptor 0 with this function
    if (index == 0 || buflen == 0) return QX_USB_ERR_INVALID_PARAM;

    // Raw bytes of a refused read, for the diagnostics export (issue #91)
    QxStringFailure seen;
    seen.index = index;
    auto fail = [&](int ret) {
        if (failure) {
            seen.present = true;
            seen.result = ret;
            *failure = seen;
        }
        return ret;
    };

    int r = usb.getStringDescriptor(0, 0, tbuf, 255);
    seen.langidsLength = r;
    keepBytes(seen.langids, tbuf, r);
    if (r < 0) return fail(r);
    if (r < 4) return fail(QX_USB_ERR_IO);

    const uint16_t langid = (uint16_t)(tbuf[2] | (tbuf[3] << 8));

    memset(tbuf, 0, sizeof(tbuf));
    r = usb.getStringDescriptor(index, langid, tbuf, 255);
    seen.answerAsked = true;
    seen.answerLength = r;
    keepBytes(seen.answer, tbuf, r);
    if (r < 0) return fail(r);
    // Shorter than the descriptor header (as the recent libusb: the old one compared the
    // stale bytes of the langid list, with the same result)
    if (r < 2) return fail(QX_USB_ERR_IO);
    if (tbuf[1] != DT_STRING) return fail(QX_USB_ERR_IO);
    if (tbuf[0] > r) return fail(QX_USB_ERR_IO);

    size_t di = 0;
    for (size_t si = 2; si < tbuf[0]; si += 2) {
        if (di >= buflen - 1) break;
        if ((tbuf[si] & 0x80) || tbuf[si + 1]) // non-ASCII
            buf[di++] = '?';
        else
            buf[di++] = (char)tbuf[si];
    }
    buf[di] = 0;
    return (int)di;
}

int qxGetStringLangid(IQxUsbPrimitives& usb, uint8_t index, uint16_t langid, char* buf, size_t buflen,
                      bool& mismatch) {
    mismatch = false;
    if (buflen == 0) return QX_USB_ERR_INVALID_PARAM;
    // NUT reads into buf itself, asking buflen bytes: here a local copy, as the primitive asks
    // at most 255 (hid_host.c caps wLength). One byte more: an odd bLength 255 makes the loop
    // read tbuf[255].
    uint8_t tbuf[256];
    memset(tbuf, 0, sizeof(tbuf));
    const size_t want = buflen < 255 ? buflen : 255;
    int ret = usb.getStringDescriptor(index, langid, tbuf, want);
    if (ret <= 0) return ret;

    // Invalid receive size - message corrupted
    if (ret != tbuf[0]) {
        mismatch = true;
        return 0;
    }

    // Simple unicode -> ASCII inplace conversion
    const unsigned size = tbuf[0];
    size_t di = 0;
    for (unsigned si = 2; si < size; si += 2) {
        if (di >= buflen - 1) break;
        if (tbuf[si + 1]) // high byte
            buf[di++] = '?';
        else
            buf[di++] = (char)tbuf[si];
    }
    buf[di] = 0;
    return (int)di;
}

int QxLangidFallback::read(IQxUsbPrimitives& usb, uint8_t index, char* buf, size_t buflen, bool& mismatch) {
    mismatch = false;
    if (_active) return qxGetStringLangid(usb, index, LANGID, buf, buflen, mismatch);

    int ret = qxGetStringAscii(usb, index, buf, buflen, &_failure);
    if (ret >= 0 || ret == QX_USB_ERR_NO_DEVICE || ret == QX_USB_ERR_INVALID_PARAM) return ret;

    // usb_get_string_simple() failed: the same string as NUT with langid_fix = 0x0409. Past
    // the command budget the primitive refuses it without traffic.
    bool fixMismatch = false;
    int fix = qxGetStringLangid(usb, index, LANGID, buf, buflen, fixMismatch);
    if (fix > 0) {
        _active = true;
        return fix;
    }
    // No help: the outcome of NUT without langid_fix
    buf[0] = 0;
    return ret;
}

// --- krauler ---

int QxKraulerTransport::doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) {
    // Still not implemented:
    // 0x6	T<n>	(don't know how to pass the parameter)
    // 0x68 and 0x69 both cause shutdown after an undefined interval
    static const struct {
        const char* str; // Megatec command
        int index;       // Krauler string index for this command
        char prefix;     // Character to replace the first byte in reply
    } command[] = {
        { "Q1\r", 0x03, '(' },
        { "F\r",  0x0d, '#' },
        { "I\r",  0x0c, '#' },
        { "T\r",  0x04, '\r' },
        { "TL\r", 0x05, '\r' },
        { "Q\r",  0x07, '\r' },
        { "C\r",  0x0b, '\r' },
        { "CT\r", 0x0b, '\r' },
        { nullptr, 0, '\0' }
    };

    buflen = capBuflen(buflen);

    for (int i = 0; command[i].str; i++) {
        // A prefix comparison on cmdlen bytes, as upstream
        if (strncmp(cmd, command[i].str, cmdlen)) continue;

        for (int retry = 0; retry < 10; retry++) {
            // Upstream: usb_get_string() with langid_fix when set (nutdrv_qx.c:1338-1342) and
            // the size check plus unicode conversion of 1358-1382; here langid_fix is applied
            // by itself (QxLangidFallback, issue #91)
            bool mismatch = false;
            int ret = _strings.read(_usb, (uint8_t)command[i].index, buf, buflen, mismatch);

            // langid_fix: "size mismatch", retried
            if (mismatch) continue;

            if (ret <= 0) return ret;

            ret = addMissingCr(buf, buflen, ret);

            // "UPS No Ack" has a special meaning
            if (isUpsNoAck(buf)) continue;

            // Replace the first byte of what we received with the correct one
            buf[0] = command[i].prefix;

            return ret;
        }

        return 0;
    }

    return echo(cmd, buf, buflen);
}

// --- fabula ---

int QxFabulaTransport::doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) {
    buflen = capBuflen(buflen);

    const int index = fabulaIndex(cmd, cmdlen, FABULA_COMMANDS);
    if (!index) return echo(cmd, buf, buflen); // Unknown commands

    // Send command/Read reply
    int ret = qxGetStringAscii(_usb, (uint8_t)index, buf, buflen);

    if (ret <= 0) return ret;

    ret = addMissingCr(buf, buflen, ret);

    // The UPS always replies "UPS No Ack" when a supported command is issued (either if it
    // fails or if it succeeds).. because of that, always return 0 (with buf empty, as if it
    // was a timeout): queries will see it as a failure, instant commands ('megatec'
    // protocol) as a success
    if (isUpsNoAck(buf)) {
        memset(buf, 0, buflen);
        return 0;
    }

    return ret;
}

// --- hunnox ---

void QxHunnoxTransport::preamble(int askingFor) {
    // The answers are not used and the results are ignored, as upstream. Upstream reads the
    // first three into a 1030-byte buffer with 1026: the primitive asks at most 255 bytes
    // (hid_host.c caps wLength there) and never writes more, and a string descriptor is never
    // longer than 255 bytes. With novendor step 3 reads nothing (US-138).
    uint8_t tmp[255];
    // langid_fix_local: 0x0409, the langid_fix QxLangidFallback applies too
    const uint16_t langid = PREAMBLE_LANGID;

    switch (_step) {
    case 0:
        _usb.getStringDescriptor(0x00, langid, tmp, 1026);
        _usb.getStringDescriptor(0x00, langid, tmp, 1026);
        _usb.getStringDescriptor(0x01, langid, tmp, 1026);
        _usb.sleepMs(10);
        break;
    case 1:
        if (askingFor != 0x0d) _usb.getStringDescriptor(0x0d, langid, tmp, 102);
        break;
    case 2:
        if (askingFor != 0x03) _usb.getStringDescriptor(0x03, langid, tmp, 102);
        break;
    case 3:
        if (askingFor != 0x0c && !_novendor) _usb.getStringDescriptor(0x0c, langid, tmp, 102);
        break;
    default:
        _step = 0;
    }
    _step++;
    if (_step > 3) _step = 1;
}

int QxHunnoxTransport::doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) {
    buflen = capBuflen(buflen);

    const int index = fabulaIndex(cmd, cmdlen, HUNNOX_COMMANDS);
    // Unknown commands: echoed before the lock-step preamble, which does not advance
    if (!index) return echo(cmd, buf, buflen);

    // Enable lock-step protocol for Hunnox (hunnox_protocol() always returns 0)
    preamble(index);

    // Seems that if we inform a large buffer, the USB locks. This value was captured from
    // the Windows "official" client. Without langid_fix the request still asks 255 bytes
    // (usb_get_string_simple()): the cap limits the answer to 101 characters
    if (buflen > 102) buflen = 102;

    // Send command/Read reply. Upstream: usb_get_string() with langid_fix when set and the
    // size check plus unicode conversion of nutdrv_qx.c:1649-1690; here langid_fix is applied
    // by itself (QxLangidFallback, issue #91), a size mismatch returns 0 as upstream.
    // Unlike fabula, no missing CR is added.
    bool mismatch = false;
    int ret = _strings.read(_usb, (uint8_t)index, buf, buflen, mismatch);

    if (ret <= 0) return ret;

    // The UPS always replies "UPS No Ack" when a supported command is issued (either if it
    // fails or if it succeeds).. because of that, always return 0 (with buf empty, as if it
    // was a timeout): queries will see it as a failure, instant commands ('megatec'
    // protocol) as a success
    if (isUpsNoAck(buf)) {
        memset(buf, 0, buflen);
        return 0;
    }

    return ret;
}
