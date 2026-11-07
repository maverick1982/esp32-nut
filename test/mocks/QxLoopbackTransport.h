#ifndef QX_LOOPBACK_TRANSPORT_H
#define QX_LOOPBACK_TRANSPORT_H

#include <string.h>
#include "QxUsbTransport.h"

/**
 * @brief A Q* USB transport for the native tests only (US-136), not a port of NUT: the
 * command goes out in 8-byte SET_REPORT blocks, the answer comes back in interrupt IN
 * chunks up to '\r', the shape of the cypress family. It exercises the primitives, their
 * deadline and the interrupt reads of the host.
 */
class QxLoopbackTransport : public QxUsbTransport {
public:
    static const uint32_t BUDGET_MS = 2000;

    explicit QxLoopbackTransport(IQxUsbPrimitives& usb) : QxUsbTransport(usb) {}

    const char* name() const override { return "loopback"; }
    uint32_t commandBudgetMs() const override { return BUDGET_MS; }
    bool needsInterruptIn() const override { return true; }

protected:
    int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override {
        if (cmdlen == 0) cmdlen = strlen(cmd);
        _usb.discardInput();
        for (size_t i = 0; i < cmdlen; i += 8) {
            uint8_t block[8] = {0};
            size_t n = cmdlen - i < 8 ? cmdlen - i : 8;
            memcpy(block, cmd + i, n);
            int ret = _usb.setReport(0x02, 0, block, sizeof(block));
            if (ret < 0) return ret;
        }
        memset(buf, 0, buflen);
        size_t len = 0;
        while (len < buflen - 1) {
            uint8_t tmp[8];
            int ret = _usb.readInterrupt(tmp, sizeof(tmp), 1000);
            // Test choice: a command the fake UPS does not answer (the "" answers of the
            // testing_t tables) ends with an empty answer, as the TESTING branch of NUT
            if (ret == QX_USB_ERR_TIMEOUT && len == 0) return 0;
            if (ret <= 0) return ret;
            size_t n = (size_t)ret;
            if (n > buflen - 1 - len) n = buflen - 1 - len;
            memcpy(buf + len, tmp, n);
            len += n;
            if (memchr(tmp, '\r', (size_t)ret)) break;
        }
        return (int)len;
    }
};

#endif // QX_LOOPBACK_TRANSPORT_H
