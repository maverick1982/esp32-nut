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
        checkLock();
        if (pastDeadline()) return fail(QX_USB_ERR_TIMEOUT, "budget");
        if (stringScript.empty() && !stringDevice) {
            return fail(QX_USB_ERR_PIPE, "ESP_ERR_NOT_SUPPORTED");
        }
        // A transfer starts: the host asks at most 255 bytes (hid_host.c)
        stringCalls.push_back(StringCall{index, langid, buflen, clock});
        if (muted) {
            clock += QX_CTRL_TIMEOUT_MS;
            return fail(QX_USB_ERR_TIMEOUT, "ESP_ERR_TIMEOUT");
        }
        std::vector<uint8_t> bytes;
        if (!stringScript.empty()) {
            StringReply r = stringScript.front();
            stringScript.pop_front();
            if (r.ret < 0) {
                clock += r.ret == QX_USB_ERR_TIMEOUT ? QX_CTRL_TIMEOUT_MS : stringTransferMs;
                return fail(r.ret, "scripted");
            }
            bytes = r.bytes;
        } else if (index == 0) {
            bytes = brokenLangidList ? std::vector<uint8_t>{2, 0x03} : langidDescriptor(langidList);
        } else {
            const char* cmd = kraulerCommandOf(index);
            if (!cmd) {
                clock += stringTransferMs;
                return fail(QX_USB_ERR_PIPE, "ESP_ERR_INVALID_RESPONSE");
            }
            char answer[512] = {0};
            int n = device.command(cmd, strlen(cmd), answer, sizeof(answer));
            if (n < 0) n = 0;
            answer[n] = '\0';
            bytes = stringDescriptor(answer);
        }
        clock += stringTransferMs;
        size_t max = buflen < 255 ? buflen : 255;
        size_t n = bytes.size() < max ? bytes.size() : max;
        if (n > 0) memcpy(buf, bytes.data(), n);
        _error = "";
        return (int)n;
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
    void sleepMs(uint32_t ms) override {
        checkLock();
        clock += ms;
        sleeps.push_back(ms);
    }
    uint32_t nowMs() const override { return clock; }
    const char* lastError() const override { return _error; }

    // Commands the device received, as QxTestingTransport recorded them
    const std::vector<std::string>& received() const { return device.sent; }

    // --- String descriptors (krauler family, US-137) ---

    // One GET_DESCRIPTOR(STRING) that started (not the ones refused past the deadline)
    struct StringCall {
        uint8_t index;
        uint16_t langid;
        size_t buflen; // as the transport passed it to the primitive
        uint32_t atMs;
    };
    // A scripted answer: ret < 0 is the error, otherwise bytes are the descriptor
    struct StringReply {
        int ret;
        std::vector<uint8_t> bytes;
    };
    std::vector<StringCall> stringCalls;
    std::deque<StringReply> stringScript; // used first while not empty
    // With the script empty: a krauler-family device. Index 0 is the list with langidList,
    // the other indices are the Megatec commands of kraulerCommandOf(), answered from the
    // testing_t table; an index outside the map STALLs. False: every request STALLs.
    bool stringDevice = false;
    uint16_t langidList = 0x0409;
    // String 0 is a bare header (no langid): usb_get_string_simple() fails on every string,
    // the langid_fix read answers (the VIDA Armour 1200VA of #91, which needs langid_fix in NUT)
    bool brokenLangidList = false;
    uint32_t stringTransferMs = 2; // fake time of one answered GET_DESCRIPTOR
    std::vector<uint32_t> sleeps;

    void script(const std::vector<uint8_t>& bytes) { stringScript.push_back(StringReply{(int)bytes.size(), bytes}); }
    void scriptError(int ret) { stringScript.push_back(StringReply{ret, {}}); }
    // The two reads of usb_get_string_simple(): the langid list, then the answer
    void scriptAnswer(const char* ascii) {
        script(langidDescriptor(langidList));
        script(stringDescriptor(ascii));
    }

    // String descriptor of an ASCII text: bLength, 0x03, UTF-16LE (at most 126 characters)
    static std::vector<uint8_t> stringDescriptor(const char* ascii) {
        size_t n = strlen(ascii);
        if (n > 126) n = 126;
        std::vector<uint8_t> d;
        d.push_back((uint8_t)(2 + 2 * n));
        d.push_back(0x03);
        for (size_t i = 0; i < n; i++) {
            d.push_back((uint8_t)ascii[i]);
            d.push_back(0);
        }
        return d;
    }
    // String descriptor 0 with one langid
    static std::vector<uint8_t> langidDescriptor(uint16_t langid) {
        return std::vector<uint8_t>{4, 0x03, (uint8_t)(langid & 0xFF), (uint8_t)(langid >> 8)};
    }
    // The fake's own map index -> Megatec command (the krauler, fabula and hunnox tables)
    static const char* kraulerCommandOf(uint8_t index) {
        switch (index) {
        case 0x03: return "Q1\r";
        case 0x0d: return "F\r";
        case 0x0c: return "I\r";
        case 0x07: return "Q\r";
        case 0x0a:
        case 0x0b: return "C\r";
        case 0x04: return "T\r";
        case 0x05: return "TL\r";
        case 0xf3: return "BL\r";
        default:   return nullptr;
        }
    }

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
