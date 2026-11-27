/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 */
#ifndef QX_SERIAL_TRANSPORT_H
#define QX_SERIAL_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ISerialPort.h"
#include "QxTransport.h"

/**
 * @brief The serial path of qx_command() (nut_repo/drivers/nutdrv_qx.c:4498-4523) on a USB
 * serial bridge (ADR 0014, section 6).
 *
 * Upstream: ser_flush_io(), ser_send_buf() of the command, then ser_get_buf() with
 * SER_WAIT_SEC (1 s, nutdrv_qx.c:65) on a port in canonical mode whose extra end of line is
 * '\r' (nutdrv_qx.c:4013-4015): one read returns one line, '\r' or '\n' included, or what fits
 * in buflen. Without a complete line within the wait the read returns nothing (0, timeout).
 * The line settings (2400 baud, cablepower) are set at the attach (lineSettings(),
 * upsdrv_initups() of nutdrv_qx.c:4005-4047).
 */
class QxSerialTransport : public QxTransport {
public:
    // SER_WAIT_SEC of nutdrv_qx.c:65
    static const uint32_t READ_WAIT_MS = 1000;
    // Writing a command: at 2400 baud a 20-character command takes 83 ms
    static const uint32_t WRITE_WAIT_MS = 500;
    // The settle time after the control lines, usleep(1100000) of nutdrv_qx.c:4046
    static const uint32_t SETTLE_MS = 1100;

    explicit QxSerialTransport(ISerialPort& port) : _port(port) {}

    // upsdrv_initups() of nutdrv_qx.c:4005-4047 (B2400, cablepower "normal")
    static SerialLineSettings lineSettings() { return SerialLineSettings(); }

    const char* name() const override { return "serial"; }
    uint32_t commandBudgetMs() const override { return WRITE_WAIT_MS + READ_WAIT_MS; }
    const char* lastError() const override { return _error[0] ? _error : _port.lastError(); }
    uint32_t nowMs() const override { return _port.nowMs(); }

    int command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override {
        _error[0] = '\0';
        // ser_flush_io()
        _port.discardInput();
        // ser_send_buf(): ret <= 0 goes back to the caller
        int ret = _port.write(reinterpret_cast<const uint8_t*>(cmd), cmdlen, WRITE_WAIT_MS);
        if (ret <= 0) {
            setError(ret == 0 ? "write timeout" : "write failed");
            return ret;
        }
        // ser_get_buf(upsfd, buf, buflen, SER_WAIT_SEC, 0) on the canonical port
        return readLine(buf, buflen);
    }

private:
    int readLine(char* buf, size_t buflen) {
        if (!buf || buflen == 0) return 0;
        const uint32_t start = _port.nowMs();
        size_t got = 0;
        while (got < buflen) {
            const uint32_t elapsed = _port.nowMs() - start;
            if (elapsed >= READ_WAIT_MS) break;
            // One byte at a time: what follows the end of the line stays for the next flush
            uint8_t c;
            const int n = _port.read(&c, 1, READ_WAIT_MS - elapsed);
            if (n < 0) {
                setError("read failed");
                return n;
            }
            if (n == 0) continue;
            buf[got++] = (char)c;
            if (c == '\r' || c == '\n') return (int)got;
        }
        // A full buffer is what one read() of buflen bytes returns
        if (got == buflen) return (int)got;
        // No complete line within SER_WAIT_SEC: the canonical read returns nothing
        setError("timeout");
        return 0;
    }

    void setError(const char* text) {
        strncpy(_error, text, sizeof(_error) - 1);
        _error[sizeof(_error) - 1] = '\0';
    }

    ISerialPort& _port;
    char _error[24] = "";
};

#endif // QX_SERIAL_TRANSPORT_H
