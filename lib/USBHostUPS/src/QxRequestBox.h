#ifndef QX_REQUEST_BOX_H
#define QX_REQUEST_BOX_H

#include <stdint.h>
#include "UpsCommands.h"
#include "UpsVariables.h"

// A command or SET VAR of another task for the Q* driver, served by the poll task (US-136)
struct QxRequest {
    bool pending = false;
    bool setvar = false;
    char name[48] = {0};
    char value[64] = {0};
    uint32_t gen = 0;
    CommandResult cmd = CommandResult::FAILED;
    NutTable::SetStatus set = NutTable::SetStatus::FAILED;
};

/**
 * @brief The locks and semaphores of the Q* request box, as the host has them: the data lock
 * (recursive), the mutex serialising the callers and the binary semaphore the poll task gives
 * when it answered. The waits feed the caller's Task WDT (takeFeedingWdt() of the host).
 */
class IQxRequestSync {
public:
    virtual ~IQxRequestSync() = default;
    virtual void lock() = 0;
    virtual void unlock() = 0;
    // The two semaphores exist (begin() created them)
    virtual bool ready() const = 0;
    virtual bool takeCaller(uint32_t ms) = 0;
    virtual void giveCaller() = 0;
    // Waits for signalDone(), at most ms: true when given
    virtual bool waitDone(uint32_t ms) = 0;
    virtual void signalDone() = 0;
    // Takes a give left over, without waiting
    virtual void drainDone() = 0;
    // The caller is the poll task
    virtual bool inPollTask() const = 0;
};

/**
 * @brief The Q* driver as the request box sees it. The run*() calls happen in the poll task
 * without the data lock (ADR 0008): only the poll task deletes the driver.
 */
class IQxRequestTarget {
public:
    virtual ~IQxRequestTarget() = default;
    // A Q* driver is attached
    virtual bool hasDriver() const = 0;
    virtual bool readyToPoll() const = 0;
    // Longest wait of a caller for one request, with a driver attached
    virtual uint32_t waitMs() const = 0;
    // param nullptr = no value
    virtual CommandResult runCommand(const char* name, const char* param) = 0;
    virtual NutTable::SetStatus runSetVariable(const char* name, const char* value) = 0;
};

/**
 * @brief The Q* request box of the host (US-136, extracted with US-137 for the native tests):
 * another task posts a command or a SET VAR and waits; the poll task serves it between two
 * steps of the driver.
 *
 * Every request has a generation: a caller that gives up (wait over) abandons its request, and
 * the poll task drops the result of a request no longer pending or of another generation.
 * The state is guarded by the data lock of IQxRequestSync, the callers are serialised by its
 * caller mutex.
 */
class QxRequestBox {
public:
    explicit QxRequestBox(IQxRequestSync& sync) : _sync(sync) {}

    /**
     * @brief Posts req and waits for the poll task. Without a driver ready, NOT_CONNECTED at
     * once; from the poll task itself, run at once with no wait.
     * @return false when the wait timed out (abandoned) or the box is not usable
     */
    bool post(QxRequest& req, IQxRequestTarget& target) {
        uint32_t waitMs;
        {
            Lock l(_sync);
            if (!target.hasDriver() || !target.readyToPoll()) {
                req.cmd = CommandResult::NOT_CONNECTED;
                req.set = NutTable::SetStatus::NOT_CONNECTED;
                return true;
            }
            // The step running when the request arrives, then the request itself
            waitMs = target.waitMs();
        }
        if (_sync.inPollTask()) {
            // From the poll task itself (no caller does it today): run at once, no wait.
            // Outside the lock (ADR 0008): only this task deletes the driver
            run(req, target);
            return true;
        }
        if (!_sync.ready()) return false;
        if (!_sync.takeCaller(waitMs)) return false;
        _sync.drainDone(); // a give left by an abandoned request
        uint32_t gen;
        {
            Lock l(_sync);
            gen = ++_gen;
            _req = req;
            _req.gen = gen;
            _req.pending = true;
        }
        _sync.waitDone(waitMs);
        bool answered;
        {
            Lock l(_sync);
            answered = _req.gen == gen && !_req.pending;
            if (answered) {
                req.cmd = _req.cmd;
                req.set = _req.set;
            } else {
                abandon(gen); // the poll task drops its result
            }
        }
        _sync.drainDone();
        _sync.giveCaller();
        return answered;
    }

    /**
     * @brief Serves the pending request, in the poll task. Without a driver it answers
     * NOT_CONNECTED, with canRun false FAILED, both without any transfer.
     * @return true when it ran on the driver (the poll step is skipped)
     */
    bool service(bool canRun, IQxRequestTarget& target) {
        QxRequest req;
        bool driver;
        {
            Lock l(_sync);
            if (!_req.pending) return false;
            req = _req;
            driver = target.hasDriver();
        }
        if (!driver) {
            answer(CommandResult::NOT_CONNECTED, NutTable::SetStatus::NOT_CONNECTED, req.gen);
            return false;
        }
        if (!canRun) {
            answer(CommandResult::FAILED, NutTable::SetStatus::FAILED, req.gen);
            return false;
        }
        req.cmd = CommandResult::FAILED;
        req.set = NutTable::SetStatus::FAILED;
        run(req, target);
        Lock l(_sync);
        if (_req.pending && _req.gen == req.gen) {
            _req.cmd = req.cmd;
            _req.set = req.set;
            _req.pending = false;
            _sync.signalDone();
        }
        return true;
    }

    /**
     * @brief Answers the pending request with a result (no driver, link not usable). gen != 0
     * answers only that request: one posted after the caller abandoned it keeps waiting for its
     * turn.
     */
    void answer(CommandResult cmd, NutTable::SetStatus set, uint32_t gen = 0) {
        Lock l(_sync);
        if (!_req.pending || (gen != 0 && _req.gen != gen)) return;
        _req.cmd = cmd;
        _req.set = set;
        _req.pending = false;
        _sync.signalDone();
    }

    // The caller of request gen gave up: what post() does when its wait is over (public so
    // that the tests can interleave it with a run of the poll task)
    void abandon(uint32_t gen) {
        Lock l(_sync);
        if (_req.gen == gen) _req.pending = false;
    }

    // The request in the box, for the tests
    const QxRequest& current() const { return _req; }

private:
    class Lock {
    public:
        explicit Lock(IQxRequestSync& s) : _s(s) { _s.lock(); }
        ~Lock() { _s.unlock(); }
    private:
        IQxRequestSync& _s;
    };

    static void run(QxRequest& req, IQxRequestTarget& target) {
        if (req.setvar) req.set = target.runSetVariable(req.name, req.value);
        else req.cmd = target.runCommand(req.name, req.value[0] ? req.value : nullptr);
    }

    IQxRequestSync& _sync;
    QxRequest _req;
    uint32_t _gen = 0;
};

#endif // QX_REQUEST_BOX_H
