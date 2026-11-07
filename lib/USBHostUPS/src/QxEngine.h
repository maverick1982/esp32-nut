#ifndef QX_ENGINE_H
#define QX_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "QxTypes.h"
#include "IQxTransport.h"
#include "UPSData.h"

/**
 * @brief The Q* engine (ADR 0013, section 4): the table walk of nut_repo/drivers/nutdrv_qx.c
 * on one protocol subdriver, over one IQxTransport.
 *
 * A port of qx_process(), qx_process_answer(), ups_infoval_set(), update_status(),
 * find_nut_info(), qx_ups_walk(), the data part of upsdrv_initinfo()/upsdrv_updateinfo(),
 * ups_status_set()/ups_alarm_set() and the battery guesstimation. Pure logic: no USB, no
 * log, no lock: QxDriver (US-136) calls it in the poll task on shadow data and publishes the
 * changes under the host lock, so no lock is held across a transfer.
 *
 * State: the tables are static const; the per-row runtime flags are copied into _flags; the
 * runtime answer/value of upstream item_t live in one pair of work buffers, because
 * qx_ups_walk() clears them after every row and no ported protocol reads the answer or the
 * value of another row. A protocol that does (voltronic, nutdrv_qx_voltronic.c:2692) will
 * need per-row buffers.
 *
 * Order of the calls, as upstream: claim() -> initUps() -> initInfo() -> updateInfo() every
 * poll interval.
 */
class QxEngine {
public:
    static const uint32_t DEFAULT_POLLFREQ_S = 30;    // DEFAULT_POLLFREQ, nutdrv_qx.h
    static const uint32_t DEFAULT_POLLINTERVAL_S = 2; // poll_interval, main.c:102 (host cadence)

    enum class Update : uint8_t {
        Ok,      // status read, data written
        Failed,  // walk failed, data untouched, not yet stale
        Stale,   // walk failed more than QX_MAXTRIES + 1 times in a row (dstate_datastale())
        Pending  // updateInfoStep() only: walk not finished, one Q* command sent (US-136)
    };

    // Outcome of initInfoStep() (US-136)
    enum class Phase : uint8_t { Pending, Ok, Failed };

    QxEngine(const QxSubdriver& sub, IQxTransport& transport, const QxOptions& options = QxOptions());

    /**
     * @brief Back to the state of a newly built engine, on sub with options (same transport).
     *
     * The probing (QxProber, US-134) reuses one engine for every protocol it tries instead
     * of allocating one per protocol. Every field of the state is set here, and the
     * constructor uses it: a new field must be reset here too.
     */
    void reset(const QxSubdriver& sub, const QxOptions& options);

    // --- Phases of NUT ---------------------------------------------------------------
    // subdriver->claim(): true when the device is covered by the protocol (US-134 probes)
    bool claim(UPSData& data);
    // subdriver->initups(), end of upsdrv_initups()
    void initUps();
    // upsdrv_initinfo(): false where upstream calls fatalx("Can't initialise data")
    bool initInfo(UPSData& data, uint32_t nowMs);
    // upsdrv_updateinfo(): full walk every DEFAULT_POLLFREQ_S or after noteDataChanged()
    Update updateInfo(UPSData& data, uint32_t nowMs);

    // --- The same phases one Q* command at a time (US-136, ADR 0013 section 3) --------
    // The poll task waits for at most one command per call; initInfo()/updateInfo() loop on
    // these. A row that reuses the answer of the previous one (previous_item) sends nothing.
    // upsdrv_initinfo(): Pending until the INIT walk ends
    Phase initInfoStep(UPSData& data, uint32_t nowMs);
    // upsdrv_updateinfo(): Pending until the walk ends, then Ok, Failed or Stale
    Update updateInfoStep(UPSData& data, uint32_t nowMs);
    // Drops a walk in progress (a command runs between two steps): the next step starts a new one
    void abortWalk();
    bool walkPending() const { return _walkActive; }
    // data_has_changed = TRUE after an instcmd/setvar (US-135)
    void noteDataChanged() { _dataChanged = true; }
    // The last Ok came after more than QX_MAXTRIES failures in a row ("Communications with
    // the UPS re-established"): as upstream, from 4 failures, one before Stale
    bool reestablished() const { return _reestablished; }

    // --- Commands and SET VAR (US-135, QxEngineControl.cpp) ---------------------------
    // STAT_INSTCMD_* of NUT
    enum class Instcmd : uint8_t { Handled, Invalid, Failed, ConversionFailed };
    // STAT_SET_* of NUT
    enum class Setvar : uint8_t { Handled, Unknown, ConversionFailed };

    /**
     * @brief instcmd() of nutdrv_qx.c:2802-2945: the QX_FLAG_CMD row of cmdname, or the
     * fallbacks load.on/load.off/shutdown.return/shutdown.stayoff on the load.*.delay rows.
     *
     * extradata nullptr or "" = none (upsd passes NULL). Handled sets data_has_changed.
     */
    Instcmd instcmd(UPSData& data, const char* cmdname, const char* extradata);
    /**
     * @brief setvar() of nutdrv_qx.c:2948-3256: the QX_FLAG_SETVAR row of varname, checked
     * against its info_rw (range, enum or string length); a QX_FLAG_ABSENT row is written
     * into data, the others are sent to the UPS.
     */
    Setvar setvar(UPSData& data, const char* varname, const char* val);
    /**
     * @brief dstate_addcmd() of the INIT walk (nutdrv_qx.c:4604-4608) and of
     * upsdrv_initinfo() (3644-3656): a QX_FLAG_CMD row not skipped, or load.off / load.on
     * derived from load.off.delay / load.on.delay. Meaningful after initInfo().
     */
    bool commandOffered(const char* cmdname) const;

