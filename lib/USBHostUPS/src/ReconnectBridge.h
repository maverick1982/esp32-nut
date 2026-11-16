#ifndef RECONNECT_BRIDGE_H
#define RECONNECT_BRIDGE_H

#include <stdint.h>
#include <string.h>

/**
 * @brief Who a UPS is, from the descriptors read at enumeration (US-140).
 *
 * VID, PID and the serial and product strings, as the host reads them before it claims the
 * interface. A serial that is missing, empty or "Blank" counts as no serial, as
 * populateStringsFromDeviceInfo() does.
 */
struct UpsIdentity {
    static const size_t STRING_LEN = 64; // HID_STR_DESC_MAX_LENGTH

    bool valid = false;
    uint16_t vid = 0;
    uint16_t pid = 0;
    char serial[STRING_LEN + 1] = {0};
    char product[STRING_LEN + 1] = {0};

    static UpsIdentity make(uint16_t vid, uint16_t pid, const char* serial, const char* product) {
        UpsIdentity id;
        id.valid = true;
        id.vid = vid;
        id.pid = pid;
        if (serial && strcmp(serial, "Blank") != 0) copy(id.serial, serial);
        if (product) copy(id.product, product);
        return id;
    }

private:
    static void copy(char (&out)[STRING_LEN + 1], const char* in) {
        size_t i = 0;
        for (; i < STRING_LEN && in[i]; i++) out[i] = in[i];
        out[i] = '\0';
    }
};

/**
 * @brief Keep serving the last values while the same UPS drops off the bus and comes back
 *        (US-140, issue #36).
 *
 * Pure logic, so it runs in the native unit tests. A Powercom SPD-750U drops off the USB bus
 * by itself every 1-3 hours and is back about a second later (dmesg on Linux shows the same).
 * Each time the host threw its values away and served DATA-STALE until the first full poll,
 * about 18 s on that UPS: Home Assistant showed it "unavailable".
 *
 * usbhid-ups does not (usbhid-ups.c:1392-1424): when the link drops it calls
 * dstate_datastale() only if the reconnection fails, and while it walks the device again
 * (HU_WALKMODE_INIT) upsd keeps serving the values it has, replaced one by one. It
 * reconnects only to the same device. Here, the owner:
 * - onDisconnected(): starts the bridge when the data was fresh. The UPS must enumerate
 *   again within ENUM_WINDOW_MS (NUT tries again after about 2 s; the board enumerates in
 *   1-1.5 s). A new drop-off during the bridge goes back to that wait without moving the
 *   start: the bridge never lasts more than MAX_BRIDGE_MS (FIRST_DATA_MAX_MS of the host);
 * - onEnumerated() and onClaim(): the same UPS keeps the values (KEEP_VALUES) and the new
 *   driver writes over them; another one, or a bridge already over, drops them;
 * - onFirstData(): the first full poll ended, every value is current again;
 * - tick(): the bridge ran out. DROP_VALUES while the UPS is not back; once it is back the
 *   values stay and the data is stale until the first full poll (STALE_UNTIL_FIRST_DATA).
 * serving() depends only on the time, so the data turns stale right at the deadline.
 */
class ReconnectBridge {
public:
    static const uint32_t ENUM_WINDOW_MS = 5000;
    static const uint32_t MAX_BRIDGE_MS = 30000;

    enum class State : uint8_t { IDLE, WAITING_ENUM, WAITING_CLAIM, WAITING_DATA };
    enum class Claim : uint8_t { NOT_BRIDGING, KEEP_VALUES, DROP_VALUES };
    enum class Expiry : uint8_t { NONE, DROP_VALUES, STALE_UNTIL_FIRST_DATA };

    /**
     * @brief Same VID/PID and the same serial; without serials, the same product. A serial on
     *        one side only means another device.
     */
    static bool sameUps(const UpsIdentity& a, const UpsIdentity& b) {
        if (!a.valid || !b.valid || a.vid != b.vid || a.pid != b.pid) return false;
        if (a.serial[0] || b.serial[0]) return strcmp(a.serial, b.serial) == 0;
        return strcmp(a.product, b.product) == 0;
    }

    /**
     * @brief The UPS served dropped off the bus.
     * @param dataFresh the data was not stale (first full poll done)
     * @param bound who it was
     * @return true: keep the values, the bridge runs
     */
    bool onDisconnected(uint32_t now, bool dataFresh, const UpsIdentity& bound) {
        if (_state != State::IDLE) {
            // Gone again during the bridge: wait for it again, within the same limit
            const uint32_t elapsed = elapsedMs(now);
            const uint32_t left = elapsed < MAX_BRIDGE_MS ? MAX_BRIDGE_MS - elapsed : 0;
            _enumWindow = elapsed + (left < ENUM_WINDOW_MS ? left : ENUM_WINDOW_MS);
            _state = State::WAITING_ENUM;
            return true;
        }
        if (!dataFresh || !bound.valid) return false;
        _state = State::WAITING_ENUM;
        _startedAt = now;
        _enumWindow = ENUM_WINDOW_MS;
        _id = bound;
        return true;
    }

    // A device enumerated (HID CONNECTED event)
    void onEnumerated(uint32_t now) {
        if (_state == State::WAITING_ENUM && serving(now)) _state = State::WAITING_CLAIM;
    }

    // An interface was claimed: keep the values for the same UPS only
    Claim onClaim(uint32_t now, const UpsIdentity& id) {
        if (_state == State::IDLE) return Claim::NOT_BRIDGING;
        if (!serving(now) || !sameUps(_id, id)) {
            reset();
            return Claim::DROP_VALUES;
        }
        _state = State::WAITING_DATA;
        return Claim::KEEP_VALUES;
    }

    /**
     * @brief The first full poll after the claim ended.
     * @param bridgedMs time since the drop-off, when it returns true
     * @return true when it closes a bridge
     */
    bool onFirstData(uint32_t now, uint32_t& bridgedMs) {
        if (_state != State::WAITING_DATA) return false;
        bridgedMs = elapsedMs(now);
        reset();
        return true;
    }

    // Once per service() pass: the end of a bridge that ran out, reported once
    Expiry tick(uint32_t now) {
        if (_state == State::IDLE || serving(now)) return Expiry::NONE;
        const bool claimed = _state == State::WAITING_DATA;
        reset();
        return claimed ? Expiry::STALE_UNTIL_FIRST_DATA : Expiry::DROP_VALUES;
    }

    // The values may be served as current
    bool serving(uint32_t now) const {
        switch (_state) {
        case State::WAITING_ENUM:
            return elapsedMs(now) < _enumWindow;
        case State::WAITING_CLAIM:
        case State::WAITING_DATA:
            return elapsedMs(now) < MAX_BRIDGE_MS;
        default:
            return false;
        }
    }

    bool active() const { return _state != State::IDLE; }
    State state() const { return _state; }
    uint32_t elapsedMs(uint32_t now) const { return _state == State::IDLE ? 0 : now - _startedAt; }

    void reset() {
        _state = State::IDLE;
        _startedAt = 0;
        _enumWindow = 0;
        _id = UpsIdentity();
    }

private:
    State _state = State::IDLE;
    uint32_t _startedAt = 0;
    uint32_t _enumWindow = 0; // WAITING_ENUM ends this long after _startedAt
    UpsIdentity _id;
};

#endif // RECONNECT_BRIDGE_H
