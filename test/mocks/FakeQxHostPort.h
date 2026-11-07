#ifndef FAKE_QX_HOST_PORT_H
#define FAKE_QX_HOST_PORT_H

#include <string>
#include <vector>
#include "IQxHostPort.h"

// The host as the Q* driver sees it, for the native tests (US-136): a lock that raises a
// flag (checked by FakeQxUsbPrimitives) and the System Log lines
class FakeQxHostPort : public IQxHostPort {
public:
    mutable bool locked = false;
    mutable int depth = 0;
    mutable std::vector<std::string> lines; // "LEVEL text"
    bool paused = false;                    // link back-off

    void lock() const override {
        depth++;
        locked = true;
    }
    void unlock() const override {
        if (--depth == 0) locked = false;
    }
    void log(const char* level, const char* text) const override {
        lines.push_back(std::string(level) + " " + text);
    }

    bool pollingPaused() const override { return paused; }

    int count(const char* fragment) const {
        int n = 0;
        for (const auto& l : lines) {
            if (l.find(fragment) != std::string::npos) n++;
        }
        return n;
    }
};

#endif // FAKE_QX_HOST_PORT_H
