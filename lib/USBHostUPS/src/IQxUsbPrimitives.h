#ifndef I_QX_USB_PRIMITIVES_H
#define I_QX_USB_PRIMITIVES_H

#include <stddef.h>
#include <stdint.h>

// Errors of the Q* USB primitives: negative, with the values of libusb, so the ported
// *_command() functions keep the upstream checks (ret <= 0)
static const int QX_USB_ERR_IO = -1;
static const int QX_USB_ERR_NO_DEVICE = -4;
static const int QX_USB_ERR_TIMEOUT = -7;
static const int QX_USB_ERR_PIPE = -9;

/**
 * @brief The USB requests a Q* transport needs (ADR 0013, section 3; US-136), on the
 * interface the host has bound.
 *
 * The host implements them in its poll task with no application lock held (ADR 0008); the
 * native tests use a fake (test/mocks/FakeQxUsbPrimitives.h). Interrupt OUT (fuji) and
 * vendor control requests (snr) are added when a tester needs them.
 */
class IQxUsbPrimitives {
public:
    virtual ~IQxUsbPrimitives() = default;

    // SET_REPORT(reportType, reportId) of len bytes: bytes sent, or an error < 0
    virtual int setReport(uint8_t reportType, uint8_t reportId, const uint8_t* data, size_t len) = 0;

    /**
     * @brief GET_DESCRIPTOR(STRING, index, langid): the raw descriptor (bLength, 0x03,
     * UTF-16LE), at most 255 bytes, into buf.
     *
     * No memory of the failed indices, unlike IHidTransport::requestStringDescriptor()
     * (review M5): for the krauler family the index is the command.
     * @return Bytes received, or an error < 0
     */
    virtual int getStringDescriptor(uint8_t index, uint16_t langid, uint8_t* buf, size_t buflen) = 0;

    // One interrupt IN chunk of the bound interface (usb_interrupt_read()): its length, or
    // QX_USB_ERR_TIMEOUT after timeoutMs, or another error < 0
    virtual int readInterrupt(uint8_t* buf, size_t buflen, uint32_t timeoutMs) = 0;

    // Drops the interrupt IN chunks received outside a command; the other events stay
    virtual void discardInput() = 0;

    /**
     * @brief Budget of the running command (QxUsbTransport::command()).
     *
     * After deadlineMs no new transfer starts (QX_USB_ERR_TIMEOUT without traffic) and the
     * interrupt waits are cut to it. 0 = no budget.
     */
    virtual void setDeadline(uint32_t deadlineMs) = 0;

    virtual uint32_t nowMs() const = 0;

    // Text of the last error for the diagnostics export ("ESP_ERR_TIMEOUT", "budget", ...)
    virtual const char* lastError() const = 0;
};

#endif // I_QX_USB_PRIMITIVES_H
