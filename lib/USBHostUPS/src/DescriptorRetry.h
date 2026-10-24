#ifndef DESCRIPTOR_RETRY_H
#define DESCRIPTOR_RETRY_H

#include <stdint.h>

/**
 * @brief Retries of the report descriptor read when an interface connects (issue #97).
 *
 * Pure logic, so it runs in the native unit tests. Right after a reset of its USB port
 * a UPS may not answer GET_DESCRIPTOR yet. Without a descriptor the interface used to be
 * dropped as "non-UPS", and only a replug brought the UPS back.
 *
 * - MAX_ATTEMPTS reads in all, the one of the CONNECTED event included, RETRY_DELAY_MS apart.
 * - One interface at a time: a UPS has a single HID interface.
 * - The host clears it when the read succeeds or the interface goes away.
 */
class DescriptorRetry {
public:
    static const uint8_t MAX_ATTEMPTS = 5;
    static const uint32_t RETRY_DELAY_MS = 2000;

    DescriptorRetry() : _pending(false), _attempts(0), _retryAt(0) {}

    /**
     * @brief A read failed.
     * @return true when another read is due at retryAt(), false when the attempts are over
     */
    bool onFailure(uint32_t now) {
        if (++_attempts >= MAX_ATTEMPTS) {
            clear();
            return false;
        }
        _pending = true;
        _retryAt = now + RETRY_DELAY_MS;
        return true;
    }

    bool pending() const { return _pending; }
    bool due(uint32_t now) const { return _pending && (int32_t)(now - _retryAt) >= 0; }
    uint8_t attempts() const { return _attempts; }

    void clear() {
        _pending = false;
        _attempts = 0;
        _retryAt = 0;
    }

private:
    bool _pending;
    uint8_t _attempts;
    uint32_t _retryAt;
};

#endif // DESCRIPTOR_RETRY_H