    // --- Functions exported by nutdrv_qx.h -------------------------------------------
    // find_nut_info(): the row of varname, or -1
    int findNutInfo(const char* varname, uint32_t flag, uint32_t noflag) const;
    // qx_process(): sends command (or the row's) and processes the answer; -1 or 0
    int process(int row, const char* command = nullptr);
    // ups_infoval_set(): -1 on failure, 0 for a status/alarm update, 1 otherwise
    int infovalSet(int row);
    // update_status(): a NUT status, optionally preceded by '!' to clear it
    void updateStatus(const char* nutvalue);
    // alarm_set()
    void alarmSet(const char* text);
    // qx_status()
    unsigned int status() const { return _status; }

    // Shared preprocess functions (qx_multiply_battvolt, qx_multiply_x1000, qx_multiply_m2s)
    static int qxMultiplyBattvolt(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
    static int qxMultiplyX1000(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
    static int qxMultiplyM2s(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);

    // --- Access for the subdriver functions (item->..., dstate_*, testvar) -------------
    const QxSubdriver& subdriver() const { return *_sub; }
    const QxOptions& options() const { return _options; }
    size_t rowCount() const { return _rows; }
    const QxItem& item(int row) const { return _sub->qx2nut[row]; }
    uint32_t flags(int row) const { return _flags[row]; }
    void addFlags(int row, uint32_t f) { _flags[row] |= f; }
    void clearFlags(int row, uint32_t f) { _flags[row] &= ~f; }
    // item->value / item->answer of the row being processed
    const char* value() const { return _value; }
    char* answer() { return _answer; }
    // dstate_getinfo/setinfo/delinfo on the data of the running call; no-ops outside one
    const char* getinfo(const char* name) const;
    void setinfo(const char* name, const char* value);
    void delinfo(const char* name);

    // ASCII strcasecmp() == 0, as the upstream comparisons
    static bool equalsIgnoreCase(const char* a, const char* b);

private:
    enum class WalkMode : uint8_t { INIT, QUICK_UPDATE, FULL_UPDATE };

    // Binds the data for the duration of a public call
    class DataScope {
    public:
        DataScope(QxEngine& e, UPSData* d) : _e(e), _prev(e._data) { e._data = d; }
        ~DataScope() { _e._data = _prev; }
    private:
        QxEngine& _e;
        UPSData* _prev;
    };

    enum class WalkStep : uint8_t { Pending, Done, Failed };

    int processAnswer(int row, size_t len);
    // Whole walk (beginWalk + walkStep until done)
    bool walk(WalkMode mode);
    // Head of qx_ups_walk(): resets previous_item and, for FULL_UPDATE, the guesstimation
    void beginWalk(WalkMode mode);
    // Loop of qx_ups_walk() from _walkRow: returns after the first row that sent a command
    WalkStep walkStep();
    void commitStatus();
    Update walkFailed();
    int battery();
    void initBattery();
    void clearWork();
    // instcmd()/setvar() with the data already bound (the fallbacks call them again)
    Instcmd doInstcmd(const char* cmdname, const char* extradata);
    Setvar doSetvar(const char* varname, const char* val);
    // The ranges qx_set_var() publishes for a row (pairs of info_rw values)
    static bool rangeContains(const QxItem& item, long value);
    // The enum values qx_set_var() publishes for a row
    static bool enumContains(const QxItem& item, const char* value);

    const QxSubdriver* _sub; // never null: set by the constructor and reset()
    IQxTransport& _transport;
    QxOptions _options;
    size_t _rows = 0;
    std::vector<uint32_t> _flags;
    UPSData* _data = nullptr;

    // Work buffers: members, not on the stack of the poll task
    char _cmd[QX_SMALLBUF];
    char _rx[QX_SMALLBUF - 1]; // buf[sizeof(item->answer) - 1] of qx_process()
    char _answer[QX_SMALLBUF];
    char _value[QX_SMALLBUF];
    char _infoval[QX_SMALLBUF];
    char _prevCommand[QX_SMALLBUF]; // previous_item of nutdrv_qx.c
    char _prevAnswer[QX_SMALLBUF];
    // value[SMALLBUF] of instcmd()/setvar(), shared: the fallbacks of instcmd() call
    // setvar()/instcmd() before filling it, so the two uses never overlap
    char _ctlValue[QX_SMALLBUF];

    unsigned int _status = 0;
    String _alarms;
    bool _dataChanged = false;
    uint32_t _lastPollMs = 0;
    int _retry = 0;
    bool _reestablished = false;

    // Walk in progress (US-136): reset() clears these too
    WalkMode _walkMode = WalkMode::INIT;
    size_t _walkRow = 0;
    bool _walkActive = false;
    bool _walkFull = false;    // the running update walk is a full one
    bool _initStarted = false; // initInfoStep() began the INIT walk

    // batt of nutdrv_qx.c, the fields used without runtimecal
    struct Batt {
        double packs = 1;
        double runtAct = -1;
        double voltAct = -1;
        double voltHigh = -1;
        double voltNom = -1;
        double voltLow = -1;
        double chrgAct = -1;
    } _batt;
    bool _battVoltageReportsOnePack = false; // ups.conf flag, not ported: always false
    bool _battVoltageReportsOnePackConsidered = false;
};

#endif // QX_ENGINE_H
