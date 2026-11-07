#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <string>
#include <vector>
#include "QxProbe.h"
#include "QxProtocols.h"
#include "QxTestingTransport.h"
#include "QxTestingTables.h"

// US-134: detection of the Q* protocol as subdriver_matcher() of nutdrv_qx.c:4321-4373,
// one claim per step. Each fake UPS answers with the testing_t table of its protocol and,
// for the commands it does not know, with the rejected string of that protocol or the echo
// of the command (TESTING branch of qx_command()). The number of steps measures the order
// and QX_MAXTRIES together: every protocol before the right one costs 3 steps.

// Counts the allocations of a QxEngine (libstdc++ linked statically, US-073: see
// test_nut_table), to check that the probing allocates one engine per attach
static size_t g_engineAllocs = 0;
void* operator new(size_t n) {
    if (n == sizeof(QxEngine)) g_engineAllocs++;
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }

void setUp(void) {}
void tearDown(void) {}

struct LogLine {
    std::string level;
    std::string text;
};

static void collect(void* ctx, const char* level, const char* text) {
    static_cast<std::vector<LogLine>*>(ctx)->push_back({ level, text });
}

struct Probe {
    QxTestingTransport t;
    std::vector<LogLine> logs;
    QxProber p;
    UPSData d;
    Probe(const QxTesting* table, const char* rejected = nullptr)
        : t(table, rejected), p(t, QX_SUBDRIVER_LIST, collect, &logs) {}

    // Calls step() while it returns Probing; checks that no step sends more than 2 commands
    QxProber::Step run(uint32_t now, int* steps, int maxSteps = 100) {
        QxProber::Step s;
        int n = 0;
        do {
            size_t before = t.sent.size();
            s = p.step(d, now);
            n++;
            TEST_ASSERT_TRUE_MESSAGE(t.sent.size() - before <= 2, "more than one claim in a step");
        } while (s == QxProber::Step::Probing && n < maxSteps);
        if (steps) *steps = n;
        return s;
    }
};

static void assertSelects(const QxTesting* table, const char* rejected, int expectedSteps, const char* expected) {
    Probe pr(table, rejected);
    int steps = 0;
    TEST_ASSERT_TRUE_MESSAGE(pr.run(0, &steps) == QxProber::Step::Claimed, expected);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expectedSteps, steps, expected);
    TEST_ASSERT_NOT_NULL(pr.p.claimed());
    TEST_ASSERT_EQUAL_STRING(expected, pr.p.claimed()->name);
}

// --- Choice of the protocol for each kind of UPS -----------------------------------------

void test_selects_mustek(void) { assertSelects(MUSTEK_TESTING, nullptr, 1, "Mustek 0.08"); }
void test_selects_megatec_old(void) { assertSelects(MEGATEC_OLD_TESTING, nullptr, 4, "Megatec/old 0.08"); }
void test_selects_megatec(void) { assertSelects(MEGATEC_TESTING, nullptr, 7, "Megatec 0.10"); }
// megatec asks I and gets the echo: 3 failed tries, then zinto with Q1 + FW?
void test_selects_zinto(void) { assertSelects(ZINTO_TESTING, nullptr, 10, "Zinto 0.07"); }
// megatec fails on the 37-byte I answer, zinto on the NAK to FW?
void test_selects_innovatae(void) { assertSelects(INNOVATAE_TESTING, "NAK\r", 13, "INNOVATAE 0.01"); }

// A unit that answers only Q1: innovatae claims with blazer_claim_light (Q1 only) and comes
// before the q1 fallback in subdriver_list[] (nutdrv_qx.c:111-116), so NUT picks innovatae
// too; q1 is reached only with protocol = q1
void test_q1_only_ups_is_innovatae_as_nut(void) {
    assertSelects(Q1_TESTING, nullptr, 13, "INNOVATAE 0.01");
}

