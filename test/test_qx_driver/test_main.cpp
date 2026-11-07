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

void test_no_transport_ported_yet(void) {
    FakeQxUsbPrimitives usb(nullptr);
    // The transports of the DriverRegistry Q* rows (qx_usb_id[] of nutdrv_qx)
    const char* names[] = { "cypress", "ablerex", "krauler", "phoenixtec", "ippon", "phoenix",
                            "sgs", "omron", "fabula", "hunnox", "fuji", "snr", "armac" };
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
    RUN_TEST(test_no_transport_ported_yet);
    RUN_TEST(test_deadline_of_the_command_is_set_and_cleared);
    return UNITY_END();
}
#endif
#endif
