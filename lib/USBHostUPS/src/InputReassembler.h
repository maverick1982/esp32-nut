#ifndef INPUT_REASSEMBLER_H
#define INPUT_REASSEMBLER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Rebuilds INPUT reports longer than one packet (review A4).
 *
 * Pure logic, so it runs in the native unit tests. The IN transfer is one packet
 * (wMaxPacketSize) long, so a longer report arrives as several INPUT_REPORT events and
 * the continuation packets used to be decoded as reports of their own, their first byte
 * taken for a report ID. On low-speed devices (MPS 8) a 9-byte report is enough.
 *
 * Sizing the transfer on the longest report does not work: an interrupt IN transfer
 * ends only on a short packet or when the buffer is full, so a shorter report whose
 * length is a multiple of MPS would wait for the next one and be glued to it.
 *
 * A full packet (== MPS) that starts a report longer than MPS opens an assembly; the
 * next packets are appended until the expected length or a short packet. A gap longer
 * than maxGapMs drops the partial report.
 */
class InputReassembler {
public:
    static const size_t MAX_REPORT = 256;

    explicit InputReassembler(uint32_t maxGapMs = 500) : _maxGapMs(maxGapMs) { reset(); }

    void reset() {
        _len = 0;
        _expected = 0;
        _ts = 0;
    }

    bool pending() const { return _len > 0; }
    uint32_t dropped() const { return _dropped; }

    /**
     * @param pkt, len  one packet, as delivered by the HID host driver
     * @param ts        time the packet was received
     * @param mps       wMaxPacketSize of the IN endpoint (0 = unknown, no assembly)
     * @param expected  total length of the report this packet starts, report ID included
     *                  (0 = unknown), as if the packet started one. Ignored while an
     *                  assembly is pending.
     * @param out, out_len  complete report, valid until the next call
     * @return true when a complete report is available
     */
    bool feed(const uint8_t* pkt, size_t len, uint32_t ts, size_t mps, size_t expected,
              const uint8_t*& out, size_t& out_len) {
        if (_len > 0) {
            if ((ts - _ts) > _maxGapMs) {
                _dropped++; // the rest never came: this packet starts a new report
                reset();
            } else {
                size_t room = _expected - _len;
                size_t n = len < room ? len : room;
                memcpy(_buf + _len, pkt, n);
                _len += n;
                _ts = ts;
                if (_len >= _expected || len < mps) {
                    out = _buf;
                    out_len = _len;
                    _len = 0;
                    return true;
                }
                return false;
            }
        }

        if (mps > 0 && len == mps && expected > len && expected <= MAX_REPORT) {
            memcpy(_buf, pkt, len);
            _len = len;
            _expected = expected;
            _ts = ts;
            return false;
        }
        out = pkt;
        out_len = len;
        return true;
    }

private:
    uint32_t _maxGapMs;
    uint8_t _buf[MAX_REPORT];
    size_t _len;
    size_t _expected;
    uint32_t _ts;
    uint32_t _dropped = 0;
};

#endif // INPUT_REASSEMBLER_H
