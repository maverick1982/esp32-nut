#ifndef I_QX_TRANSPORT_H
#define I_QX_TRANSPORT_H

#include <stddef.h>

/**
 * @brief One Q* command, one answer (ADR 0013, section 3): the shape of qx_command() and
 * of the USB subdriver *_command() functions of nut_repo/drivers/nutdrv_qx.c.
 *
 * The USB implementations derive from QxUsbTransport (US-136), which gives every command a
 * time budget enforced by the USB primitives; the subdrivers themselves fill the
 * QxUsbTransports table (krauler, fabula and hunnox in QxUsbKrauler, US-137). The native
 * tests use fakes
 * (test/mocks/QxTestingTransport.h, test/mocks/QxLoopbackTransport.h).
 */
class IQxTransport {
public:
    virtual ~IQxTransport() = default;

    /**
     * @brief Sends cmd and reads the answer into buf.
     *
     * @param cmd The command, usually a NUL terminated string such as "Q1\r"
     * @param cmdlen Its length in bytes (as returned by the command preprocess, 0 or the
     *               snprintf length)
     * @param buf Buffer for the answer, zeroed by the caller
     * @param buflen Its size
     * @return -1 on errors, otherwise the number of bytes read
     */
    virtual int command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) = 0;
};

#endif // I_QX_TRANSPORT_H
