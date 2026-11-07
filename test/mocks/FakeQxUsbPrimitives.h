#ifndef FAKE_QX_USB_PRIMITIVES_H
#define FAKE_QX_USB_PRIMITIVES_H

#include <string.h>
#include <deque>
#include <string>
#include <vector>
#include "IQxUsbPrimitives.h"
#include "QxUsbTransport.h"
#include "QxTestingTransport.h"

/**
 * @brief The host's Q* USB primitives for the native tests (US-136), with a fake clock and a
 * fake device behind them.
 *
 * The device collects the SET_REPORT blocks up to '\r', answers the command from a testing_t
 * table (QxTestingTransport) and queues the answer as 8-byte interrupt IN chunks. Muted, every
 * transfer takes its whole timeout on the clock. The deadline of the running command is
 * honoured as by the host: no transfer starts past it. Any call made while *lockFlag is true
 * is counted in callsUnderLock (no application lock during a transfer, ADR 0008).
 */
class FakeQxUsbPrimitives : public IQxUsbPrimitives {
public:
    explicit FakeQxUsbPrimitives(const QxTesting* table, const char* rejected = nullptr)
        : device(table, rejected) {}

    QxTestingTransport device;     // answers of the fake UPS (also records the commands)
    uint32_t clock = 1000;         // ms
    bool muted = false;            // the UPS does not answer
    const bool* lockFlag = nullptr;
    int callsUnderLock = 0;
    int setReports = 0;
    int reads = 0;
    int discards = 0;

    int setReport(uint8_t reportType, uint8_t reportId, const uint8_t* data, size_t len) override {
        (void)reportType;
        (void)reportId;
        checkLock();
        if (pastDeadline()) return fail(QX_USB_ERR_TIMEOUT, "budget");
        setReports++;
        if (muted) {
            clock += QX_CTRL_TIMEOUT_MS;
            return fail(QX_USB_ERR_TIMEOUT, "ESP_ERR_TIMEOUT");
        }
        clock += 2;
        for (size_t i = 0; i < len; i++) {
            if (data[i] == 0) continue; // padding of the 8-byte block
            _pending.push_back((char)data[i]);
            if (data[i] == '\r') answer();
        }
        return (int)len;
    }

    int getStringDescriptor(uint8_t index, uint16_t langid, uint8_t* buf, size_t buflen) override {
        (void)index;
        (void)langid;
        (void)buf;
        (void)buflen;
        checkLock();
        return fail(QX_USB_ERR_PIPE, "ESP_ERR_NOT_SUPPORTED");
    }

    int readInterrupt(uint8_t* buf, size_t buflen, uint32_t timeoutMs) override {
        checkLock();
        if (pastDeadline()) return fail(QX_USB_ERR_TIMEOUT, "budget");
        reads++;
        if (_chunks.empty()) {
            uint32_t wait = timeoutMs;
            if (_deadline && _deadline - clock < wait) wait = _deadline - clock;
            clock += wait;
            return fail(QX_USB_ERR_TIMEOUT, "ESP_ERR_TIMEOUT");
        }
        clock += 8;
        std::string c = _chunks.front();
        _chunks.pop_front();
        size_t n = c.size() < buflen ? c.size() : buflen;
        memcpy(buf, c.data(), n);
        return (int)n;
    }

    void discardInput() override {
        checkLock();
        discards++;
        _chunks.clear();
    }

    void setDeadline(uint32_t deadlineMs) override { _deadline = deadlineMs; }
    uint32_t nowMs() const override { return clock; }
    const char* lastError() const override { return _error; }

    // Commands the device received, as QxTestingTransport recorded them
    const std::vector<std::string>& received() const { return device.sent; }

private:
    void checkLock() {
        if (lockFlag && *lockFlag) callsUnderLock++;
    }
    bool pastDeadline() const { return _deadline != 0 && (int32_t)(clock - _deadline) >= 0; }
    int fail(int code, const char* text) {
        _error = text;
        return code;
    }
    void answer() {
        char buf[512] = {0};
        int n = device.command(_pending.c_str(), _pending.size(), buf, sizeof(buf));
        _pending.clear();
        for (int i = 0; i < n; i += 8) {
            _chunks.push_back(std::string(buf + i, (size_t)(n - i < 8 ? n - i : 8)));
        }
    }

    std::string _pending;
    std::deque<std::string> _chunks;
    uint32_t _deadline = 0;
    const char* _error = "";
};

#endif // FAKE_QX_USB_PRIMITIVES_H
