#ifndef FIRST_DATA_H
#define FIRST_DATA_H

#include <stdint.h>

/**
 * @brief When the host starts serving the data after a claim (issue #60; US-136).
 *
 * Ready once the driver ended its first full poll and ups.status is known. When the wait is
 * bounded (the HID drivers), also after maxMs: the data are then served anyway and timedOut
 * is set, for the WARN line. The Q* driver waits without limit (IUPSDriver::
 * firstDataWaitBounded()): until the protocol is recognised and the first walk is done.
 */
inline bool firstDataReady(bool initialPollDone, bool statusKnown, bool bounded,
                           uint32_t elapsedMs, uint32_t maxMs, bool& timedOut) {
    timedOut = false;
    if (initialPollDone && statusKnown) return true;
    if (bounded && elapsedMs >= maxMs) {
        timedOut = true;
        return true;
    }
    return false;
}

#endif // FIRST_DATA_H