// MAXTRIES inside a protocol: the first two D queries fail, the third one is claimed
void test_claim_on_third_try(void) {
    Probe pr(MEGATEC_OLD_TESTING);
    pr.t.failNext(5); // 3 x QS of mustek + 2 x D
    int steps = 0;
    TEST_ASSERT_TRUE(pr.run(0, &steps) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_INT(6, steps);
    TEST_ASSERT_EQUAL_STRING("Megatec/old 0.08", pr.p.claimed()->name);
}

void test_tries_and_trying_progress(void) {
    Probe pr(MEGATEC_TESTING);
    TEST_ASSERT_EQUAL_STRING("Mustek 0.08", pr.p.trying()->name);
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 0) == QxProber::Step::Probing);
    TEST_ASSERT_EQUAL_INT(1, pr.p.tryCount());
    pr.p.step(pr.d, 0);
    pr.p.step(pr.d, 0);
    TEST_ASSERT_EQUAL_STRING("Megatec/old 0.08", pr.p.trying()->name);
    TEST_ASSERT_EQUAL_INT(0, pr.p.tryCount());
}

// --- Claim -------------------------------------------------------------------------------

void test_info_line_on_claim_then_idle(void) {
    Probe pr(MEGATEC_TESTING);
    TEST_ASSERT_TRUE(pr.run(0, nullptr) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_UINT(1, pr.logs.size());
    TEST_ASSERT_EQUAL_STRING("INFO", pr.logs[0].level.c_str());
    TEST_ASSERT_EQUAL_STRING("[Q*] Using protocol: Megatec 0.10", pr.logs[0].text.c_str());
    TEST_ASSERT_NULL(pr.p.trying());
    size_t sent = pr.t.sent.size();
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 1000) == QxProber::Step::Idle);
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 999999) == QxProber::Step::Idle);
    TEST_ASSERT_EQUAL_UINT(sent, pr.t.sent.size());
    TEST_ASSERT_EQUAL_UINT(1, pr.logs.size());
}

void test_taken_engine_completes_start(void) {
    Probe pr(MEGATEC_TESTING);
    TEST_ASSERT_NULL(pr.p.takeEngine().get()); // nothing before the claim
    TEST_ASSERT_TRUE(pr.run(0, nullptr) == QxProber::Step::Claimed);
    std::unique_ptr<QxEngine> e = pr.p.takeEngine();
    TEST_ASSERT_NOT_NULL(e.get());
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", e->subdriver().name);
    e->initUps();
    TEST_ASSERT_TRUE(e->initInfo(pr.d, 0));
    TEST_ASSERT_TRUE(e->updateInfo(pr.d, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", pr.d.find("driver.version.data")->c_str());
    TEST_ASSERT_EQUAL_STRING("TESTING", pr.d.find("ups.firmware")->c_str());
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(pr.d).c_str());
    // Taken once; the claimed protocol stays known
    TEST_ASSERT_NULL(pr.p.takeEngine().get());
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", pr.p.claimed()->name);
}

// --- No answer: one line, then spaced cycles ----------------------------------------------

static void assertSingleNotSupportedLine(const Probe& pr, const char* fragment) {
    TEST_ASSERT_EQUAL_UINT(1, pr.logs.size());
    TEST_ASSERT_EQUAL_STRING("ERROR", pr.logs[0].level.c_str());
    TEST_ASSERT_NOT_NULL(strstr(pr.logs[0].text.c_str(), "Device not supported!"));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(pr.logs[0].text.c_str(), fragment), pr.logs[0].text.c_str());
}

