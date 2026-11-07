#ifndef QX_TESTING_TRANSPORT_H
#define QX_TESTING_TRANSPORT_H

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "IQxTransport.h"

// testing_t of nut_repo/drivers/nutdrv_qx.h: a command and its answer; answer_len -1 treats
// the answer as a NUL terminated string, otherwise inner '\0's are kept
struct QxTesting {
    const char* cmd;
    const char* answer;
    int answer_len;
};

/**
 * Fake Q* transport: the TESTING branch of qx_command() (nut_repo/drivers/nutdrv_qx.c,
 * the loop on subdriver->testing): the command is looked up case-insensitively in the
 * testing table; when missing, the answer is the rejected string of the protocol, or else
 * the echo of the command. For the tests it also records the commands it receives, can
 * fail the next calls and can replace the answer of a command.
 */
class QxTestingTransport : public IQxTransport {
public:
    QxTestingTransport(const QxTesting* table, const char* rejected = nullptr)
        : _table(table), _rejected(rejected) {}

    std::vector<std::string> sent;

    // The next n commands return -1 (still recorded in sent)
    void failNext(int n) { _fail = n; }

    // Another fake UPS from now on (US-137: a protocol change after a recovery)
    void setTable(const QxTesting* table, const char* rejected) {
        _table = table;
        _rejected = rejected;
    }

    // Replaces (or adds) the answer of cmd; answer nullptr removes the replacement
    void override(const char* cmd, const char* answer) {
        for (size_t i = 0; i < _overrides.size(); i++) {
            if (equalsIgnoreCase(_overrides[i].first.c_str(), cmd)) {
                if (answer) _overrides[i].second = answer;
                else _overrides.erase(_overrides.begin() + i);
                return;
            }
        }
        if (answer) _overrides.push_back(std::make_pair(std::string(cmd), std::string(answer)));
    }

    int command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override {
        (void)cmdlen;
        sent.push_back(cmd);
        if (_fail > 0) {
            _fail--;
            return -1;
        }
        for (const auto& o : _overrides) {
            if (equalsIgnoreCase(o.first.c_str(), cmd)) return snprintf(buf, buflen, "%s", o.second.c_str());
        }
        for (const QxTesting* t = _table; t && t->cmd; t++) {
            if (!equalsIgnoreCase(cmd, t->cmd)) continue;
            // Preserve inner '\0's when asked to (nutdrv_qx.c, TESTING branch)
            if (t->answer_len > 0 && strlen(t->answer) < (size_t)t->answer_len) {
                size_t len = buflen <= (size_t)t->answer_len ? buflen - 1 : (size_t)t->answer_len;
                memcpy(buf, t->answer, len);
                return (int)len;
            }
            return snprintf(buf, buflen, "%s", t->answer);
        }
        if (_rejected) return snprintf(buf, buflen, "%s", _rejected);
        return snprintf(buf, buflen, "%s", cmd);
    }

private:
    static bool equalsIgnoreCase(const char* a, const char* b) {
        for (; *a && *b; a++, b++) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
            if (ca != cb) return false;
        }
        return *a == *b;
    }

    const QxTesting* _table;
    const char* _rejected;
    int _fail = 0;
    std::vector<std::pair<std::string, std::string>> _overrides;
};

#endif // QX_TESTING_TRANSPORT_H
