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
 */

#ifndef QX_USB_CYPRESS_H
#define QX_USB_CYPRESS_H

#include <stddef.h>
#include <stdint.h>
#include "QxUsbTransport.h"

/**
 * @brief cypress_command() of nut_repo/drivers/nutdrv_qx.c:764-929 (US-141, NUT master
 * 1c4ab78): the command goes out in 8-byte SET_REPORT blocks (class, interface, 0x09, wValue
 * 0x200: Output report, ID 0), the answer comes back in 8-byte interrupt IN chunks up to '\r'
 * or a full buffer. The USB-to-serial bridge of 0665:5161 (Voltronic/Belkin, Legrand KEOR SP
 * of #150, IPPON of #12) and the other cypress rows of qx_usb_id[].
 *
 * The 0665:5161 quirk (NUT PR #3563 and #3720, cypress_0665_5161_quirk), on when setDevice()
 * gets that VID/PID as the default of upsdrv_initups() (nutdrv_qx.c:4175-4186): the bridge can
 * keep the reply of an earlier command on its interrupt endpoint, and taking it for the reply
 * of the next command puts every later reply one command behind. Before each command the
 * stale input is drained: a 25 ms read confirms an idle endpoint, but once a reply was left
 * unread (_replyOutstanding) or stale data turns up, the drain waits until the UPS stays quiet
 * for 500 ms (longer than its pauses within a reply). An endpoint still busy after 32 reports
 * or 3 s returns QX_USB_ERR_DESYNC without sending the command: the host then resets the
 * device (IQxHostPort::transportResult()). Each reply chunk waits up to 3000 ms instead of
 * 1000 ms. Stale data the UPS sent unasked is logged once (takeNotice()), until the endpoint
 * is found idle again.
 *
 * Differences from NUT:
 * - discardInput() before each command, for every cypress device. libusb reads the endpoint
 *   only inside usb_interrupt_read(); the host keeps the IN pipe running and queues every
 *   chunk, so the tail of an answer that came after a timeout (or an unsolicited chunk) would
 *   otherwise lead the answer of the next command. With the quirk, the chunks it drops count
 *   as discarded stale reports (log, and the 500 ms quiet wait that follows), but not towards
 *   the 32 report / 3 s limits: the bridge keeps at most 8 reports while nobody reads, the host
 *   queue many more, and the limits are about an endpoint that keeps sending during the drain.
 * - No cypress_drain_quirk option (ups.conf): the quirk follows the VID/PID, as NUT without it.
 * - The control transfer times out after QX_CTRL_TIMEOUT_MS (hid_host) instead of 5000 ms, and
 *   wIndex is the bound interface instead of a fixed 0 (these bridges have one interface).
 * - buflen < 8 fails without traffic (QX_USB_ERR_IO): upstream, buflen - 8 would wrap around
 *   on size_t. The engine always passes QX_SMALLBUF.
 * - The command budget (QxUsbTransport) cuts the waits: a reply that is not complete when it
 *   ends fails as a timeout, and stays outstanding for the next drain, as in NUT.
 */
class QxCypressTransport : public QxUsbTransport {
public:
    // Drain (500 ms quiet after stale data), 1-2 SET_REPORT (<= QX_CTRL_TIMEOUT_MS each) and the
    // reply chunks: the largest budget the poll step bound allows (12 s)
    static const uint32_t BUDGET_MS = 5250;
    // HID report type of the SET_REPORT: wValue 0x200 = Output (2) << 8 | report ID 0
    static const uint8_t REPORT_TYPE_OUTPUT = 0x02;
    static const size_t BLOCK = 8;
    // CYPRESS_REPLY_TIMEOUT, CYPRESS_0665_5161_REPLY_TIMEOUT
    static const uint32_t READ_TIMEOUT_MS = 1000;
    static const uint32_t QUIRK_READ_TIMEOUT_MS = 3000;
    // CYPRESS_0665_5161_FLUSH_REPORTS, _FLUSH_BUDGET, _FLUSH_TIMEOUT, _RESYNC_QUIET
    static const size_t FLUSH_REPORTS = 32;
    static const uint32_t FLUSH_BUDGET_MS = 3000;
    static const uint32_t FLUSH_TIMEOUT_MS = 25;
    static const uint32_t RESYNC_QUIET_MS = 500;
    // CYPRESS_VENDORID, and the product of the "Belkin F6C1200-UNV/Voltronic Power UPSes" row
    static const uint16_t QUIRK_VID = 0x0665;
    static const uint16_t QUIRK_PID = 0x5161;

    explicit QxCypressTransport(IQxUsbPrimitives& usb) : QxUsbTransport(usb) {}

    const char* name() const override { return "cypress"; }
    uint32_t commandBudgetMs() const override { return BUDGET_MS; }
    bool needsInterruptIn() const override { return true; }
    void setDevice(uint16_t vid, uint16_t pid) override { _quirk = vid == QUIRK_VID && pid == QUIRK_PID; }
    const char* takeNotice() override;

    bool quirk() const { return _quirk; }

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;

private:
    // The quirk block of cypress_command(): 0, or the error that ends the command
    int drainStaleInput(const char* cmd);

    bool _quirk = false;
    bool _replyOutstanding = false; // cypress_reply_outstanding
    bool _staleReported = false;    // cypress_stale_reported
    bool _noticePending = false;
    char _notice[128] = {0};
};

#endif // QX_USB_CYPRESS_H