void test_no_answer_backoff(void) {
    Probe pr(MEGATEC_TESTING);
    pr.t.failNext(1000000);
    int steps = 0;
    uint32_t now = 5000;
    TEST_ASSERT_TRUE(pr.run(now, &steps) == QxProber::Step::NotSupported);
    TEST_ASSERT_EQUAL_INT(18, steps); // 6 protocols x QX_MAXTRIES
    TEST_ASSERT_EQUAL_UINT(18, pr.t.sent.size());
    assertSingleNotSupportedLine(pr, "No protocol claimed the UPS, next probe in 30 s");
    TEST_ASSERT_EQUAL_UINT32(30000, pr.p.retryDelayMs());
    TEST_ASSERT_EQUAL_UINT(1, pr.p.failedCycles());
    TEST_ASSERT_NULL(pr.p.trying());

    // Waiting: nothing sent until the delay is over
    TEST_ASSERT_TRUE(pr.p.step(pr.d, now + 1) == QxProber::Step::Waiting);
    TEST_ASSERT_TRUE(pr.p.step(pr.d, now + 29999) == QxProber::Step::Waiting);
    TEST_ASSERT_EQUAL_UINT(18, pr.t.sent.size());

    // Each later cycle doubles the delay up to 10 minutes, without new log lines
    const uint32_t delays[] = { 60000, 120000, 240000, 480000, 600000, 600000 };
    uint32_t delay = 30000;
    for (uint32_t next : delays) {
        now += delay;
        size_t sent = pr.t.sent.size();
        TEST_ASSERT_TRUE(pr.p.step(pr.d, now) == QxProber::Step::Probing);
        TEST_ASSERT_EQUAL_UINT(sent + 1, pr.t.sent.size());
        TEST_ASSERT_TRUE(pr.run(now, &steps) == QxProber::Step::NotSupported);
        TEST_ASSERT_EQUAL_INT(17, steps);
        TEST_ASSERT_EQUAL_UINT32(next, pr.p.retryDelayMs());
        TEST_ASSERT_EQUAL_UINT32(now + next, pr.p.nextProbeAtMs());
        TEST_ASSERT_TRUE(pr.p.step(pr.d, now + next - 1) == QxProber::Step::Waiting);
        delay = next;
    }
    TEST_ASSERT_EQUAL_UINT(7, pr.p.failedCycles());
    TEST_ASSERT_EQUAL_UINT(1, pr.logs.size());
}

void test_restart_resets(void) {
    Probe pr(MEGATEC_TESTING);
    pr.t.failNext(1000000);
    TEST_ASSERT_TRUE(pr.run(0, nullptr) == QxProber::Step::NotSupported);
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 100) == QxProber::Step::Waiting);

    pr.p.restart();
    TEST_ASSERT_EQUAL_UINT32(0, pr.p.retryDelayMs());
    TEST_ASSERT_EQUAL_UINT(0, pr.p.failedCycles());
    TEST_ASSERT_EQUAL_STRING("Mustek 0.08", pr.p.trying()->name);
    size_t sent = pr.t.sent.size();
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 200) == QxProber::Step::Probing);
    TEST_ASSERT_EQUAL_UINT(sent + 1, pr.t.sent.size());
    TEST_ASSERT_TRUE(pr.run(200, nullptr) == QxProber::Step::NotSupported);
    // A new attach logs again, with the first delay
    TEST_ASSERT_EQUAL_UINT(2, pr.logs.size());
    TEST_ASSERT_NOT_NULL(strstr(pr.logs[1].text.c_str(), "next probe in 30 s"));

    // And a later claim works after the restart
    pr.t.failNext(0);
    pr.p.restart();
    TEST_ASSERT_TRUE(pr.run(300, nullptr) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", pr.p.claimed()->name);
}

void test_backoff_across_uint32_wrap(void) {
    Probe pr(MEGATEC_TESTING);
    pr.t.failNext(1000000);
    const uint32_t start = 0xFFFFF000UL;
    TEST_ASSERT_TRUE(pr.run(start, nullptr) == QxProber::Step::NotSupported);
    TEST_ASSERT_EQUAL_UINT32(start + 30000UL, pr.p.nextProbeAtMs()); // wrapped
    TEST_ASSERT_TRUE(pr.p.step(pr.d, start + 4096UL) == QxProber::Step::Waiting); // past zero
    TEST_ASSERT_TRUE(pr.p.step(pr.d, start + 29999UL) == QxProber::Step::Waiting);
    TEST_ASSERT_TRUE(pr.p.step(pr.d, start + 30000UL) == QxProber::Step::Probing);
}

