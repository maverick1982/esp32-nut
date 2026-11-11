#include <unity.h>
#include <string.h>
#include <memory>
#include <string>
#include "QxDriver.h"
#include "QxControl.h"
#include "QxUsbTransports.h"
#include "FirstData.h"
#include "IUpsControlPort.h"
#include "FakeQxUsbPrimitives.h"
#include "FakeQxHostPort.h"
#include "QxLoopbackTransport.h"
#include "QxTestingTables.h"

// US-136, the demonstration: the Q* driver of the host on fake primitives and a fake
// transport. Attach, protocol detection, first full walk, values in the host data, commands,
// stale data after failed polls, the "qx" object of the export, the wait bound of a poll step
// and no primitive called with the host lock held (ADR 0008, ADR 0013 sections 2, 3 and 5).

void setUp(void) {}
void tearDown(void) {}

namespace {

struct Rig {
    FakeQxUsbPrimitives usb{MEGATEC_TESTING, QX_MEGATEC.rejected};
    FakeQxHostPort host;
    QxDriver driver;
    UPSData data; // the host data, published by the driver
    uint32_t longestStep = 0;

    Rig() : driver(std::unique_ptr<QxUsbTransport>(new QxLoopbackTransport(usb)), host, 0x0665, 0x5161) {
        usb.lockFlag = &host.locked;
        driver.setup();
    }

    // A ported transport of QxUsbTransports on a krauler-family fake device (US-137): the
    // commands are string descriptor indices, answered from the Megatec testing_t table
    explicit Rig(const char* transport)
        : driver(QxUsbTransports::create(transport, usb), host, 0x0001, 0x0000) {
        usb.stringDevice = true;
        usb.lockFlag = &host.locked;
        driver.setup();
    }

    // One poll step at the time of the fake clock, then 10 ms (POLL_TASK_PERIOD_MS)
    void step() {
        const uint32_t start = usb.clock;
        driver.poll(data, start);
        const uint32_t took = usb.clock - start;
        if (took > longestStep) longestStep = took;
        usb.clock += 10;
    }
    // Steps until the first full walk, at most n
    bool runUntilFirstData(int n = 200) {
        for (int i = 0; i < n && !driver.initialPollDone(); i++) step();
        return driver.initialPollDone();
    }
    // Steps for ms of fake time
    void runFor(uint32_t ms) {
        const uint32_t end = usb.clock + ms;
        while ((int32_t)(usb.clock - end) < 0) step();
    }
    std::string get(const char* key) const {
        const String* v = data.find(key);
        return v ? v->c_str() : "(missing)";
    }
    bool waitingFirstData() const {
        bool timedOut = false;
        String status = UPSData::computeUPSStatusString(data);
        return !firstDataReady(driver.initialPollDone(), status != "Unknown", driver.firstDataWaitBounded(),
                               usb.clock, 30000, timedOut);
    }
};

// The host's port as QxControl sees it, running the driver's commands at once (the real host
// runs them in the poll task)
class ForwardPort : public IUpsControlPort {
public:
    explicit ForwardPort(Rig& r) : _r(r) {}
    void lock() const override {}
    void unlock() const override {}
    bool isConnected() const override { return true; }
    const UPSData& upsData() const override { return _r.data; }
    bool setBeeper(bool) override { return false; }
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        return QxControl::instance().executeCommand(*this, name, param);
    }
    std::vector<const UPSCommandInfo*> driverCommands() const override { return _r.driver.commands(); }
    CommandResult runDriverCommand(const char* name, const char* param) override {
        return _r.driver.runCommand(_r.data, name, param);
    }
    NutTable::SetStatus runDriverSetVariable(const char* name, const char* value) override {
        return _r.driver.runSetVariable(_r.data, name, value);
    }
private:
    Rig& _r;
};

bool listed(const std::vector<const UPSCommandInfo*>& cmds, const char* name) {
    for (const auto* c : cmds) {
        if (strcmp(c->name, name) == 0) return true;
    }
    return false;
}

} // namespace

