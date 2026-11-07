#ifndef QX_PROBE_H
#define QX_PROBE_H

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include "QxTypes.h"
#include "QxEngine.h"
#include "QxProtocols.h"
#include "IQxTransport.h"
#include "UPSData.h"

/**
 * Log sink of the probing: level as USBHostUPS::log() ("INFO", "ERROR"), text without
 * trailing newline.
 */
using QxLogFn = void (*)(void* ctx, const char* level, const char* text);

/**
 * @brief Detection of the Q* protocol (ADR 0013, section 4): subdriver_matcher() of
 * nut_repo/drivers/nutdrv_qx.c:4321-4373, split into steps.
 *
 * The protocols of the list are tried in its order (subdriver_list[]), each up to
 * QX_MAXTRIES times with its own claim(), as upstream; with a forced protocol (the protocol
 * of ups.conf) only the matching one is tried. Unlike upstream, step() makes at most one
 * claim attempt (1-2 Q* commands), so the poll task stays responsive, and a cycle without a
 * claim is not fatal: "Device not supported!" is logged once per restart() and a new cycle
 * starts after a back-off (30 s, doubled up to 10 minutes), never in a tight loop.
 *
 * Memory: one QxEngine (~4 KB) is allocated at the first attempt after construction or
 * after takeEngine(), then reset for every other protocol and cycle (QxEngine::reset()).
 *
 * Pure logic: no USB, no clock, no lock. The caller (QxDriver in the poll task, US-136)
 * passes the time and the log sink; it runs step() on its shadow data, so no lock is held
 * across the transfer, and publishes the data under the host lock afterwards.
 */
class QxProber {
public:
    static const uint32_t FIRST_RETRY_MS = 30000; // first new probe after 30 s
    static const uint32_t MAX_RETRY_MS = 600000;  // doubled up to 10 minutes

    enum class Step : uint8_t {
        Probing,      // one claim attempt done and failed, more attempts in this cycle
        Claimed,      // the claim of this step succeeded: engine ready
        NotSupported, // the last attempt of the cycle failed (or none was possible): back-off scheduled
        Waiting,      // back-off running, nothing sent
        Idle          // already claimed earlier, nothing sent
    };

    QxProber(IQxTransport& transport, const QxSubdriver* const* list = QX_SUBDRIVER_LIST,
             QxLogFn log = nullptr, void* logCtx = nullptr);

    /**
     * @brief Sets the forced protocol and the options, then restart().
     * @param protocol nullptr or "" for the automatic detection, otherwise the ups.conf name
     *                 (copied, at most PROTOCOL_MAX - 1 characters)
     */
    void configure(const char* protocol, const QxOptions& options);

    // New attach: first protocol, first try, back-off and "not supported" line reset, engine
    // dropped
    void restart();

    // At most one claim attempt; the claims write into data as upstream (input.voltage,
    // ups.firmware)
    Step step(UPSData& data, uint32_t nowMs);

    // The engine of the claimed protocol: claim() already run, initUps() NOT yet (the caller
    // runs initUps/initInfo as upsdrv_initups()/upsdrv_initinfo() do after the matcher).
    // nullptr before Claimed or once taken.
    std::unique_ptr<QxEngine> takeEngine();

    // --- Diagnostics (US-136 export) and tests ---------------------------------------
    const QxSubdriver* claimed() const { return _claimed; }
    // Protocol of the next attempt; nullptr while claimed or in back-off
    const QxSubdriver* trying() const;
    // Attempts already made on trying(), 0..QX_MAXTRIES-1
    int tryCount() const { return _tries; }
    // Full cycles without a claim since restart()
    uint16_t failedCycles() const { return _failedCycles; }
    // Delay of the running back-off, 0 if none since restart()
    uint32_t retryDelayMs() const { return _retryDelayMs; }
    uint32_t nextProbeAtMs() const { return _nextProbeAtMs; }
    bool backingOff() const { return _backoff; }
    // "" when automatic
    const char* protocol() const { return _protocol; }

    static const size_t PROTOCOL_MAX = 32;

private:
    bool accepted(const QxSubdriver& sub) const;
    // Index of the first accepted protocol from index from, or -1
    int nextAccepted(int from) const;
    Step endCycle(uint32_t nowMs);
    void log(const char* level, const char* text) const;

    IQxTransport& _transport;
    const QxSubdriver* const* _list;
    QxLogFn _log;
    void* _logCtx;
    QxOptions _options;
    char _protocol[PROTOCOL_MAX];

    int _index = -1; // current protocol in _list, -1 when none is accepted
    int _tries = 0;
    std::unique_ptr<QxEngine> _engine;
    const QxSubdriver* _claimed = nullptr;
    uint16_t _failedCycles = 0;
    uint32_t _retryDelayMs = 0;
    uint32_t _nextProbeAtMs = 0;
    bool _backoff = false;
    bool _notSupportedLogged = false;
};

#endif // QX_PROBE_H
