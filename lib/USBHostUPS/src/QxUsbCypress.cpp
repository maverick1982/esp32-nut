/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026. See QxUsbCypress.h for the copyright lines
 * of drivers/nutdrv_qx.c.
 */

#include "QxUsbCypress.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "QxTypes.h"

// Row by row copy of cypress_command(), nut_repo/drivers/nutdrv_qx.c:764-929 (US-141, NUT
// master 1c4ab78). The upsdebugx() lines are not ported: every command, its answer and the
// primitive error go to the Q* exchanges of the diagnostics export (QxDriver::Recorder). The
// upslogx(LOG_NOTICE) of stale data becomes takeNotice().

const char* QxCypressTransport::takeNotice() {
    if (!_noticePending) return nullptr;
    _noticePending = false;
    return _notice;
}

int QxCypressTransport::drainStaleInput(const char* cmd) {
    char tmp[BLOCK];
    int ret;
    size_t i;

    // Not upstream: the chunks the host queued since the last command (see the class). Each of
    // them is a read of upstream that returned at once
    const size_t queued = _usb.discardInput();

    // An idle endpoint is confirmed by a short read. But once a reply was left unread, or
    // stale data turns up, more of it may still be on its way: wait until the UPS stays quiet
    // for longer than it pauses within a reply.
    //
    // Read once more than the number of reports we accept, so a timeout confirms that the
    // endpoint is empty. Do not send a command if it remains busy (for too many reports or for
    // too long, as either would stall the driver): its reply could not be associated reliably.
    const uint32_t flushStart = _usb.nowMs();
    for (i = 0; i <= FLUSH_REPORTS; i++) {
        ret = _usb.readInterrupt((uint8_t*)tmp, BLOCK,
                                 (_replyOutstanding || i > 0 || queued > 0) ? RESYNC_QUIET_MS : FLUSH_TIMEOUT_MS);

        if (ret == QX_USB_ERR_TIMEOUT) break;

        // An empty report is no reply to this command either: discard it like any other
        if (ret < 0) return ret;

        if (i == FLUSH_REPORTS || (uint32_t)(_usb.nowMs() - flushStart) > FLUSH_BUDGET_MS) {
            return QX_USB_ERR_DESYNC;
        }
    }

    const size_t discarded = queued + i;
    if (discarded == 0) {
        _staleReported = false;
    } else if (!_replyOutstanding && !_staleReported) {
        // Name the command by its readable start: commands of some protocols end with binary
        // CRC bytes
        size_t cmdshown = 0;
        while (cmd[cmdshown] && isprint((unsigned char)cmd[cmdshown])) cmdshown++;

        // The device sent data unasked: logged once, not on every poll of a device which
        // keeps doing it
        snprintf(_notice, sizeof(_notice),
                 "[Q*] cypress: discarded %u stale input report(s) before %.*s: replies were out of step with commands",
                 (unsigned)discarded, (int)cmdshown, cmd);
        _noticePending = true;
        _staleReported = true;
    }
    // Otherwise the rest of an unread reply is expected (e.g. during protocol detection), or
    // the stale data was logged already: upsdebugx(2) upstream
    return 0;
}

int QxCypressTransport::doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) {
    char tmp[QX_SMALLBUF];
    size_t tmplen;
    int ret = 0;
    uint32_t replyTimeout = READ_TIMEOUT_MS;
    size_t i;

    // Not upstream: buflen - 8 below would wrap around
    if (buflen < BLOCK) return QX_USB_ERR_IO;

    if (_quirk) {
        replyTimeout = QUIRK_READ_TIMEOUT_MS;
        if (int r = drainStaleInput(cmd)) return r;
    } else {
        // Not upstream: the IN pipe of the host runs between commands (see the class)
        _usb.discardInput();
    }

    // Until its reply was read completely
    _replyOutstanding = true;

    // Send command
    memset(tmp, 0, sizeof(tmp));
    tmplen = cmdlen > sizeof(tmp) ? sizeof(tmp) : cmdlen;
    memcpy(tmp, cmd, tmplen);

    for (i = 0; i < tmplen; i += (size_t)ret) {
        // Write data in 8-byte chunks
        ret = _usb.setReport(REPORT_TYPE_OUTPUT, 0, (const uint8_t*)&tmp[i], BLOCK);
        if (ret <= 0) return ret;
    }

    // Read reply
    memset(buf, 0, buflen);

    for (i = 0; (i <= buflen - BLOCK) && (memchr(buf, '\r', buflen) == NULL); i += (size_t)ret) {
        // Read data in 8-byte chunks. Any errors here mean that we are unable to read a reply
        // (which will happen after successfully writing a command to the UPS)
        ret = _usb.readInterrupt((uint8_t*)&buf[i], BLOCK, replyTimeout);
        if (ret <= 0) return ret;
    }

    if (memchr(buf, '\r', i) != NULL) _replyOutstanding = false; // the reply was read completely

    return (int)i;
}
