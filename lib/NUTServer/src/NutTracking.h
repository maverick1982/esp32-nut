#ifndef NUT_TRACKING_H
#define NUT_TRACKING_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "NutTable.h"

/**
 * Results of the SET VAR requests sent with TRACKING ON (US-067), as tracking_add() /
 * tracking_get() of upsd (nut_repo/server/upsd.c), in fixed memory.
 *
 * upsd keeps a linked list that tracking_cleanup() trims by age. Here a full table drops
 * its oldest entry instead, so the memory never grows. The table is shared by every
 * client, as in upsd: the TRACKING flag is per session (NUTServer), the ids are not.
 *
 * Header-only: the native test env compiles only the .cpp files listed in platformio.ini.
 */
class NutTracking {
public:
    // Source of random bits for the ids: esp_random() on the device, injectable in tests
    typedef uint32_t (*RandomFn)();

    static const size_t CAPACITY = 16;
    // tracking_delay of upsd: 3600 s
    static const uint32_t MAX_AGE_MS = 3600000UL;
    // UUID4_LEN of include/common.h: 36 characters and the terminator
    static const size_t ID_LEN = 37;

    struct Entry {
        char id[ID_LEN];
        NutTable::SetStatus status;
        uint32_t created_ms;
        bool used;
    };

    explicit NutTracking(RandomFn rnd) : _rnd(rnd) { clear(); }

    void setRandom(RandomFn rnd) { _rnd = rnd; }

    void clear() {
        for (size_t i = 0; i < CAPACITY; i++) {
            _entries[i].id[0] = '\0';
            _entries[i].status = NutTable::SetStatus::UNKNOWN;
            _entries[i].created_ms = 0;
            _entries[i].used = false;
        }
    }

    // nut_uuid_v4() (upsd.c): 16 random bytes, version 4 and variant 10xx, uppercase hex
    // in the 8-4-4-4-12 layout
    void newId(char out[ID_LEN]) const {
        uint8_t b[16];
        for (size_t i = 0; i < 4; i++) {
            uint32_t r = _rnd ? _rnd() : 0;
            b[i * 4 + 0] = (uint8_t)(r >> 24);
            b[i * 4 + 1] = (uint8_t)(r >> 16);
            b[i * 4 + 2] = (uint8_t)(r >> 8);
            b[i * 4 + 3] = (uint8_t)r;
        }
        b[6] = (uint8_t)((b[6] & 0x0F) | 0x40);
        b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
        snprintf(out, ID_LEN,
                 "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                 b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    }

    // tracking_add(), with the outcome already known: a free entry, else the oldest one
    void add(const char* id, NutTable::SetStatus st, uint32_t now) {
        if (!id) return;
        size_t slot = 0;
        bool found = false;
        for (size_t i = 0; i < CAPACITY; i++) {
            if (!_entries[i].used) {
                slot = i;
                found = true;
                break;
            }
        }
        if (!found) {
            // Unsigned differences stay right across the wrap of millis()
            uint32_t oldest = 0;
            for (size_t i = 0; i < CAPACITY; i++) {
                uint32_t age = now - _entries[i].created_ms;
                if (age >= oldest) {
                    oldest = age;
                    slot = i;
                }
            }
        }
        Entry& e = _entries[slot];
        strncpy(e.id, id, ID_LEN - 1);
        e.id[ID_LEN - 1] = '\0';
        e.status = st;
        e.created_ms = now;
        e.used = true;
    }

    // tracking_cleanup(), run by the server loop as by the main loop of upsd: an entry
    // older than tracking_delay is dropped before its age can wrap around (49.7 days)
    void cleanup(uint32_t now) {
        for (size_t i = 0; i < CAPACITY; i++) {
            if (_entries[i].used && now - _entries[i].created_ms > MAX_AGE_MS) {
                _entries[i].used = false;
            }
        }
    }

    // tracking_get(): ids compared with strcasecmp; unknown or expired is ERR UNKNOWN.
    // PENDING never shows up, since the outcome is known before the reply.
    const char* get(const char* id, uint32_t now) const {
        if (!id) return "ERR UNKNOWN";
        for (size_t i = 0; i < CAPACITY; i++) {
            const Entry& e = _entries[i];
            if (!e.used || !NutTable::equalsIgnoreCase(e.id, id)) continue;
            if (now - e.created_ms > MAX_AGE_MS) return "ERR UNKNOWN";
            return statusText(e.status);
        }
        return "ERR UNKNOWN";
    }

    // STAT_HANDLED, STAT_CONVERSION_FAILED, STAT_FAILED and STAT_UNKNOWN of tracking_get()
    static const char* statusText(NutTable::SetStatus st) {
        switch (st) {
        case NutTable::SetStatus::OK:                return "SUCCESS";
        case NutTable::SetStatus::CONVERSION_FAILED: return "ERR INVALID-ARGUMENT";
        case NutTable::SetStatus::FAILED:            return "ERR FAILED";
        case NutTable::SetStatus::NOT_CONNECTED:     return "ERR FAILED";
        case NutTable::SetStatus::UNKNOWN:           return "ERR UNKNOWN";
        }
        return "ERR UNKNOWN";
    }

private:
    RandomFn _rnd;
    Entry _entries[CAPACITY];
};

#endif // NUT_TRACKING_H
