#ifndef EVENT_BACKLOG_H
#define EVENT_BACKLOG_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Events the poll task read from its queue while a Q* command waited for an
 * interrupt IN chunk (US-136, ADR 0013 section 3). They are handled, in their order, as soon
 * as the command returns: the queue is drained from the backlog first.
 *
 * A fixed-size FIFO: no allocation in the poll task.
 */
template <typename Ev, size_t N>
class EventBacklog {
public:
    bool push(const Ev& ev) {
        if (_count == N) return false;
        _items[(_head + _count) % N] = ev;
        _count++;
        return true;
    }
    bool pop(Ev& ev) {
        if (_count == 0) return false;
        ev = _items[_head];
        _head = (_head + 1) % N;
        _count--;
        return true;
    }
    size_t size() const { return _count; }
    bool full() const { return _count == N; }
    void clear() { _head = _count = 0; }

private:
    Ev _items[N];
    size_t _head = 0;
    size_t _count = 0;
};

enum class ChunkRead : uint8_t {
    Chunk,   // a chunk of the bound interface was copied
    Timeout, // nothing within the time
    Gone,    // the bound interface was disconnected (event kept in the backlog)
    Error    // transfer error of the bound interface, or backlog full
};

/**
 * @brief One interrupt IN chunk of the bound interface, taken from the event queue of the
 * poll task (usb_interrupt_read() of NUT on top of the hid_host events).
 *
 * Traits: Event, Handle, isInput(ev), isDisconnect(ev), isTransferError(ev), handle(ev),
 * data(ev), length(ev). Source: bool receive(Event&, uint32_t waitMs), uint32_t now().
 *
 * Rules: an INPUT of the bound interface ends the wait (truncated to buflen); a
 * DISCONNECTED or TRANSFER_ERROR of the bound interface goes to the backlog and ends it; any
 * other event goes to the backlog, except the INPUT of other interfaces, dropped and counted
 * in dropped. With the backlog full nothing more is taken from the queue (Error), so the
 * order is kept.
 */
template <typename Traits, typename Source, size_t N>
ChunkRead readInterruptChunk(Source& src, EventBacklog<typename Traits::Event, N>& backlog,
                             typename Traits::Handle bound, uint8_t* buf, size_t buflen,
                             size_t& got, uint32_t timeoutMs, uint32_t& dropped) {
    got = 0;
    const uint32_t start = src.now();
    typename Traits::Event ev;
    while (true) {
        const uint32_t elapsed = src.now() - start;
        if (elapsed >= timeoutMs && timeoutMs > 0) return ChunkRead::Timeout;
        if (backlog.full()) return ChunkRead::Error;
        const uint32_t wait = timeoutMs > elapsed ? timeoutMs - elapsed : 0;
        if (!src.receive(ev, wait)) {
            if (timeoutMs == 0) return ChunkRead::Timeout;
            continue; // the time is checked at the top
        }
        const bool mine = Traits::handle(ev) == bound;
        if (Traits::isInput(ev)) {
            if (!mine) {
                dropped++;
                continue;
            }
            size_t n = Traits::length(ev);
            if (n > buflen) n = buflen;
            if (n > 0) memcpy(buf, Traits::data(ev), n);
            got = n;
            return ChunkRead::Chunk;
        }
        backlog.push(ev);
        if (mine && Traits::isDisconnect(ev)) return ChunkRead::Gone;
        if (mine && Traits::isTransferError(ev)) return ChunkRead::Error;
    }
}

/**
 * @brief Drops the interrupt IN chunks of the bound interface already queued (received
 * outside a command), with the same rules for the other events. Stops at the first empty
 * receive, or when the backlog is full.
 */
template <typename Traits, typename Source, size_t N>
void discardInputChunks(Source& src, EventBacklog<typename Traits::Event, N>& backlog,
                        typename Traits::Handle bound, uint32_t& dropped) {
    typename Traits::Event ev;
    while (!backlog.full() && src.receive(ev, 0)) {
        if (Traits::isInput(ev)) {
            if (Traits::handle(ev) != bound) dropped++;
            continue;
        }
        backlog.push(ev);
    }
}

#endif // EVENT_BACKLOG_H
