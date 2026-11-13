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

#include "QxProbe.h"
#include <stdio.h>
#include <string.h>

QxProber::QxProber(IQxTransport& transport, const QxSubdriver* const* list, QxLogFn log, void* logCtx)
    : _transport(transport), _list(list), _log(log), _logCtx(logCtx) {
    _protocol[0] = '\0';
    restart();
}

void QxProber::configure(const char* protocol, const QxOptions& options) {
    snprintf(_protocol, sizeof(_protocol), "%s", protocol ? protocol : "");
    _options = options;
    restart();
}

void QxProber::restart() {
    // The engine, if any, is kept: step() resets it on the first protocol it tries
    _claimed = nullptr;
    _index = nextAccepted(0);
    _tries = 0;
    _failedCycles = 0;
    _retryDelayMs = 0;
    _nextProbeAtMs = 0;
    _backoff = false;
    _notSupportedLogged = false;
}

// "If protocol is set in ups.conf, use it" (nutdrv_qx.c:4329-4345)
bool QxProber::accepted(const QxSubdriver& sub) const {
    return _protocol[0] == '\0' || qxProtocolMatches(sub, _protocol);
}

int QxProber::nextAccepted(int from) const {
    if (!_list) return -1;
    for (int i = from; _list[i] != nullptr; i++) {
        if (accepted(*_list[i])) return i;
    }
    return -1;
}

const QxSubdriver* QxProber::trying() const {
    if (_claimed || _backoff || _index < 0) return nullptr;
    return _list[_index];
}

void QxProber::log(const char* level, const char* text) const {
    if (_log) _log(_logCtx, level, text);
}

QxProber::Step QxProber::step(UPSData& data, uint32_t nowMs) {
    if (_claimed) return Step::Idle;

    if (_backoff) {
        if ((int32_t)(nowMs - _nextProbeAtMs) < 0) return Step::Waiting;
        // Back-off over: a new cycle from the first protocol
        _backoff = false;
        _index = nextAccepted(0);
        _tries = 0;
    }

    // No protocol of the list is accepted (forced protocol not ported)
    if (_index < 0) return endCycle(nowMs);

    const QxSubdriver* sub = _list[_index];
    // A fresh engine state for each protocol, kept for its QX_MAXTRIES attempts as the
    // global table of the upstream subdriver. One engine is allocated per attach and reset
    // for the next protocols, so a probing cycle does not allocate and free ~4 KB per
    // protocol on the heap of the board.
    if (_tries == 0) {
        if (_engine) _engine->reset(*sub, _options);
        else _engine.reset(new QxEngine(*sub, _transport, _options));
    }

    // "Give every subdriver some tries" (nutdrv_qx.c:4347-4362), one per step
    if (_engine->claim(data)) {
        _claimed = sub;
        char line[96];
        snprintf(line, sizeof(line), "[Q*] Using protocol: %s", sub->name);
        log("INFO", line);
        return Step::Claimed;
    }

    if (++_tries < QX_MAXTRIES) return Step::Probing;

    // Tries over: next protocol (the engine is reset on its first attempt)
    _tries = 0;
    _index = nextAccepted(_index + 1);
    if (_index >= 0) return Step::Probing;
    return endCycle(nowMs);
}

// upslogx(LOG_ERR, "Device not supported!") (nutdrv_qx.c:4366), here followed by a back-off
// instead of fatalx()
QxProber::Step QxProber::endCycle(uint32_t nowMs) {
    // The engine stays allocated through the back-off: the next cycle reuses it
    _tries = 0;
    if (_failedCycles < UINT16_MAX) _failedCycles++;
    if (_retryDelayMs == 0) {
        _retryDelayMs = FIRST_RETRY_MS;
    } else {
        _retryDelayMs = _retryDelayMs >= MAX_RETRY_MS / 2 ? MAX_RETRY_MS : _retryDelayMs * 2;
    }
    _nextProbeAtMs = nowMs + _retryDelayMs;
    _backoff = true;

    if (!_notSupportedLogged) {
        _notSupportedLogged = true;
        char line[128];
        if (_protocol[0] == '\0') {
            snprintf(line, sizeof(line),
                     "[Q*] Device not supported! No protocol claimed the UPS, next probe in %u s",
                     (unsigned)(_retryDelayMs / 1000));
        } else {
            snprintf(line, sizeof(line),
                     "[Q*] Device not supported! Protocol %s did not claim the UPS, next probe in %u s",
                     _protocol, (unsigned)(_retryDelayMs / 1000));
        }
        log("ERROR", line);
    }
    return Step::NotSupported;
}

std::unique_ptr<QxEngine> QxProber::takeEngine() {
    if (!_claimed) return nullptr;
    return std::move(_engine);
}
