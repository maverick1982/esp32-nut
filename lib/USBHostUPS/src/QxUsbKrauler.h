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
 * drivers/usb-common.h:
 *   Copyright (C) 2008 - 2016 Arnaud Quette <arnaud.quette@gmail.com>
 *                 2021 Jim Klimov <jimklimov+nut@gmail.com>
 */

#ifndef QX_USB_KRAULER_H
#define QX_USB_KRAULER_H

#include <stddef.h>
#include <stdint.h>
#include "QxUsbTransport.h"

/**
 * @brief The krauler family of the Q* USB subdrivers (ADR 0013, section 3; US-137): the
 * Megatec command is encoded as a string descriptor index, the answer is that string.
 *
 * Ports of krauler_command(), fabula_command(), hunnox_command() and hunnox_protocol() of
 * nut_repo/drivers/nutdrv_qx.c:1240-1711, without the noscanlangid option (only a debug
 * line in NUT); novendor is read by hunnox_protocol() since US-138
 * (QxUsbTransport::setOptions()). Without langid_fix the three read the answer with
 * usb_get_string_simple(), ported as qxGetStringAscii(). langid_fix is not a setting:
 * krauler and hunnox, the two that read it in NUT, switch to 0x0409 by themselves when
 * usb_get_string_simple() fails and the langid_fix read answers (QxLangidFallback, issue #91).
 */

/**
 * @brief usb_get_string_simple() of NUT (nut_repo/drivers/usb-common.h:352-371), that is
 * libusb_get_string_descriptor_ascii(): the langid list (string 0), then the string with the
 * first langid, converted to ASCII ('?' for the other characters), at most buflen - 1
 * characters and a NUL.
 * @param failure when not null, filled with the raw bytes on an error after some traffic
 * @return The characters copied, or an error < 0 (QX_USB_ERR_IO for a malformed descriptor,
 *         QX_USB_ERR_INVALID_PARAM for index 0, without traffic)
 */
int qxGetStringAscii(IQxUsbPrimitives& usb, uint8_t index, char* buf, size_t buflen,
                     QxStringFailure* failure = nullptr);

/**
 * @brief The read of krauler_command() and hunnox_command() with langid_fix
 * (nutdrv_qx.c:1338-1384, 1649-1690): usb_get_string(index, langid) asking buflen bytes
 * (at most 255), the "size mismatch" check (bytes received == bLength) and the in-place
 * UTF-16 -> ASCII conversion ('?' for a unit with a high byte), at most buflen - 1 characters.
 * @param mismatch set to true on a size mismatch (then the return is 0, buf untouched)
 * @return The characters copied, 0, or the error of the primitive
 */
int qxGetStringLangid(IQxUsbPrimitives& usb, uint8_t index, uint16_t langid, char* buf, size_t buflen,
                      bool& mismatch);

/**
 * @brief langid_fix applied by itself (issue #91).
 *
 * A read starts as NUT without langid_fix (qxGetStringAscii()). Only if that fails with an
 * error other than "no device", the same string is read as NUT with langid_fix = 0x0409
 * (qxGetStringLangid()); when that gives an answer, every later read of the transport (one
 * attach) goes that way only, as NUT with the option set. A device that answers
 * usb_get_string_simple() never sees the second read. The VIDA Armour 1200VA of #91 needs
 * langid_fix = 0x0409 in NUT; NUT's own source suggests reading the langid list "should
 * allow automatic application of the workaround" (nutdrv_qx.c:3924-3943).
 */
class QxLangidFallback {
public:
    static const uint16_t LANGID = 0x0409;

    /**
     * @param mismatch set to true only on a size mismatch of the langid_fix read once active
     *        (krauler retries there, hunnox returns 0, as upstream)
     * @return The characters copied, 0, or an error < 0 (the one of usb_get_string_simple()
     *         when the langid_fix read did not help)
     */
    int read(IQxUsbPrimitives& usb, uint8_t index, char* buf, size_t buflen, bool& mismatch);

    bool active() const { return _active; }
    const QxStringFailure& failure() const { return _failure; }

private:
    bool _active = false;
    QxStringFailure _failure;
};

// krauler_command(), nutdrv_qx.c:1294-1424
class QxKraulerTransport : public QxUsbTransport {
public:
    static const uint32_t BUDGET_MS = 3000;

    explicit QxKraulerTransport(IQxUsbPrimitives& usb) : QxUsbTransport(usb) {}

    const char* name() const override { return "krauler"; }
    uint32_t commandBudgetMs() const override { return BUDGET_MS; }
    uint16_t langidFix() const override { return _strings.active() ? QxLangidFallback::LANGID : 0; }
    const QxStringFailure& stringFailure() const override { return _strings.failure(); }

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;

private:
    QxLangidFallback _strings; // issue #91
};

// fabula_command(), nutdrv_qx.c:1427-1541
class QxFabulaTransport : public QxUsbTransport {
public:
    static const uint32_t BUDGET_MS = 3000;

    explicit QxFabulaTransport(IQxUsbPrimitives& usb) : QxUsbTransport(usb) {}

    const char* name() const override { return "fabula"; }
    uint32_t commandBudgetMs() const override { return BUDGET_MS; }

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;
};

// hunnox_command(), nutdrv_qx.c:1546-1711, with the lock-step preamble of hunnox_protocol()
class QxHunnoxTransport : public QxUsbTransport {
public:
    static const uint32_t BUDGET_MS = 4500;
    // langid_fix_local of hunnox_protocol() when langid_fix is not set
    static const uint16_t PREAMBLE_LANGID = 0x0409;

    explicit QxHunnoxTransport(IQxUsbPrimitives& usb) : QxUsbTransport(usb) {}

    const char* name() const override { return "hunnox"; }
    uint32_t commandBudgetMs() const override { return BUDGET_MS; }
    // testvar("novendor") of hunnox_protocol() (US-138)
    void setOptions(const QxOptions& options) override { _novendor = options.novendor; }
    uint16_t langidFix() const override { return _strings.active() ? QxLangidFallback::LANGID : 0; }
    const QxStringFailure& stringFailure() const override { return _strings.failure(); }

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;

private:
    // hunnox_protocol(), nutdrv_qx.c:1240-1291
    void preamble(int askingFor);

    // The static hunnox_step of NUT: it lives as long as the transport (one attach), so a
    // restart of the Q* driver on the same attach (recoverInterface()) does not reset it,
    // as a reconnection in NUT does not reset the static
    int _step = 0;
    bool _novendor = false;
    QxLangidFallback _strings; // issue #91
};

#endif // QX_USB_KRAULER_H