// --- Forced protocol (protocol of ups.conf, US-138) ---------------------------------------

void test_forced_protocol_only(void) {
    Probe pr(MEGATEC_TESTING); // answers Q1 and I, not FW?
    pr.p.configure("zinto", QxOptions());
    TEST_ASSERT_EQUAL_STRING("zinto", pr.p.protocol());
    TEST_ASSERT_EQUAL_STRING("Zinto 0.07", pr.p.trying()->name);
    int steps = 0;
    TEST_ASSERT_TRUE(pr.run(0, &steps) == QxProber::Step::NotSupported);
    TEST_ASSERT_EQUAL_INT(3, steps);
    TEST_ASSERT_EQUAL_UINT(6, pr.t.sent.size());
    for (const auto& s : pr.t.sent) TEST_ASSERT_TRUE(s == "Q1\r" || s == "FW?\r");
    assertSingleNotSupportedLine(pr, "Protocol zinto did not claim the UPS, next probe in 30 s");
    // The next cycle tries zinto again, and only zinto
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 30000) == QxProber::Step::Probing);
    TEST_ASSERT_EQUAL_STRING("Zinto 0.07", pr.p.trying()->name);
}

void test_forced_q1_reachable(void) {
    Probe pr(Q1_TESTING);
    pr.p.configure("Q1", QxOptions());
    int steps = 0;
    TEST_ASSERT_TRUE(pr.run(0, &steps) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_INT(1, steps);
    TEST_ASSERT_EQUAL_STRING("Q1 0.08", pr.p.claimed()->name);
}

void test_forced_name_like_ups_conf(void) {
    Probe pr(MEGATEC_OLD_TESTING);
    pr.p.configure("MEGATEC/OLD", QxOptions());
    int steps = 0;
    TEST_ASSERT_TRUE(pr.run(0, &steps) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_INT(1, steps);
    TEST_ASSERT_EQUAL_STRING("Megatec/old 0.08", pr.p.claimed()->name);

    // The file name is not a protocol name
    Probe wrong(MEGATEC_OLD_TESTING);
    wrong.p.configure("megatec-old", QxOptions());
    TEST_ASSERT_TRUE(wrong.p.step(wrong.d, 0) == QxProber::Step::NotSupported);
    TEST_ASSERT_EQUAL_UINT(0, wrong.t.sent.size());
}

// A protocol not ported (hunnox before US-138): no command, back-off as for a failed cycle
void test_forced_unknown_protocol(void) {
    Probe pr(MEGATEC_TESTING);
    pr.p.configure("hunnox", QxOptions());
    TEST_ASSERT_NULL(pr.p.trying());
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 0) == QxProber::Step::NotSupported);
    TEST_ASSERT_EQUAL_UINT(0, pr.t.sent.size());
    TEST_ASSERT_EQUAL_UINT32(30000, pr.p.retryDelayMs());
    assertSingleNotSupportedLine(pr, "Protocol hunnox did not claim the UPS");
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 1000) == QxProber::Step::Waiting);
}