void test_core_of_the_driver_without_hid(void) {
    Rig r;
    TEST_ASSERT_NULL(r.driver.hid());
    TEST_ASSERT_TRUE(r.driver.qx() == &r.driver);
    TEST_ASSERT_EQUAL_STRING("nutdrv_qx", r.driver.getDriverName());
    TEST_ASSERT_FALSE(r.driver.firstDataWaitBounded());
    TEST_ASSERT_TRUE(r.driver.needsInterruptIn());
    TEST_ASSERT_EQUAL_STRING("loopback", r.driver.transportName());
}

// During the link back-off the driver skips its steps, as the HID drivers do: no transfer, no
// exchange, no probing attempt counted
void test_link_backoff_skips_the_step(void) {
    Rig r;
    r.host.paused = true;
    r.runFor(5000);
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL(0, (int)d.exchanges.size());
    TEST_ASSERT_EQUAL(0, d.tries);
    TEST_ASSERT_EQUAL(0, d.failed_cycles);
    TEST_ASSERT_FALSE(r.driver.initialPollDone());
    r.host.paused = false;
    TEST_ASSERT_TRUE(r.runUntilFirstData());
}

void test_full_cycle_attach_detect_first_walk_values(void) {
    Rig r;
    TEST_ASSERT_TRUE(r.waitingFirstData());
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    TEST_ASSERT_FALSE(r.waitingFirstData());
    TEST_ASSERT_EQUAL(1, r.host.count("[Q*] Using protocol: Megatec 0.10"));
    // Values in the host data
    TEST_ASSERT_EQUAL_STRING("nutdrv_qx", r.get("driver.name").c_str());
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", r.get("driver.version.data").c_str());
    TEST_ASSERT_EQUAL_STRING("0665", r.get("ups.vendorid").c_str());
    TEST_ASSERT_EQUAL_STRING("5161", r.get("ups.productid").c_str());
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(r.data).c_str());
    TEST_ASSERT_FALSE(r.driver.dataStale());
    // Commands of the protocol, once the init is done
    std::vector<const UPSCommandInfo*> cmds = r.driver.commands();
    TEST_ASSERT_TRUE(listed(cmds, "beeper.toggle"));
    TEST_ASSERT_TRUE(listed(cmds, "shutdown.return"));
    // Never a primitive under the host lock
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

void test_first_data_waits_until_protocol_and_first_walk(void) {
    Rig r;
    // Before the claim: waiting, also long after the 30 s of the HID drivers
    r.usb.muted = true;
    r.runFor(60000);
    TEST_ASSERT_FALSE(r.driver.initialPollDone());
    TEST_ASSERT_TRUE(r.waitingFirstData());
    r.usb.muted = false;
    TEST_ASSERT_TRUE(r.runUntilFirstData(2000));
    TEST_ASSERT_FALSE(r.waitingFirstData());
}

