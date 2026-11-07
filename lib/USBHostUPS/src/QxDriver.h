#ifndef QX_DRIVER_H
#define QX_DRIVER_H

#include <Arduino.h>
#include <stdint.h>
#include <memory>
#include <vector>
#include "IUPSDriver.h"
#include "IQxHostPort.h"
#include "QxUsbTransport.h"
#include "QxExchangeLog.h"
#include "QxProbe.h"
#include "QxProtocols.h"
#include "QxEngine.h"
#include "UpsCommands.h"
#include "UpsVariables.h"
#include "UsbDiagSnapshot.h"
#include "UPSData.h"

/**
 * @brief The Q* driver (ADR 0013, sections 2-5; US-136): what the host creates after the
 * attach of a Q* UPS whose USB transport is ported.
 *
 * It implements the protocol-neutral core of IUPSDriver (hid() == nullptr) on top of one
 * QxUsbTransport, with the protocol detection of QxProber, the walk of QxEngine and the
 * commands of QxControl. Every poll() is one step and sends at most one Q* command (two for
 * a claim), so the poll task waits at most qxStepBoundMs() of the transport.
 *
 * No lock is held during a command (ADR 0008): engine and prober write into a shadow
 * UPSData of the driver, and at the end of a step that changed it the driver takes the
 * host lock and makes the host data equal to it. On the Q* path the host writes no other key.
 *
 * Threads: setup(), poll(), runCommand(), runSetVariable() run in the poll task;
 * commands() and fillDiagnostics() in any task, with the host lock held by the caller.
 */
class QxDriver : public IUPSDriver {
public:
    QxDriver(std::unique_ptr<QxUsbTransport> transport, IQxHostPort& host, uint16_t vid, uint16_t pid,
             const QxSubdriver* const* list = QX_SUBDRIVER_LIST);
    ~QxDriver() override;

    // --- IUPSDriver ---------------------------------------------------------------------
    const char* getDriverName() const override { return "nutdrv_qx"; }
    // Attach and every restart of the recovery ladder: the protocol is detected again. The
    // shadow data are kept (the readers keep the last values, as with the HID drivers)
    void setup() override;
    void poll(UPSData& data, uint32_t now) override;
    // The protocol is recognised, initInfo done and the first walk ended (US-136 criterion)
    bool initialPollDone() const override { return _firstWalkDone; }
    bool dataStale() const override { return _stale; }
    bool firstDataWaitBounded() const override { return false; }
    const IUpsControl& control() const override;
    QxDriver* qx() override { return this; }
    const QxDriver* qx() const override { return this; }

    // --- Q* extension -------------------------------------------------------------------
    // The protocol and novendor settings of Board Settings (US-138, protocol/novendor of
    // ups.conf): protocol nullptr or "" for the automatic detection. Before setup() or the
    // first poll() it only stores them; afterwards the detection starts again with them and
    // the values of the previous protocol are dropped. Poll task (or before setup()).
    void configure(const char* protocol, const QxOptions& options);
    // Name of the claimed protocol ("Megatec 0.10"), "" before a claim. Host lock held
    const char* claimedProtocol() const;
    // Poll task: a command / SET VAR of QxControl on the engine, between two steps (a walk
    // in progress starts again). NOT_SUPPORTED / UNKNOWN before the first walk.
    CommandResult runCommand(UPSData& data, const char* name, const char* param);
    NutTable::SetStatus runSetVariable(UPSData& data, const char* name, const char* value);
    // Host lock held: the commands of the recognised protocol (empty before the init)
    std::vector<const UPSCommandInfo*> commands() const { return _commands; }
    // Host lock held: the "qx" object of the export
    void fillDiagnostics(UsbDiagQx& out) const;

    const char* transportName() const { return _transport->name(); }
    bool needsInterruptIn() const { return _transport->needsInterruptIn(); }
    uint32_t transportBudgetMs() const { return _transport->commandBudgetMs(); }

    // Pause after an initInfo() that failed (fatalx() upstream) before probing again
    static const uint32_t INIT_RETRY_MS = 30000;

private:
    enum class State : uint8_t { Probing, Init, Polling, Held };

    // IQxTransport given to prober and engine: records every exchange
    class Recorder : public IQxTransport {
    public:
        explicit Recorder(QxDriver& d) : _d(d) {}
        int command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override;
    private:
        QxDriver& _d;
    };

    static void logThunk(void* ctx, const char* level, const char* text);
    // Makes data equal to the shadow, under the host lock, when the shadow changed
    void publish(UPSData& data);
    void stepPolling(uint32_t now);
    // A protocol other than the previous one was claimed (US-137): drops the shadow values
    // except driver.name, ups.vendorid and ups.productid; the first walk fills them again
    void dropProtocolValues(const char* from, const char* to);
    // Removes from the shadow every key except driver.name, ups.vendorid and ups.productid
    void dropValuesExceptIdentity();

    std::unique_ptr<QxUsbTransport> _transport;
    IQxHostPort& _host;
    uint16_t _vid;
    uint16_t _pid;
    Recorder _recorder;
    QxProber _prober;
    std::unique_ptr<QxEngine> _engine;
    UPSData _shadow;
    uint32_t _publishedWrites = UINT32_MAX; // _shadow.writes() at the last publish
    QxExchangeLog _exchanges;               // written under the host lock
    std::vector<const UPSCommandInfo*> _commands; // written under the host lock

    State _state = State::Probing;
    bool _firstWalkDone = false;
    bool _stale = false;
    uint32_t _walkStartedAt = 0;
    bool _walkStarted = false; // at least one update walk since the init
    uint32_t _heldUntil = 0;
    // Name (static, of the QxSubdriver) of the protocol claimed last: not reset by setup(), so
    // a new claim after a recovery or a Held state can tell whether the protocol changed
    const char* _lastProtocol = nullptr;
    // The settings of configure() (US-138)
    QxOptions _options;
    // A poll() ran since setup(): configure() then restarts the detection
    bool _started = false;
};

#endif // QX_DRIVER_H
