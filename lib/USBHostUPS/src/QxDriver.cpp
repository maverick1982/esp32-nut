#include "QxDriver.h"
#include <stdio.h>
#include <string.h>
#include "QxControl.h"

// The host side of nutdrv_qx (US-136): upsdrv_initups()/upsdrv_initinfo()/upsdrv_updateinfo()
// of nut_repo/drivers/nutdrv_qx.c run one Q* command per poll step, on a shadow of the data.

namespace {

// Holds the host lock for a scope
class HostLock {
public:
    explicit HostLock(const IQxHostPort& h) : _h(h) { _h.lock(); }
    ~HostLock() { _h.unlock(); }
private:
    const IQxHostPort& _h;
};

} // namespace

QxDriver::QxDriver(std::unique_ptr<QxUsbTransport> transport, IQxHostPort& host, uint16_t vid, uint16_t pid,
                   const QxSubdriver* const* list)
    : _transport(std::move(transport)), _host(host), _vid(vid), _pid(pid), _recorder(*this),
      _prober(_recorder, list, &QxDriver::logThunk, this) {}

QxDriver::~QxDriver() = default;

void QxDriver::logThunk(void* ctx, const char* level, const char* text) {
    static_cast<QxDriver*>(ctx)->_host.log(level, text);
}

const IUpsControl& QxDriver::control() const {
    return QxControl::instance();
}

int QxDriver::Recorder::command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) {
    // No lock during the command (ADR 0008): the exchange is recorded afterwards
    const uint32_t start = _d._transport->nowMs();
    int ret = _d._transport->command(cmd, cmdlen, buf, buflen);
    const uint32_t duration = _d._transport->nowMs() - start;
    size_t len = cmdlen > 0 ? cmdlen : (cmd ? strlen(cmd) : 0);
    HostLock lock(_d._host);
    _d._exchanges.add(cmd, len, reinterpret_cast<const uint8_t*>(buf), ret, start, duration,
                      _d._transport->lastError());
    return ret;
}

void QxDriver::setup() {
    _prober.restart();
    _engine.reset();
    _state = State::Probing;
    _firstWalkDone = false;
    _walkStarted = false;
    {
        HostLock lock(_host);
        _commands.clear();
    }
    // driver.name as the HID drivers publish it (GenericDriver), ups.vendorid/productid as
    // upsdrv_initups() (nutdrv_qx.c:3920-3921)
    char id[8];
    _shadow.set("driver.name", getDriverName());
    snprintf(id, sizeof(id), "%04x", (unsigned)_vid);
    _shadow.set("ups.vendorid", id);
    snprintf(id, sizeof(id), "%04x", (unsigned)_pid);
    _shadow.set("ups.productid", id);
}

void QxDriver::poll(UPSData& data, uint32_t now) {
    if (_host.pollingPaused()) return;
    switch (_state) {
    case State::Probing:
        if (_prober.step(_shadow, now) == QxProber::Step::Claimed) {
            _engine = _prober.takeEngine();
            // End of upsdrv_initups() after the matcher
            if (_engine) {
                _engine->initUps();
                _state = State::Init;
            }
        }
        break;

    case State::Init: {
        QxEngine::Phase p = _engine->initInfoStep(_shadow, now);
        if (p == QxEngine::Phase::Ok) {
            std::vector<const UPSCommandInfo*> cmds = QxControl::commandsOf(*_engine);
            {
                HostLock lock(_host);
                _commands = std::move(cmds);
            }
            _state = State::Polling;
            _walkStarted = false;
        } else if (p == QxEngine::Phase::Failed) {
            // fatalx(EXIT_FAILURE, "Can't initialise data") of upsdrv_initinfo(): here the
            // protocol is detected again after a pause
            char line[96];
            snprintf(line, sizeof(line), "[Q*] Can't initialise data, new probe in %u s",
                     (unsigned)(INIT_RETRY_MS / 1000));
            _host.log("ERROR", line);
            _engine.reset();
            _state = State::Held;
            _heldUntil = now + INIT_RETRY_MS;
        }
        break;
    }

    case State::Polling:
        stepPolling(now);
        break;

    case State::Held:
        if ((int32_t)(now - _heldUntil) >= 0) {
            _prober.restart();
            _state = State::Probing;
        }
        break;
    }

    publish(data);
}

