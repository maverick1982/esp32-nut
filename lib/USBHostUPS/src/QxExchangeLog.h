#ifndef QX_EXCHANGE_LOG_H
#define QX_EXCHANGE_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// One Q* command and its answer, as the transport returned it (US-136, diagnostics export)
struct QxExchange {
    static const size_t COMMAND_MAX = 32;
    static const size_t ANSWER_MAX = 128;

    char command[COMMAND_MAX + 1];
    uint8_t answer[ANSWER_MAX];
    uint8_t answerLen;    // bytes kept in answer
    bool truncated;       // the answer was longer than ANSWER_MAX
    int32_t result;       // return value of the transport: length, or an error < 0
    uint32_t atMs;        // uptime when the command started
    uint32_t durationMs;
    char error[24];       // text of the primitive error when result < 0
};

/**
 * @brief The last CAPACITY Q* exchanges of the bound UPS, for the diagnostics export: a
 * fixed ring, no allocation in the poll task. at(0) is the oldest.
 */
class QxExchangeLog {
public:
    static const size_t CAPACITY = 20;

    void add(const char* command, size_t commandLen, const uint8_t* answer, int result,
             uint32_t atMs, uint32_t durationMs, const char* error) {
        QxExchange& e = _items[(_head + _count) % CAPACITY];
        if (_count == CAPACITY) {
            _head = (_head + 1) % CAPACITY; // the oldest goes
        } else {
            _count++;
        }
        size_t n = commandLen < QxExchange::COMMAND_MAX ? commandLen : QxExchange::COMMAND_MAX;
        if (command && n > 0) memcpy(e.command, command, n);
        else n = 0;
        e.command[n] = '\0';
        e.answerLen = 0;
        e.truncated = false;
        if (answer && result > 0) {
            size_t len = (size_t)result;
            e.truncated = len > QxExchange::ANSWER_MAX;
            if (e.truncated) len = QxExchange::ANSWER_MAX;
            memcpy(e.answer, answer, len);
            e.answerLen = (uint8_t)len;
        }
        e.result = result;
        e.atMs = atMs;
        e.durationMs = durationMs;
        snprintfError(e.error, sizeof(e.error), result < 0 && error ? error : "");
    }

    size_t size() const { return _count; }
    const QxExchange& at(size_t i) const { return _items[(_head + i) % CAPACITY]; }
    void clear() { _head = _count = 0; }

private:
    static void snprintfError(char* out, size_t outLen, const char* text) {
        size_t n = strlen(text);
        if (n >= outLen) n = outLen - 1;
        memcpy(out, text, n);
        out[n] = '\0';
    }

    QxExchange _items[CAPACITY];
    size_t _head = 0;
    size_t _count = 0;
};

#endif // QX_EXCHANGE_LOG_H