// Back to automatic: "" (or nullptr) tries the whole list again
void test_configure_back_to_automatic(void) {
    Probe pr(MEGATEC_TESTING);
    pr.p.configure("zinto", QxOptions());
    TEST_ASSERT_TRUE(pr.run(0, nullptr) == QxProber::Step::NotSupported);
    pr.p.configure(nullptr, QxOptions());
    TEST_ASSERT_EQUAL_STRING("", pr.p.protocol());
    TEST_ASSERT_TRUE(pr.run(10, nullptr) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", pr.p.claimed()->name);
}

// novendor reaches the claim: blazer_claim stops after the status query
void test_novendor_reaches_claim(void) {
    Probe pr(MEGATEC_NO_VENDOR);
    QxOptions opt;
    opt.novendor = true;
    pr.p.configure("megatec", opt);
    int steps = 0;
    TEST_ASSERT_TRUE(pr.run(0, &steps) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_INT(1, steps);
    TEST_ASSERT_EQUAL_UINT(1, pr.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("Q1\r", pr.t.sent[0].c_str());
}

// A long forced name is truncated, never overflows, and simply matches nothing
void test_forced_name_truncated(void) {
    Probe pr(MEGATEC_TESTING);
    pr.p.configure("megatec_with_a_very_long_name_that_does_not_fit", QxOptions());
    TEST_ASSERT_EQUAL_UINT(QxProber::PROTOCOL_MAX - 1, strlen(pr.p.protocol()));
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 0) == QxProber::Step::NotSupported);
}

// --- Memory: one engine per attach, reset for the other protocols -------------------------

void test_one_engine_allocation_per_attach(void) {
    Probe pr(MEGATEC_TESTING);
    pr.t.failNext(1000000);
    size_t before = g_engineAllocs;
    TEST_ASSERT_TRUE(pr.run(0, nullptr) == QxProber::Step::NotSupported); // 6 protocols
    TEST_ASSERT_EQUAL_UINT(before + 1, g_engineAllocs);
    // Later cycles and a restart reuse it
    TEST_ASSERT_TRUE(pr.p.step(pr.d, 30000) == QxProber::Step::Probing);
    TEST_ASSERT_TRUE(pr.run(30000, nullptr) == QxProber::Step::NotSupported);
    pr.p.restart();
    pr.t.failNext(0);
    TEST_ASSERT_TRUE(pr.run(100000, nullptr) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_UINT(before + 1, g_engineAllocs);
    // The taken engine leaves with the driver: the next attach allocates a new one
    std::unique_ptr<QxEngine> e = pr.p.takeEngine();
    pr.p.restart();
    TEST_ASSERT_TRUE(pr.run(200000, nullptr) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_UINT(before + 2, g_engineAllocs);
}

// The engine reused across protocols starts each one from a clean state: the claim of a
// protocol is not affected by the walk and the failures of the previous one
void test_reused_engine_state_is_fresh(void) {
    Probe pr(ZINTO_TESTING); // megatec fails 3 times on I, then zinto claims
    int steps = 0;
    TEST_ASSERT_TRUE(pr.run(0, &steps) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_INT(10, steps);
    std::unique_ptr<QxEngine> e = pr.p.takeEngine();
    TEST_ASSERT_EQUAL_STRING("Zinto 0.07", e->subdriver().name);
    TEST_ASSERT_EQUAL_UINT(34, e->rowCount());
    // The table flags, plus QX_FLAG_MAPPING_HANDLED only on the two rows of the zinto claim
    for (size_t r = 0; r < e->rowCount(); r++) {
        const QxItem& it = QX_ZINTO.qx2nut[r];
        bool claimRow = &it == &QX_ZINTO.qx2nut[e->findNutInfo("input.voltage", 0, 0)] ||
                        &it == &QX_ZINTO.qx2nut[e->findNutInfo("ups.firmware", 0, 0)];
        uint32_t expected = it.qxflags | (claimRow ? QX_FLAG_MAPPING_HANDLED : 0);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, e->flags((int)r), it.info_type);
    }
    TEST_ASSERT_EQUAL_UINT(0, e->status());
    e->initUps();
    TEST_ASSERT_TRUE(e->initInfo(pr.d, 0));
    TEST_ASSERT_TRUE(e->updateInfo(pr.d, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("Zinto 0.07", pr.d.find("driver.version.data")->c_str());
    TEST_ASSERT_EQUAL_STRING("NOT_A_LIVE_UPS", pr.d.find("device.mfr")->c_str());
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(pr.d).c_str());
}

// QxEngine::reset() gives the same engine as the constructor, options included
void test_engine_reset_equals_new_engine(void) {
    QxTestingTransport t(MEGATEC_TESTING);
    QxEngine used(QX_MEGATEC, t);
    UPSData d1;
    TEST_ASSERT_TRUE(used.claim(d1));
    used.initUps();
    TEST_ASSERT_TRUE(used.initInfo(d1, 0));
    // Status on battery, then failures: status bits, retries and work buffers in use
    t.override("Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 11000010\r");
    TEST_ASSERT_TRUE(used.updateInfo(d1, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_TRUE(used.status() != 0);
    t.failNext(3);
    used.updateInfo(d1, 4000);
    used.updateInfo(d1, 6000);
    t.failNext(0);
    t.override("Q1\r", nullptr);

    QxOptions opt;
    opt.novendor = true;
    used.reset(QX_Q1, opt);
    QxTestingTransport t2(MEGATEC_TESTING);
    QxEngine fresh(QX_Q1, t2, opt);
    TEST_ASSERT_EQUAL_STRING("Q1 0.08", used.subdriver().name);
    TEST_ASSERT_TRUE(used.options().novendor);
    TEST_ASSERT_EQUAL_UINT(fresh.rowCount(), used.rowCount());
    for (size_t r = 0; r < fresh.rowCount(); r++) TEST_ASSERT_EQUAL_UINT32(fresh.flags((int)r), used.flags((int)r));
    TEST_ASSERT_EQUAL_UINT(0, used.status());
    TEST_ASSERT_FALSE(used.reestablished());

    // Same results on the same answers
    UPSData a, b;
    TEST_ASSERT_TRUE(used.claim(a));
    TEST_ASSERT_TRUE(fresh.claim(b));
    used.initUps();
    fresh.initUps();
    TEST_ASSERT_TRUE(used.initInfo(a, 0));
    TEST_ASSERT_TRUE(fresh.initInfo(b, 0));
    TEST_ASSERT_TRUE(used.updateInfo(a, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_TRUE(fresh.updateInfo(b, 2000) == QxEngine::Update::Ok);
    for (const char* key : { "driver.version.data", "input.voltage", "output.voltage", "ups.load",
                             "battery.voltage", "ups.type", "ups.beeper.status", "ups.delay.start" }) {
        TEST_ASSERT_NOT_NULL_MESSAGE(b.find(key), key);
        TEST_ASSERT_NOT_NULL_MESSAGE(a.find(key), key);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(b.find(key)->c_str(), a.find(key)->c_str(), key);
    }
    TEST_ASSERT_EQUAL_STRING(UPSData::computeUPSStatusString(b).c_str(), UPSData::computeUPSStatusString(a).c_str());
    TEST_ASSERT_EQUAL_STRING("OL", UPSData::computeUPSStatusString(a).c_str());
    TEST_ASSERT_NULL(a.find("ups.alarm"));
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_selects_mustek);
    RUN_TEST(test_selects_megatec_old);
    RUN_TEST(test_selects_megatec);
    RUN_TEST(test_selects_zinto);
    RUN_TEST(test_selects_innovatae);
    RUN_TEST(test_q1_only_ups_is_innovatae_as_nut);
    RUN_TEST(test_claim_on_third_try);
    RUN_TEST(test_tries_and_trying_progress);
    RUN_TEST(test_info_line_on_claim_then_idle);
    RUN_TEST(test_taken_engine_completes_start);
    RUN_TEST(test_no_answer_backoff);
    RUN_TEST(test_restart_resets);
    RUN_TEST(test_backoff_across_uint32_wrap);
    RUN_TEST(test_forced_protocol_only);
    RUN_TEST(test_forced_q1_reachable);
    RUN_TEST(test_forced_name_like_ups_conf);
    RUN_TEST(test_forced_unknown_protocol);
    RUN_TEST(test_configure_back_to_automatic);
    RUN_TEST(test_novendor_reaches_claim);
    RUN_TEST(test_forced_name_truncated);
    RUN_TEST(test_one_engine_allocation_per_attach);
    RUN_TEST(test_reused_engine_state_is_fresh);
    RUN_TEST(test_engine_reset_equals_new_engine);
    return UNITY_END();
}
#endif
#endif