void QxDriver::stepPolling(uint32_t now) {
    if (!_engine->walkPending()) {
        // upsdrv_updateinfo() every poll interval (main.c of NUT), from the start of the
        // previous walk; the first one right after the init
        if (_walkStarted && (uint32_t)(now - _walkStartedAt) < QxEngine::DEFAULT_POLLINTERVAL_S * 1000UL) return;
        _walkStarted = true;
        _walkStartedAt = now;
    }

    switch (_engine->updateInfoStep(_shadow, now)) {
    case QxEngine::Update::Pending:
        break;
    case QxEngine::Update::Ok:
        _firstWalkDone = true;
        if (_engine->reestablished()) {
            _host.log("INFO", "[Q*] Communications with the UPS re-established");
        }
        _stale = false;
        break;
    case QxEngine::Update::Failed:
        break;
    case QxEngine::Update::Stale:
        // dstate_datastale(); upstream logs the loss at debug level only
        if (!_stale) _host.log("WARN", "[Q*] Communications with the UPS lost: status read failed, data stale");
        _stale = true;
        break;
    }
}

void QxDriver::publish(UPSData& data) {
    if (_shadow.writes() == _publishedWrites) return;
    HostLock lock(_host);
    for (const auto& p : _shadow.getAll()) {
        if (!data.valueEquals(p.key.c_str(), p.value)) data.set(p.key, p.value);
    }
    // Keys the shadow no longer has (delinfo() of the engine)
    std::vector<String> gone;
    for (const auto& p : data.getAll()) {
        if (!_shadow.find(p.key.c_str())) gone.push_back(p.key);
    }
    for (const auto& k : gone) data.remove(k);
    _publishedWrites = _shadow.writes();
}

CommandResult QxDriver::runCommand(UPSData& data, const char* name, const char* param) {
    if (_state != State::Polling || !_engine) return CommandResult::NOT_SUPPORTED;
    _engine->abortWalk();
    CommandResult r = QxControl::runCommand(*_engine, _shadow, name, param);
    publish(data);
    return r;
}

NutTable::SetStatus QxDriver::runSetVariable(UPSData& data, const char* name, const char* value) {
    if (_state != State::Polling || !_engine) return NutTable::SetStatus::UNKNOWN;
    _engine->abortWalk();
    NutTable::SetStatus st = QxControl::runSetVariable(*_engine, _shadow, name, value);
    publish(data);
    return st;
}

void QxDriver::fillDiagnostics(UsbDiagQx& out) const {
    out.present = true;
    out.transport = _transport->name();
    const QxSubdriver* claimed = _prober.claimed();
    out.protocol = claimed ? claimed->name : "";
    // Held: protocol recognised but "Can't initialise data", waiting for the next attempt
    if (_state == State::Held) out.probe = "waiting";
    else if (!claimed && _prober.backingOff()) out.probe = "not_supported";
    else if (claimed) out.probe = "claimed";
    else out.probe = "probing";
    out.tries = (uint8_t)_prober.tryCount();
    out.failed_cycles = _prober.failedCycles();
    out.retry_ms = _prober.retryDelayMs();
    out.forced_protocol = _prober.protocol();
    out.exchanges.clear();
    out.exchanges.reserve(_exchanges.size());
    for (size_t i = 0; i < _exchanges.size(); i++) {
        const QxExchange& e = _exchanges.at(i);
        UsbDiagQxExchange x;
        x.command = e.command;
        x.answer.assign(e.answer, e.answer + e.answerLen);
        x.length = e.result;
        x.uptime_ms = e.atMs;
        x.duration_ms = e.durationMs;
        x.truncated = e.truncated;
        x.error = e.error;
        out.exchanges.push_back(std::move(x));
    }
}
