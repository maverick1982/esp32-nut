#ifndef FAKE_SERIAL_PORT_H
#define FAKE_SERIAL_PORT_H

#include <deque>
#include <map>
#include <string>
#include <vector>
#include "ISerialPort.h"

// ISerialPort for the native tests (ADR 0014): simulated time, a receive queue of bytes that
// arrive at given times, the written bytes recorded, and optional answers per command.
class FakeSerialPort : public ISerialPort {
public:
    struct Arrival {
        uint32_t at;
        uint8_t byte;
    };

    uint32_t now = 1000;
    std::deque<Arrival> rx;           // bytes that arrive at their time
    std::string written;              // every byte written
    std::vector<std::string> writes;  // one entry per write()
    std::map<std::string, std::string> answers;  // command -> answer, queued after the write
    uint32_t answerDelayMs = 50;
    int writeResult = 0;  // 0: write everything; otherwise returned as is
    bool readFails = false;
    bool configured = false;
    SerialLineSettings settings;
    size_t discards = 0;
    size_t discardedBytes = 0;
    uint32_t sleptMs = 0;

    // Bytes available at t (relative to now)
    void arrive(const std::string& bytes, uint32_t afterMs = 0) {
        for (char c : bytes) rx.push_back({ now + afterMs, (uint8_t)c });
    }

    bool configure(const SerialLineSettings& s) override {
        settings = s;
        configured = true;
        return true;
    }

    int write(const uint8_t* data, size_t len, uint32_t) override {
        if (writeResult != 0) return writeResult;
        std::string cmd(reinterpret_cast<const char*>(data), len);
        written += cmd;
        writes.push_back(cmd);
        auto it = answers.find(cmd);
        if (it != answers.end()) arrive(it->second, answerDelayMs);
        return (int)len;
    }

    int read(uint8_t* buf, size_t len, uint32_t timeoutMs) override {
        if (readFails) return -1;
        const uint32_t deadline = now + timeoutMs;
        if (rx.empty() || rx.front().at > deadline) {
            now = deadline;  // nothing within the wait
            return 0;
        }
        if (rx.front().at > now) now = rx.front().at;
        size_t n = 0;
        while (n < len && !rx.empty() && rx.front().at <= now) {
            buf[n++] = rx.front().byte;
            rx.pop_front();
        }
        return (int)n;
    }

    size_t discardInput() override {
        discards++;
        size_t n = 0;
        while (!rx.empty() && rx.front().at <= now) {
            rx.pop_front();
            n++;
        }
        discardedBytes += n;
        return n;
    }

    void sleepMs(uint32_t ms) override {
        now += ms;
        sleptMs += ms;
    }
    uint32_t nowMs() const override { return now; }
    const char* lastError() const override { return readFails ? "device gone" : ""; }
};

#endif // FAKE_SERIAL_PORT_H
