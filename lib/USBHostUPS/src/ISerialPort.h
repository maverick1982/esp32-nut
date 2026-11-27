#ifndef I_SERIAL_PORT_H
#define I_SERIAL_PORT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Line settings of a serial port, as termios gives them to a NUT serial driver.
 */
struct SerialLineSettings {
    uint32_t baud = 2400;
    uint8_t data_bits = 8;
    char parity = 'N';      // 'N', 'E', 'O'
    uint8_t stop_bits = 1;
    bool dtr = true;        // cablepower "normal" of nutdrv_qx: DTR on, RTS off
    bool rts = false;
};

/**
 * @brief The serial port of a USB serial bridge, as the protocol layer sees it (ADR 0014,
 * section 4): the only interface above usb-transport. The bridge chip specifics (CDC-ACM
 * requests, XR21V141x registers) live in its adapter behind it.
 *
 * Poll task only, with no application lock held (ADR 0008). Every wait is bounded.
 */
class ISerialPort {
public:
    virtual ~ISerialPort() = default;
    // Line settings and control lines; false (with lastError()) when the bridge refused them
    virtual bool configure(const SerialLineSettings& settings) = 0;
    // Bytes written, or < 0 on errors (nothing written within timeoutMs)
    virtual int write(const uint8_t* data, size_t len, uint32_t timeoutMs) = 0;
    // Bytes read (at least one) as soon as some arrived, 0 when none within timeoutMs, < 0 on
    // errors (device gone)
    virtual int read(uint8_t* buf, size_t len, uint32_t timeoutMs) = 0;
    // Drops what was received and not read yet (ser_flush_io()); returns the bytes dropped
    virtual size_t discardInput() = 0;
    virtual void sleepMs(uint32_t ms) = 0;
    virtual uint32_t nowMs() const = 0;
    // Text of the last error for the diagnostics export
    virtual const char* lastError() const = 0;
};

#endif // I_SERIAL_PORT_H
