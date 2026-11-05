#ifndef DIAG_SWEEP_H
#define DIAG_SWEEP_H

#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <set>
#include <vector>
#include "HIDUsages.h"
#include "DiagReadStatus.h"

/**
 * @brief State of the on-demand read behind the USB diagnostics export (US-119, issue #123).
 *
 * The export used to carry only the reports the poll had cached. When the user asks for an
 * export, the poll task reads once every FEATURE report of the descriptor that is not cached
 * and that the driver accepts, one request per step, so the export also shows the reports no
 * row reads (and keeps doing so when the poll reads only what NUT reads, US-120).
 *
 * No platform code and no USB call: USBHostUPS runs the transfers and records the results
 * here. Only the poll task changes the state; the other tasks read it under the host mutex.
 */
class DiagSweep {
public:
    using State = DiagReadState;
    // Copy of the state and counters, for the readers of the other tasks
    using Status = DiagReadStatus;
    enum class Result : uint8_t { Read, Empty, Failed };

    // A report to request
    struct Item {
        uint8_t type; // 3 = FEATURE
        uint8_t id;
    };

    // Result of a report that answered: Read with its bytes, or Empty (0 bytes). A failed
    // request is only counted.
    struct Entry {
        uint8_t type;
        uint8_t id;
        bool empty;
        std::vector<uint8_t> data;
    };

    // False for the reports a driver must never request (IUPSDriver::acceptDiagReport)
    using Accept = std::function<bool(uint8_t report_type, uint8_t report_id)>;

    static constexpr uint8_t FEATURE = 3;

    static uint16_t key(uint8_t type, uint8_t id) { return (uint16_t)((type << 8) | id); }

    /**
     * @brief Starts a new read and forgets the results of the previous one.
     *
     * The list holds the FEATURE reports of the descriptor in order of first appearance,
     * without duplicates and with report ID 0 for the devices without IDs, less the ones
     * whose key (type << 8) | id is in cached_keys and the ones accept() refuses. INPUT and
     * OUTPUT reports are not read. An empty list ends the read at once (Done).
     */
    void start(const std::vector<HIDUsageDef>& usages, const std::set<uint16_t>& cached_keys,
               const Accept& accept) {
        clear();
        for (const auto& u : usages) {
            if (u.report_type != FEATURE) continue;
            if (cached_keys.count(key(u.report_type, u.report_id))) continue;
            bool found = false;
            for (const auto& it : _items) {
                if (it.id == u.report_id) { found = true; break; }
            }
            if (found) continue;
            if (accept && !accept(u.report_type, u.report_id)) continue;
            _items.push_back(Item{u.report_type, u.report_id});
        }
        _state = _items.empty() ? State::Done : State::Reading;
    }

    // The next report to request; false when the read is not running
    bool next(Item& out) const {
        if (_state != State::Reading || _pos >= _items.size()) return false;
        out = _items[_pos];
        return true;
    }

    /**
     * @brief Result of the report next() gave. Ignored when the read is not running or id is
     * not that report. The read ends (Done) after the last one.
     */
    void record(uint8_t id, Result r, const uint8_t* data, size_t len) {
        if (_state != State::Reading || _pos >= _items.size() || _items[_pos].id != id) return;
        const Item& it = _items[_pos];
        if (r == Result::Read && (data == nullptr || len == 0)) r = Result::Empty;
        switch (r) {
        case Result::Read:
            _read++;
            _results.push_back(Entry{it.type, it.id, false, std::vector<uint8_t>(data, data + len)});
            break;
        case Result::Empty:
            _empty++;
            _results.push_back(Entry{it.type, it.id, true, {}});
            break;
        case Result::Failed:
            _failed++;
            break;
        }
        _pos++;
        if (_pos >= _items.size()) _state = State::Done;
    }

    // The link no longer allows requests: the read stops, the results so far stay
    void interrupt() {
        if (_state == State::Reading) _state = State::Interrupted;
    }

    // New device or disconnection
    void clear() {
        _items.clear();
        _results.clear();
        _pos = 0;
        _read = _empty = _failed = 0;
        _state = State::Idle;
    }

    State state() const { return _state; }
    uint16_t total() const { return (uint16_t)_items.size(); }
    uint16_t done() const { return (uint16_t)_pos; }
    uint16_t read() const { return _read; }
    uint16_t empty() const { return _empty; }
    uint16_t failed() const { return _failed; }
    Status status() const {
        Status s;
        s.state = _state;
        s.total = total();
        s.done = done();
        s.read = _read;
        s.empty = _empty;
        s.failed = _failed;
        return s;
    }
    // Reports that answered, in request order
    const std::vector<Entry>& results() const { return _results; }

private:
    std::vector<Item> _items;
    std::vector<Entry> _results;
    size_t _pos = 0;
    uint16_t _read = 0;
    uint16_t _empty = 0;
    uint16_t _failed = 0;
    State _state = State::Idle;
};

#endif // DIAG_SWEEP_H