void test_commands_through_qxcontrol(void) {
    Rig r;
    ForwardPort port(r);
    // No protocol yet: nothing is offered
    TEST_ASSERT_TRUE(r.driver.runCommand(r.data, "beeper.toggle", nullptr) == CommandResult::NOT_SUPPORTED);
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    size_t before = r.usb.received().size();
    TEST_ASSERT_TRUE(QxControl::instance().executeCommand(port, "beeper.toggle", nullptr) == CommandResult::OK);
    TEST_ASSERT_EQUAL_UINT(before + 1, r.usb.received().size());
    TEST_ASSERT_EQUAL_STRING("Q\r", r.usb.received().back().c_str());
    // ups.delay.shutdown 30 s by default: S.5 (the answer of megatec_testing[])
    TEST_ASSERT_TRUE(QxControl::instance().executeCommand(port, "shutdown.stayoff", nullptr) == CommandResult::OK);
    TEST_ASSERT_EQUAL_STRING("S.5R0000\r", r.usb.received().back().c_str());
    // Not offered by megatec
    TEST_ASSERT_TRUE(QxControl::instance().executeCommand(port, "outlet.1.load.off", nullptr) ==
                     CommandResult::NOT_SUPPORTED);
    // SET VAR of a server-side variable of the protocol
    TEST_ASSERT_TRUE(QxControl::instance().setVariable(port, "ups.delay.shutdown", "60") == NutTable::SetStatus::OK);
    TEST_ASSERT_EQUAL_STRING("60", r.get("ups.delay.shutdown").c_str());
    // The poll goes on after the commands
    r.runFor(5000);
    TEST_ASSERT_FALSE(r.driver.dataStale());
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

void test_stale_after_consecutive_failed_polls_then_reestablished(void) {
    Rig r;
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    r.usb.muted = true;
    // upsdrv_updateinfo(): 4 failures are not stale yet, the 5th is (QX_MAXTRIES + 2 walks)
    int walks = 0;
    while (!r.driver.dataStale() && walks < 20) {
        r.runFor(QxEngine::DEFAULT_POLLINTERVAL_S * 1000UL);
        walks++;
    }
    TEST_ASSERT_TRUE(r.driver.dataStale());
    TEST_ASSERT_TRUE(walks >= QX_MAXTRIES + 2);
    TEST_ASSERT_TRUE(walks <= QX_MAXTRIES + 3);
    TEST_ASSERT_EQUAL(1, r.host.count("Communications with the UPS lost"));
    r.runFor(10000);
    TEST_ASSERT_EQUAL(1, r.host.count("Communications with the UPS lost"));
    r.usb.muted = false;
    r.runFor(3000);
    TEST_ASSERT_FALSE(r.driver.dataStale());
    TEST_ASSERT_EQUAL(1, r.host.count("[Q*] Communications with the UPS re-established"));
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

void test_poll_step_stays_within_the_bound_with_a_mute_ups(void) {
    Rig r;
    r.usb.muted = true;
    r.runFor(120000); // probing cycles and back-off
    TEST_ASSERT_TRUE(r.longestStep > 0);
    TEST_ASSERT_TRUE(r.longestStep <= qxStepBoundMs(QxLoopbackTransport::BUDGET_MS));
    // Mute while polling too
    Rig p;
    TEST_ASSERT_TRUE(p.runUntilFirstData());
    p.usb.muted = true;
    p.runFor(60000);
    TEST_ASSERT_TRUE(p.longestStep <= qxStepBoundMs(QxLoopbackTransport::BUDGET_MS));
    TEST_ASSERT_TRUE(qxStepBoundMs(QxLoopbackTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
}

void test_not_supported_when_no_protocol_claims(void) {
    Rig r;
    r.usb.muted = true;
    // Until the end of the first probing cycle: the back-off starts
    for (int i = 0; i < 10000 && r.host.count("Device not supported!") == 0; i++) r.step();
    TEST_ASSERT_EQUAL(1, r.host.count("Device not supported!"));
    UsbDiagQx d;
    r.host.lock();
    r.driver.fillDiagnostics(d);
    r.host.unlock();
    TEST_ASSERT_EQUAL_STRING("not_supported", d.probe.c_str());
    TEST_ASSERT_EQUAL_STRING("", d.protocol.c_str());
    TEST_ASSERT_TRUE(d.failed_cycles >= 1);
    TEST_ASSERT_TRUE(d.retry_ms >= QxProber::FIRST_RETRY_MS);
}

void test_diagnostics_keep_the_last_20_exchanges(void) {
    Rig r;
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    r.runFor(60000); // many more walks
    r.usb.muted = true;
    r.runFor(QxEngine::DEFAULT_POLLINTERVAL_S * 1000UL + 10);
    UsbDiagQx d;
    r.host.lock();
    r.driver.fillDiagnostics(d);
    r.host.unlock();
    TEST_ASSERT_TRUE(d.present);
    TEST_ASSERT_EQUAL_STRING("loopback", d.transport.c_str());
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", d.protocol.c_str());
    TEST_ASSERT_EQUAL_STRING("claimed", d.probe.c_str());
    TEST_ASSERT_EQUAL_UINT(QxExchangeLog::CAPACITY, d.exchanges.size());
    // Oldest first: an answered Q1, then the failed one at the end
    const UsbDiagQxExchange& ok = d.exchanges[0];
    TEST_ASSERT_EQUAL_STRING("Q1\r", ok.command.c_str());
    TEST_ASSERT_EQUAL_INT32((int32_t)ok.answer.size(), ok.length);
    TEST_ASSERT_EQUAL('(', ok.answer[0]);
    TEST_ASSERT_EQUAL('\r', ok.answer.back());
    TEST_ASSERT_EQUAL_STRING("", ok.error.c_str());
    const UsbDiagQxExchange& bad = d.exchanges.back();
    TEST_ASSERT_EQUAL_INT32(QX_USB_ERR_TIMEOUT, bad.length);
    TEST_ASSERT_TRUE(bad.answer.empty());
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_TIMEOUT", bad.error.c_str());
    TEST_ASSERT_TRUE(bad.uptime_ms > ok.uptime_ms);
    TEST_ASSERT_TRUE(bad.duration_ms >= QX_CTRL_TIMEOUT_MS);
}

void test_exchange_log_ring_drops_the_oldest(void) {
    QxExchangeLog log;
    char cmd[8];
    for (int i = 0; i < 21; i++) {
        snprintf(cmd, sizeof(cmd), "C%d\r", i);
        log.add(cmd, strlen(cmd), nullptr, 0, (uint32_t)i, 1, "");
    }
    TEST_ASSERT_EQUAL_UINT(20, log.size());
    TEST_ASSERT_EQUAL_STRING("C1\r", log.at(0).command);
    TEST_ASSERT_EQUAL_STRING("C20\r", log.at(19).command);
    // A long answer is truncated and flagged
    uint8_t big[200];
    memset(big, 'x', sizeof(big));
    log.add("Q1\r", 3, big, 200, 99, 1, "");
    TEST_ASSERT_TRUE(log.at(19).truncated);
    TEST_ASSERT_EQUAL_UINT(QxExchange::ANSWER_MAX, log.at(19).answerLen);
    TEST_ASSERT_EQUAL_INT32(200, log.at(19).result);
}

void test_setup_after_a_recovery_probes_again_and_keeps_the_values(void) {
    Rig r;
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    r.driver.setup();
    TEST_ASSERT_FALSE(r.driver.initialPollDone());
    TEST_ASSERT_TRUE(r.driver.commands().empty());
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL_STRING("probing", d.probe.c_str());
    r.step();
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage").c_str());
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    TEST_ASSERT_EQUAL(2, r.host.count("[Q*] Using protocol: Megatec 0.10"));
}

void test_transports_of_the_registry_rows(void) {
    FakeQxUsbPrimitives usb(nullptr);
    // The transports of the DriverRegistry Q* rows (qx_usb_id[] of nutdrv_qx): the krauler
    // family is ported (US-137), the others stay "not supported yet"
    struct Ported { const char* name; uint32_t budget; };
    const Ported ported[] = { { "krauler", 3000 }, { "fabula", 3000 }, { "hunnox", 4500 } };
    for (const Ported& p : ported) {
        std::unique_ptr<QxUsbTransport> t = QxUsbTransports::create(p.name, usb);
        TEST_ASSERT_NOT_NULL_MESSAGE(t.get(), p.name);
        TEST_ASSERT_EQUAL_STRING(p.name, t->name());
        TEST_ASSERT_EQUAL_UINT32(p.budget, t->commandBudgetMs());
        TEST_ASSERT_FALSE(t->needsInterruptIn());
    }
    TEST_ASSERT_EQUAL(3, (int)QxUsbTransports::count());
    const char* names[] = { "cypress", "ablerex", "phoenixtec", "ippon", "phoenix",
                            "sgs", "omron", "fuji", "snr", "armac" };
    for (const char* n : names) {
        TEST_ASSERT_NULL_MESSAGE(QxUsbTransports::create(n, usb).get(), n);
    }
    TEST_ASSERT_NULL(QxUsbTransports::create(nullptr, usb).get());
    // Every transport registered keeps the poll step bound (US-137 adds rows)
    for (size_t i = 0; i < QxUsbTransports::count(); i++) {
        TEST_ASSERT_TRUE_MESSAGE(qxStepBoundMs(QxUsbTransports::budgetAt(i)) <= QX_STEP_BOUND_MAX_MS,
                                 QxUsbTransports::nameAt(i));
    }
}

void test_deadline_of_the_command_is_set_and_cleared(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    QxLoopbackTransport t(usb);
    usb.muted = true;
    char buf[64] = {0};
    const uint32_t start = usb.clock;
    TEST_ASSERT_EQUAL_INT(QX_USB_ERR_TIMEOUT, t.command("Q1\r", 3, buf, sizeof(buf)));
    TEST_ASSERT_TRUE(usb.clock - start <= QxLoopbackTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
    // Deadline cleared: a later transfer is not refused as past the budget
    usb.muted = false;
    usb.clock += 100000;
    TEST_ASSERT_TRUE(t.command("Q1\r", 3, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL('(', buf[0]);
}

// US-137 (review of US-136, point 2): after a recovery another protocol is claimed; the keys
// only the previous one published leave the host data, the identity keys stay
void test_protocol_change_drops_the_previous_values(void) {
    Rig r;
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", r.get("driver.version.data").c_str());
    // From the I command of Megatec, not of the Q1-only UPS
    TEST_ASSERT_TRUE(r.data.find("ups.serial") != nullptr);
    TEST_ASSERT_TRUE(r.data.find("battery.runtime") != nullptr);

    // The UPS behind the bridge answers only Q1 now: in NUT INNOVATAE claims it (US-134)
    r.usb.device.setTable(Q1_TESTING, QX_Q1.rejected);
    r.driver.setup(); // recovery of the host
    UsbDiagQx d;
    for (int i = 0; i < 2000; i++) {
        r.step();
        r.driver.fillDiagnostics(d);
        if (d.probe == "claimed") break;
    }
    TEST_ASSERT_EQUAL_STRING("INNOVATAE 0.01", d.protocol.c_str());
    TEST_ASSERT_EQUAL(1, r.host.count("[Q*] Protocol changed from Megatec 0.10 to INNOVATAE 0.01"));
    TEST_ASSERT_NULL(r.data.find("ups.serial"));
    TEST_ASSERT_NULL(r.data.find("battery.runtime"));
    TEST_ASSERT_EQUAL_STRING("nutdrv_qx", r.get("driver.name").c_str());
    TEST_ASSERT_EQUAL_STRING("0665", r.get("ups.vendorid").c_str());
    TEST_ASSERT_EQUAL_STRING("5161", r.get("ups.productid").c_str());

    // The first walk of the new protocol fills its keys again
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    TEST_ASSERT_EQUAL_STRING("INNOVATAE 0.01", r.get("driver.version.data").c_str());
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage").c_str());
    TEST_ASSERT_NULL(r.data.find("ups.serial"));
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

// --- US-138: protocol and novendor from Board Settings ---

// Before the first poll configure() only stores the settings: nothing dropped, nothing logged
void test_configure_before_start_keeps_data(void) {
    Rig r;
    QxOptions opt;
    opt.novendor = true;
    r.driver.configure("megatec", opt);
    TEST_ASSERT_EQUAL(0, r.host.count("Settings changed"));
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL_STRING("megatec", d.forced_protocol.c_str());
    TEST_ASSERT_TRUE(d.novendor);
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", r.driver.claimedProtocol());
}

// A change while polling: the detection starts again with the new protocol, the values of the
// previous one leave the host data, the identity keys stay
void test_configure_restarts_detection(void) {
    Rig r;
    TEST_ASSERT_EQUAL_STRING("", r.driver.claimedProtocol());
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    TEST_ASSERT_TRUE(r.data.find("ups.serial") != nullptr);
    TEST_ASSERT_FALSE(r.driver.commands().empty());

    const size_t sentBefore = r.usb.received().size();
    r.driver.configure("zinto", QxOptions()); // the megatec fake does not answer FW?
    TEST_ASSERT_EQUAL(1, r.host.count("[Q*] Settings changed (protocol: zinto, novendor: off): detecting the protocol again"));
    TEST_ASSERT_FALSE(r.driver.initialPollDone());
    TEST_ASSERT_TRUE(r.driver.commands().empty());
    TEST_ASSERT_EQUAL_STRING("", r.driver.claimedProtocol());
    r.step();
    TEST_ASSERT_NULL(r.data.find("ups.serial"));
    TEST_ASSERT_NULL(r.data.find("input.voltage"));
    TEST_ASSERT_NULL(r.data.find("driver.version.data"));
    TEST_ASSERT_EQUAL_STRING("nutdrv_qx", r.get("driver.name").c_str());
    TEST_ASSERT_EQUAL_STRING("0665", r.get("ups.vendorid").c_str());
    TEST_ASSERT_EQUAL_STRING("5161", r.get("ups.productid").c_str());
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL_STRING("zinto", d.forced_protocol.c_str());
    TEST_ASSERT_FALSE(d.novendor);
    // Only zinto is tried: no claim, so no first data
    r.runFor(5000);
    TEST_ASSERT_FALSE(r.driver.initialPollDone());
    TEST_ASSERT_TRUE(r.usb.received().size() > sentBefore);
    for (size_t i = sentBefore; i < r.usb.received().size(); i++) {
        const std::string& c = r.usb.received()[i];
        TEST_ASSERT_TRUE_MESSAGE(c == "Q1\r" || c == "FW?\r", c.c_str());
    }
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

// Back to automatic: the line says "auto" and megatec is found again
void test_configure_back_to_automatic(void) {
    Rig r;
    r.driver.configure("zinto", QxOptions());
    r.runFor(1000);
    r.driver.configure("", QxOptions());
    TEST_ASSERT_EQUAL(1, r.host.count("[Q*] Settings changed (protocol: auto, novendor: off)"));
    TEST_ASSERT_TRUE(r.runUntilFirstData(2000));
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", r.get("driver.version.data").c_str());
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL_STRING("", d.forced_protocol.c_str());
}

// The demonstration of #91 on the fake: a hunnox device behind the hunnox transport is claimed
// by protocol = hunnox with novendor (Q1 only, no 0x0c in the preamble), and reaches the
// first walk with battery.charge from BL
void test_hunnox_with_novendor_reaches_the_first_walk(void) {
    Rig r("hunnox");
    r.usb.device.setTable(HUNNOX_TESTING, QX_HUNNOX.rejected);
    QxOptions opt;
    opt.novendor = true;
    r.driver.configure("hunnox", opt);
    TEST_ASSERT_TRUE(r.runUntilFirstData(2000));
    TEST_ASSERT_EQUAL_STRING("Hunnox 0.03", r.get("driver.version.data").c_str());
    TEST_ASSERT_EQUAL_STRING("100", r.get("battery.charge").c_str());
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("Q1\r", r.usb.received().front().c_str());
    for (const auto& c : r.usb.stringCalls) TEST_ASSERT_NOT_EQUAL(0x0c, c.index);
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL_STRING("Hunnox 0.03", d.protocol.c_str());
    TEST_ASSERT_EQUAL_STRING("hunnox", d.forced_protocol.c_str());
    TEST_ASSERT_TRUE(d.novendor);
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

// Without novendor the same device is not claimed: FW? is echoed by the transport (#91)
void test_hunnox_without_novendor_is_not_claimed(void) {
    Rig r("hunnox");
    r.usb.device.setTable(HUNNOX_TESTING, QX_HUNNOX.rejected);
    r.driver.configure("hunnox", QxOptions());
    r.runFor(20000);
    TEST_ASSERT_FALSE(r.driver.initialPollDone());
    UsbDiagQx d;
    r.driver.fillDiagnostics(d);
    TEST_ASSERT_EQUAL_STRING("", d.protocol.c_str());
    TEST_ASSERT_EQUAL(1, r.host.count("Protocol hunnox did not claim the UPS"));
}

// #91, qx-hunnox-v2: Q1 failed on the VIDA because usb_get_string_simple() refuses its
// strings (NUT needs langid_fix = 0x0409). The transport switches to langid_fix by itself,
// says it once in the System Log and in the export, and the protocol is claimed.
void test_hunnox_with_a_broken_langid_list_switches_to_langid_fix(void) {
    Rig r("hunnox");
    r.usb.device.setTable(HUNNOX_TESTING, QX_HUNNOX.rejected);
    r.usb.brokenLangidList = true;
    QxOptions opt;
    opt.novendor = true;
    r.driver.configure("hunnox", opt);
    TEST_ASSERT_TRUE(r.runUntilFirstData(2000));
    TEST_ASSERT_EQUAL_STRING("Hunnox 0.03", r.get("driver.version.data").c_str());
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage").c_str());
    r.runFor(60000);
    TEST_ASSERT_EQUAL(1, r.host.count("[Q*] The UPS answers only with LANGID 0x0409: langid_fix = 0x0409 applied"));
    UsbDiagQx d;
    r.host.lock();
    r.driver.fillDiagnostics(d);
    r.host.unlock();
    TEST_ASSERT_EQUAL_STRING("0x0409", d.langid_fix.c_str());
    TEST_ASSERT_TRUE(d.string_failure);
    TEST_ASSERT_EQUAL(3, d.sf_index);
    TEST_ASSERT_EQUAL_INT32(QX_USB_ERR_IO, d.sf_result);
    TEST_ASSERT_EQUAL_INT32(2, d.sf_langids_length);
    TEST_ASSERT_EQUAL(2, (int)d.sf_langids.size());
    TEST_ASSERT_FALSE(d.sf_answer_asked);
    TEST_ASSERT_EQUAL(0, r.usb.callsUnderLock);
}

// A device that answers usb_get_string_simple() keeps it: no langid_fix, no failure
void test_hunnox_with_a_good_langid_list_keeps_the_simple_read(void) {
    Rig r("hunnox");
    r.usb.device.setTable(HUNNOX_TESTING, QX_HUNNOX.rejected);
    QxOptions opt;
    opt.novendor = true;
    r.driver.configure("hunnox", opt);
    TEST_ASSERT_TRUE(r.runUntilFirstData(2000));
    TEST_ASSERT_EQUAL(0, r.host.count("langid_fix"));
    UsbDiagQx d;
    r.host.lock();
    r.driver.fillDiagnostics(d);
    r.host.unlock();
    TEST_ASSERT_EQUAL_STRING("", d.langid_fix.c_str());
    TEST_ASSERT_FALSE(d.string_failure);
}

// --- US-137: the krauler family on the driver ---

namespace {
const char* const KRAULER_FAMILY[] = { "krauler", "fabula", "hunnox" };
}

// The demonstration: a UPS behind krauler, fabula or hunnox reaches the protocol claim and the
// first full walk, as with NUT (mustek and megatec/old probe with QS and D, which these
// transports cannot encode: echoed, so not claimed; then Megatec with Q1 and I)
void test_krauler_family_reaches_the_protocol_and_first_walk(void) {
    for (const char* name : KRAULER_FAMILY) {
        Rig r(name);
        TEST_ASSERT_TRUE_MESSAGE(r.runUntilFirstData(), name);
        UsbDiagQx d;
        r.host.lock();
        r.driver.fillDiagnostics(d);
        r.host.unlock();
        TEST_ASSERT_EQUAL_STRING_MESSAGE(name, d.transport.c_str(), name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("Megatec 0.10", d.protocol.c_str(), name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("claimed", d.probe.c_str(), name);
        TEST_ASSERT_EQUAL_MESSAGE(1, r.host.count("[Q*] Using protocol: Megatec 0.10"), name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("215.0", r.get("input.voltage").c_str(), name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("OL", UPSData::computeUPSStatusString(r.data).c_str(), name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("0001", r.get("ups.vendorid").c_str(), name);
        TEST_ASSERT_FALSE_MESSAGE(r.driver.needsInterruptIn(), name);
        TEST_ASSERT_EQUAL_MESSAGE(0, r.usb.callsUnderLock, name);
        TEST_ASSERT_TRUE_MESSAGE(r.longestStep <= qxStepBoundMs(r.driver.transportBudgetMs()), name);
    }
}

void test_krauler_exchanges_in_the_export(void) {
    Rig r("krauler");
    // The probing: the first exchanges include the echo of a command krauler cannot encode
    bool echoSeen = false;
    for (int i = 0; i < 200 && !echoSeen && !r.driver.initialPollDone(); i++) {
        r.step();
        UsbDiagQx d;
        r.host.lock();
        r.driver.fillDiagnostics(d);
        r.host.unlock();
        for (const UsbDiagQxExchange& x : d.exchanges) {
            if (x.command != "QS\r") continue;
            // Echoed at once: the command itself, no transfer, no time
            TEST_ASSERT_EQUAL_INT32(3, x.length);
            TEST_ASSERT_EQUAL_STRING("QS\r", std::string(x.answer.begin(), x.answer.end()).c_str());
            TEST_ASSERT_EQUAL_UINT32(0, x.duration_ms);
            TEST_ASSERT_EQUAL_STRING("", x.error.c_str());
            echoSeen = true;
        }
    }
    TEST_ASSERT_TRUE(echoSeen);
    TEST_ASSERT_TRUE(r.runUntilFirstData());
    UsbDiagQx d;
    r.host.lock();
    r.driver.fillDiagnostics(d);
    r.host.unlock();
    bool q1Seen = false;
    for (const UsbDiagQxExchange& x : d.exchanges) {
        if (x.command != "Q1\r") continue;
        TEST_ASSERT_TRUE(x.length > 0);
        TEST_ASSERT_EQUAL('(', x.answer[0]);
        TEST_ASSERT_EQUAL('\r', x.answer.back());
        q1Seen = true;
    }
    TEST_ASSERT_TRUE(q1Seen);
    // A failed read: the primitive error is in the export
    r.usb.muted = true;
    r.runFor(QxEngine::DEFAULT_POLLINTERVAL_S * 1000UL + 10);
    r.host.lock();
    r.driver.fillDiagnostics(d);
    r.host.unlock();
    const UsbDiagQxExchange& bad = d.exchanges.back();
    TEST_ASSERT_EQUAL_INT32(QX_USB_ERR_TIMEOUT, bad.length);
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_TIMEOUT", bad.error.c_str());
}

void test_krauler_family_step_bound_with_a_mute_ups(void) {
    for (const char* name : KRAULER_FAMILY) {
        Rig r(name);
        r.usb.muted = true;
        r.runFor(60000);
        TEST_ASSERT_TRUE_MESSAGE(r.longestStep > 0, name);
        TEST_ASSERT_TRUE_MESSAGE(r.longestStep <= qxStepBoundMs(r.driver.transportBudgetMs()), name);
        // Mute while polling too
        Rig p(name);
        TEST_ASSERT_TRUE_MESSAGE(p.runUntilFirstData(), name);
        p.usb.muted = true;
        p.runFor(60000);
        TEST_ASSERT_TRUE_MESSAGE(p.longestStep <= qxStepBoundMs(p.driver.transportBudgetMs()), name);
    }
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_core_of_the_driver_without_hid);
    RUN_TEST(test_link_backoff_skips_the_step);
    RUN_TEST(test_full_cycle_attach_detect_first_walk_values);
    RUN_TEST(test_first_data_waits_until_protocol_and_first_walk);
    RUN_TEST(test_commands_through_qxcontrol);
    RUN_TEST(test_stale_after_consecutive_failed_polls_then_reestablished);
    RUN_TEST(test_poll_step_stays_within_the_bound_with_a_mute_ups);
    RUN_TEST(test_not_supported_when_no_protocol_claims);
    RUN_TEST(test_diagnostics_keep_the_last_20_exchanges);
    RUN_TEST(test_exchange_log_ring_drops_the_oldest);
    RUN_TEST(test_setup_after_a_recovery_probes_again_and_keeps_the_values);
    RUN_TEST(test_transports_of_the_registry_rows);
    RUN_TEST(test_deadline_of_the_command_is_set_and_cleared);
    RUN_TEST(test_protocol_change_drops_the_previous_values);
    RUN_TEST(test_configure_before_start_keeps_data);
    RUN_TEST(test_configure_restarts_detection);
    RUN_TEST(test_configure_back_to_automatic);
    RUN_TEST(test_hunnox_with_novendor_reaches_the_first_walk);
    RUN_TEST(test_hunnox_without_novendor_is_not_claimed);
    RUN_TEST(test_hunnox_with_a_broken_langid_list_switches_to_langid_fix);
    RUN_TEST(test_hunnox_with_a_good_langid_list_keeps_the_simple_read);
    RUN_TEST(test_krauler_family_reaches_the_protocol_and_first_walk);
    RUN_TEST(test_krauler_exchanges_in_the_export);
    RUN_TEST(test_krauler_family_step_bound_with_a_mute_ups);
    return UNITY_END();
}
#endif
#endif
