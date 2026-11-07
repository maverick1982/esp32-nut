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
 * nut_repo/drivers/nutdrv_qx.c:1240-1711, without the langid_fix, noscanlangid and novendor
 * options (ADR 0013: added when a tester needs them). Without langid_fix the three read the
 * answer with usb_get_string_simple(), ported as qxGetStringAscii().
 */

/**
 * @brief usb_get_string_simple() of NUT (nut_repo/drivers/usb-common.h:352-371), that is
 * libusb_get_string_descriptor_ascii(): the langid list (string 0), then the string with the
 * first langid, converted to ASCII ('?' for the other characters), at most buflen - 1
 * characters and a NUL.
 * @return The characters copied, or an error < 0 (QX_USB_ERR_IO for a malformed descriptor,
 *         QX_USB_ERR_INVALID_PARAM for index 0, without traffic)
 */
int qxGetStringAscii(IQxUsbPrimitives& usb, uint8_t index, char* buf, size_t buflen);

// krauler_command(), nutdrv_qx.c:1294-1424
class QxKraulerTransport : public QxUsbTransport {
public:
    static const uint32_t BUDGET_MS = 3000;

    explicit QxKraulerTransport(IQxUsbPrimitives& usb) : QxUsbTransport(usb) {}

    const char* name() const override { return "krauler"; }
    uint32_t commandBudgetMs() const override { return BUDGET_MS; }

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;
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

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;

private:
    // hunnox_protocol(), nutdrv_qx.c:1240-1291
    void preamble(int askingFor);

    // The static hunnox_step of NUT: it lives as long as the transport (one attach), so a
    // restart of the Q* driver on the same attach (recoverInterface()) does not reset it,
    // as a reconnection in NUT does not reset the static
    int _step = 0;
};

#endif // QX_USB_KRAULER_H
