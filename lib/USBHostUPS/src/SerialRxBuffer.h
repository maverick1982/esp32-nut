#ifndef SERIAL_RX_BUFFER_H
#define SERIAL_RX_BUFFER_H

#include <atomic>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief The bytes a USB serial bridge received, between the data callback of the CDC-ACM
 * driver (its task) and the reader in the poll task (ADR 0014, section 4).
 *
 * Single producer, single consumer, lock free: the callback never blocks and never waits for
 * the poll task (ADR 0008). A full buffer drops the new bytes and counts them; a Q* answer is
 * at most a few hundred bytes, so a drop means nobody read for long.
 */
template <size_t N>
class SerialRxBuffer {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");

public:
    // Producer (callback): bytes stored; the rest is dropped and counted
    size_t push(const uint8_t* data, size_t len) {
        const size_t head = _head.load(std::memory_order_relaxed);
        const size_t tail = _tail.load(std::memory_order_acquire);
        const size_t room = N - (head - tail);
        const size_t n = len < room ? len : room;
        for (size_t i = 0; i < n; i++) _buf[(head + i) & (N - 1)] = data[i];
        _head.store(head + n, std::memory_order_release);
        if (n < len) _dropped.fetch_add((uint32_t)(len - n), std::memory_order_relaxed);
        return n;
    }

    // Consumer (poll task): bytes copied into out, at most len
    size_t pop(uint8_t* out, size_t len) {
        const size_t tail = _tail.load(std::memory_order_relaxed);
        const size_t head = _head.load(std::memory_order_acquire);
        const size_t avail = head - tail;
        const size_t n = len < avail ? len : avail;
        for (size_t i = 0; i < n; i++) out[i] = _buf[(tail + i) & (N - 1)];
        _tail.store(tail + n, std::memory_order_release);
        return n;
    }

    // Consumer: drops everything stored; returns the bytes dropped
    size_t clear() {
        const size_t tail = _tail.load(std::memory_order_relaxed);
        const size_t head = _head.load(std::memory_order_acquire);
        _tail.store(head, std::memory_order_release);
        return head - tail;
    }

    size_t size() const {
        return _head.load(std::memory_order_acquire) - _tail.load(std::memory_order_acquire);
    }
    static constexpr size_t capacity() { return N; }
    uint32_t dropped() const { return _dropped.load(std::memory_order_relaxed); }

private:
    uint8_t _buf[N] = {0};
    std::atomic<size_t> _head{0};
    std::atomic<size_t> _tail{0};
    std::atomic<uint32_t> _dropped{0};
};

#endif // SERIAL_RX_BUFFER_H
