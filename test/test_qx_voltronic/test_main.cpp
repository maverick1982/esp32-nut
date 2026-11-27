#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "QxEngine.h"
#include "QxProbe.h"
#include "QxProtocols.h"
#include "QxTestingTransport.h"
#include "QxTestingTables.h"

// ADR 0014: the Voltronic Q* protocol (nut_repo/drivers/nutdrv_qx_voltronic.c) replayed with
// its upstream testing table (VOLTRONIC_TESTING, test/mocks/QxTestingTables.h) in the order
// of nutdrv_qx: claim -> initups -> initinfo -> updateinfo, then instcmd/setvar. Expected
// values come from the from/to positions of voltronic_qx2nut[] on the testing answers.

void setUp(void) {}
void tearDown(void) {}

static const char* NAK = "(NAK\r";

struct Run {
    QxTestingTransport t;
    QxEngine e;
    UPSData d;
    explicit Run(const QxTesting* table = VOLTRONIC_TESTING) : t(table, QX_VOLTRONIC.rejected), e(QX_VOLTRONIC, t) {}
    // claim -> initups -> initinfo (t = 0 ms) -> first updateinfo (t = 2 000 ms, quick)
    bool start() {
        if (!e.claim(d)) return false;
        e.initUps();
        if (!e.initInfo(d, 0)) return false;
        return e.updateInfo(d, 2000) == QxEngine::Update::Ok;
    }
    // A full update (pollfreq elapsed)
    bool full(uint32_t now = 40000) { return e.updateInfo(d, now) == QxEngine::Update::Ok; }
    const char* get(const char* key) {
        const String* v = d.find(key);
        return v ? v->c_str() : "(missing)";
    }
    String status() const { return UPSData::computeUPSStatusString(d); }
    bool sentCmd(const char* cmd) const {
        for (const auto& s : t.sent) {
            if (s == cmd) return true;
        }
        return false;
    }
    const char* last() const { return t.sent.empty() ? "" : t.sent.back().c_str(); }
    int rowOf(const char* name, uint32_t flag) const { return e.findNutInfo(name, flag, 0); }
};

// The QGS answer of the testing table with the 12 status bits replaced
static const char* qgs(const char* bits, const char* vin = "234.9", const char* vout = "229.8") {
    static char buf[96];
    snprintf(buf, sizeof(buf), "(%s 50.0 %s 50.0 000.0 00A 369.1 ---.- 026.5 ---.- 018.8 %s\r", vin, vout, bits);
    return buf;
}

// A QWS answer with the given bit indexes (0-based) set
static const char* qws(const std::vector<int>& bits) {
    static char buf[80];
    char v[65];
    memset(v, '0', 64);
    v[64] = '\0';
    for (int b : bits) v[b] = '1';
    snprintf(buf, sizeof(buf), "(%s\r", v);
    return buf;
}

// --- Claim and probing order -------------------------------------------------------------

void test_claim_sends_qgs_then_qpi(void) {
    Run r;
    TEST_ASSERT_TRUE(r.e.claim(r.d));
    TEST_ASSERT_EQUAL_UINT(2, r.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("QGS\r", r.t.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("QPI\r", r.t.sent[1].c_str());
    TEST_ASSERT_EQUAL_STRING("234.9", r.get("input.voltage"));
    TEST_ASSERT_EQUAL_STRING("P01", r.get("ups.firmware.aux"));
}

void test_claim_fails_on_unsupported_protocol_and_drops_input_voltage(void) {
    const char* answers[] = { "(PI05\r", "(PI04\r", "(PI11\r", "(XX01\r", "(PI1A\r" };
    for (const char* a : answers) {
        Run r;
        r.t.override("QPI\r", a);
        TEST_ASSERT_FALSE_MESSAGE(r.e.claim(r.d), a);
        TEST_ASSERT_NULL_MESSAGE(r.d.find("input.voltage"), a);
    }
    // The accepted protocols (voltronic_protocol(), 2856-2871)
    const char* ok[] = { "(PI00\r", "(PI08\r", "(PI13\r", "(PI14\r", "(PI71\r", "(PI99\r" };
    for (const char* a : ok) {
        Run r;
        r.t.override("QPI\r", a);
        TEST_ASSERT_TRUE_MESSAGE(r.e.claim(r.d), a);
    }
}

// The APC SRV2KI-E: every legacy probe (QS, D, Q1, ...) answers "(NAK\r". It cannot claim
// any other protocol, and Voltronic comes first in the list anyway
void test_nak_answers_claim_no_legacy_protocol(void) {
    for (const QxSubdriver* const* s = QX_SUBDRIVER_LIST; *s; s++) {
        QxTestingTransport t(nullptr, NAK); // NAK to every command
        QxEngine e(**s, t);
        UPSData d;
        TEST_ASSERT_FALSE_MESSAGE(e.claim(d), (*s)->name);
        TEST_ASSERT_FALSE_MESSAGE(e.timedOut(), (*s)->name); // a rejected query is EINVAL
    }
}

void test_probing_claims_voltronic_first(void) {
    QxTestingTransport t(VOLTRONIC_TESTING, NAK);
    QxProber p(t);
    UPSData d;
    TEST_ASSERT_TRUE(QX_SUBDRIVER_LIST[0] == &QX_VOLTRONIC);
    TEST_ASSERT_TRUE(p.step(d, 0) == QxProber::Step::Claimed);
    TEST_ASSERT_TRUE(p.claimed() == &QX_VOLTRONIC);
    TEST_ASSERT_EQUAL_UINT(2, t.sent.size());
    TEST_ASSERT_EQUAL_STRING("QGS\r", t.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("QPI\r", t.sent[1].c_str());
}

// A megatec UPS echoes QGS: Voltronic fails its 3 tries without sleeping, megatec claims later
void test_probing_megatec_ups_passes_voltronic(void) {
    QxTestingTransport t(MEGATEC_TESTING, nullptr);
    QxProber p(t);
    int sleeps = 0;
    p.setSleep([](void* ctx, uint32_t) { (*static_cast<int*>(ctx))++; }, &sleeps);
    UPSData d;
    QxProber::Step s;
    int steps = 0;
    do {
        s = p.step(d, 0);
        steps++;
    } while (s == QxProber::Step::Probing && steps < 100);
    TEST_ASSERT_TRUE(s == QxProber::Step::Claimed);
    TEST_ASSERT_TRUE(p.claimed() == &QX_MEGATEC);
    TEST_ASSERT_EQUAL_STRING("QGS\r", t.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("QGS\r", t.sent[1].c_str());
    TEST_ASSERT_EQUAL_STRING("QGS\r", t.sent[2].c_str());
    TEST_ASSERT_EQUAL_INT(0, sleeps);
}

// A UPS that answers QGS late (zero-length read: errno ETIMEDOUT upstream)
struct LaggyTransport : public IQxTransport {
    QxTestingTransport inner{ VOLTRONIC_TESTING, "(NAK\r" };
    int silentQgs = 0;
    int command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) override {
        if (strcmp(cmd, "QGS\r") == 0 && silentQgs > 0) {
            silentQgs--;
            inner.sent.push_back(cmd);
            return 0;
        }
        return inner.command(cmd, cmdlen, buf, buflen);
    }
};

static void recordSleep(void* ctx, uint32_t ms) {
    static_cast<std::vector<uint32_t>*>(ctx)->push_back(ms);
}

void test_claim_timeout_sleeps_5s_and_asks_again(void) {
    LaggyTransport t;
    t.silentQgs = 1;
    QxEngine e(QX_VOLTRONIC, t);
    std::vector<uint32_t> sleeps;
    e.setSleep(recordSleep, &sleeps);
    UPSData d;
    TEST_ASSERT_TRUE(e.claim(d));
    TEST_ASSERT_EQUAL_UINT(1, sleeps.size());
    TEST_ASSERT_EQUAL_UINT32(5000, sleeps[0]);
    TEST_ASSERT_EQUAL_UINT(3, t.inner.sent.size());
    TEST_ASSERT_EQUAL_STRING("QGS\r", t.inner.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("QGS\r", t.inner.sent[1].c_str());
    TEST_ASSERT_EQUAL_STRING("QPI\r", t.inner.sent[2].c_str());

    // Still silent after the sleep: no claim, one sleep only
    LaggyTransport t2;
    t2.silentQgs = 2;
    QxEngine e2(QX_VOLTRONIC, t2);
    sleeps.clear();
    e2.setSleep(recordSleep, &sleeps);
    TEST_ASSERT_FALSE(e2.claim(d));
    TEST_ASSERT_EQUAL_UINT(1, sleeps.size());
    TEST_ASSERT_EQUAL_UINT(2, t2.inner.sent.size());

    // A transport error is not a timeout: no sleep, no retry (and the engine sleeps nowhere
    // without a sleeper)
    QxTestingTransport t3(VOLTRONIC_TESTING, NAK);
    t3.failNext(1);
    QxEngine e3(QX_VOLTRONIC, t3);
    sleeps.clear();
    e3.setSleep(recordSleep, &sleeps);
    TEST_ASSERT_FALSE(e3.claim(d));
    TEST_ASSERT_EQUAL_UINT(0, sleeps.size());
    TEST_ASSERT_EQUAL_UINT(1, t3.sent.size());
}

// The prober hands its sleeper to the engines it creates
void test_prober_passes_the_sleeper(void) {
    LaggyTransport t;
    t.silentQgs = 1;
    QxProber p(t);
    std::vector<uint32_t> sleeps;
    p.setSleep(recordSleep, &sleeps);
    UPSData d;
    TEST_ASSERT_TRUE(p.step(d, 0) == QxProber::Step::Claimed);
    TEST_ASSERT_EQUAL_UINT(1, sleeps.size());
    TEST_ASSERT_EQUAL_UINT32(5000, sleeps[0]);
}

// --- initinfo + update walk with the testing table ----------------------------------------

void test_testing_values(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("Voltronic 0.15", r.get("driver.version.data"));
    TEST_ASSERT_EQUAL_STRING("P01", r.get("ups.firmware.aux"));
    // (234.9 50.0 229.8 50.0 000.0 00A 369.1 ---.- 026.5 ---.- 018.8 100000000001\r
    //  0123456789012345678901234567890123456789012345678901234567890123456789012345
    TEST_ASSERT_EQUAL_STRING("234.9", r.get("input.voltage"));    // 1-5
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("input.frequency"));   // 7-10
    TEST_ASSERT_EQUAL_STRING("229.8", r.get("output.voltage"));   // 12-16
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("output.frequency"));  // 18-21
    TEST_ASSERT_EQUAL_STRING("0.0", r.get("output.current"));     // 23-27 "000.0"
    TEST_ASSERT_NULL(r.d.find("ups.load"));                       // 29-31 "00A": non numerical
    TEST_ASSERT_EQUAL_STRING("026.5", r.get("battery.voltage"));  // 45-49, qx_multiply_battvolt passes it on
    TEST_ASSERT_EQUAL_STRING("18.8", r.get("ups.temperature"));   // 57-61
    TEST_ASSERT_EQUAL_STRING("online", r.get("ups.type"));        // 63-64 "10"
    TEST_ASSERT_EQUAL_STRING("enabled", r.get("ups.beeper.status")); // 72 '0'
    // (230.0 004 024.0 50.0\r
    TEST_ASSERT_EQUAL_STRING("230.0", r.get("output.voltage.nominal"));
    TEST_ASSERT_EQUAL_STRING("4", r.get("output.current.nominal"));
    TEST_ASSERT_EQUAL_STRING("24.0", r.get("battery.voltage.nominal"));
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("output.frequency.nominal"));
    // (#######OLHVT1K0 ###1000 80 2/2 230 230 02 12.0\r
    TEST_ASSERT_EQUAL_STRING("OLHVT1K0", r.get("device.model"));     // 1-15 trimmed
    TEST_ASSERT_EQUAL_STRING("1000", r.get("ups.power.nominal"));    // 17-23 trimmed
    TEST_ASSERT_EQUAL_STRING("0.8", r.get("output.powerfactor"));    // 25-26 "80" * 0.01
    TEST_ASSERT_EQUAL_STRING("2", r.get("input.phases"));            // 28
    TEST_ASSERT_EQUAL_STRING("2", r.get("output.phases"));           // 30
    TEST_ASSERT_EQUAL_STRING("230.0", r.get("input.voltage.nominal")); // 32-34, before F
    // #220.0 000 024.0 50.0\r: only what QRI/QMD did not give
    TEST_ASSERT_EQUAL_STRING("0.0", r.get("input.current.nominal"));
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("input.frequency.nominal"));
    TEST_ASSERT_EQUAL_STRING("VOLTRONIC", r.get("device.mfr"));      // QMF, before I
    TEST_ASSERT_EQUAL_STRING("00322.02", r.get("ups.firmware"));     // QVFW 7-14
    TEST_ASSERT_EQUAL_STRING("685653211455", r.get("device.serial")); // QID
    // (026.5 02 01 068 255\r
    TEST_ASSERT_EQUAL_STRING("1", r.get("battery.packs"));
    TEST_ASSERT_EQUAL_STRING("68", r.get("battery.charge"));
    TEST_ASSERT_EQUAL_STRING("15300", r.get("battery.runtime")); // 255 min
    // (005 080\r
    TEST_ASSERT_EQUAL_STRING("5", r.get("output.power.minimum.percent"));
    TEST_ASSERT_EQUAL_STRING("80", r.get("output.power.maximum.percent"));
    // RE0 "#20" then the guesstimation of qx_initbattery() (no battery.voltage.high):
    // 104*24/120 and 130*24/120
    TEST_ASSERT_EQUAL_STRING("20.80", r.get("battery.voltage.low"));
    TEST_ASSERT_EQUAL_STRING("26.00", r.get("battery.voltage.high"));
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start"));
    TEST_ASSERT_EQUAL_STRING("30", r.get("ups.delay.shutdown"));
    // NONUT rows are not published
    const char* nonut[] = { "battery_number", "bypass_alarm", "alarm_control", "eco_mode", "max_bypass_volt",
                            "min_bypass_freq", "input_phase_angle", "output_phase_angle", "voltronic_parallel" };
    for (const char* k : nonut) TEST_ASSERT_NULL_MESSAGE(r.d.find(k), k);
    // Multiphase rows (P09/P10), P31 rows, QFS and QBDR stay skipped for P01; I is not
    // needed (QMF/QMD/QVFW came first)
    const char* notSent[] = { "Q3PV\r", "Q3OV\r", "Q3PC\r", "Q3OC\r", "Q3LD\r", "Q3YV\r", "QBT\r", "QGR\r",
                              "QFS\r", "QBDR\r", "I\r", "QSKT4\r" };
    for (const char* c : notSent) TEST_ASSERT_FALSE_MESSAGE(r.sentCmd(c), c);
    // Bypass limits (all but P00/P08/P31)
    TEST_ASSERT_TRUE(r.sentCmd("QBYV\r"));
    TEST_ASSERT_TRUE(r.sentCmd("QBYF\r"));
    TEST_ASSERT_TRUE(r.sentCmd("QPD\r"));
    TEST_ASSERT_TRUE(r.sentCmd("QPAR\r"));
    // QMOD 'S' (standby): OFF; QGS bit 65 '0': OL
    TEST_ASSERT_EQUAL_STRING("OL OFF", r.status().c_str());
    TEST_ASSERT_NULL(r.d.find("experimental.ups.mode.buzzwords"));
}

// The APC SRV2KI-E on its XR21V1410 bridge (ADR 0014), answers recorded by the USB diagnostics
// export on 2026-11-27 (line mode, output on, no load); the other commands keep the testing
// table. Values as the export showed them on the board
void test_apc_srv2ki_e_answers(void) {
    Run r;
    r.t.override("QGS\r", "(224.3 50.0 229.8 50.0 000.0 000 377.5 376.3 054.4 ---.- 018.7 100000000001\r");
    r.t.override("QMOD\r", "(L\r");
    r.t.override("QWS\r", "(0000000000000000000000000000000000000000000000000000000000000000\r");
    r.t.override("QBV\r", "(054.4 04 01 096 434\r");
    r.t.override("QLDL\r", "(000 000\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_TRUE(r.full());
    TEST_ASSERT_EQUAL_STRING("224.3", r.get("input.voltage"));
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("input.frequency"));
    TEST_ASSERT_EQUAL_STRING("229.8", r.get("output.voltage"));
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("output.frequency"));
    TEST_ASSERT_EQUAL_STRING("0.0", r.get("output.current"));
    TEST_ASSERT_EQUAL_STRING("0", r.get("ups.load"));
    TEST_ASSERT_EQUAL_STRING("054.4", r.get("battery.voltage"));
    TEST_ASSERT_EQUAL_STRING("18.7", r.get("ups.temperature"));
    TEST_ASSERT_EQUAL_STRING("96", r.get("battery.charge"));
    TEST_ASSERT_EQUAL_STRING("26040", r.get("battery.runtime"));
    TEST_ASSERT_EQUAL_STRING("online", r.get("ups.type"));
    TEST_ASSERT_EQUAL_STRING("OL", r.status().c_str());
}

void test_capabilities_of_testing_table(void) {
    // (EpashcjDbroegfl\r: enabled "pashcj", disabled "broegfl" (the leading E is skipped)
    Run r;
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("no", r.get("ups.start.auto"));       // r disabled
    TEST_ASSERT_EQUAL_STRING("yes", r.get("battery.protection"));  // s enabled
    TEST_ASSERT_EQUAL_STRING("no", r.get("battery.energysave"));   // g disabled
    TEST_ASSERT_EQUAL_STRING("yes", r.get("ups.start.battery"));   // c enabled
    TEST_ASSERT_EQUAL_STRING("yes", r.get("outlet.0.switchable")); // j enabled
    // a enabled: alarm control -> beeper.{enable,disable}
    TEST_ASSERT_TRUE(r.e.commandOffered("beeper.enable"));
    TEST_ASSERT_TRUE(r.e.commandOffered("beeper.disable"));
    // e present (disabled): ECO mode -> bypass.{start,stop}, input.transfer.*, input.frequency.*
    TEST_ASSERT_TRUE(r.e.commandOffered("bypass.start"));
    TEST_ASSERT_TRUE(r.e.commandOffered("bypass.stop"));
    // QHE (242 218: P01, output.voltage.nominal 230 >= 200: low 206..223, high 237..254
    TEST_ASSERT_EQUAL_STRING("242", r.get("input.transfer.high"));
    TEST_ASSERT_EQUAL_STRING("218", r.get("input.transfer.low"));
    TEST_ASSERT_EQUAL_STRING("206", r.get("input.transfer.low.min"));
    TEST_ASSERT_EQUAL_STRING("223", r.get("input.transfer.low.max"));
    TEST_ASSERT_EQUAL_STRING("237", r.get("input.transfer.high.min"));
    TEST_ASSERT_EQUAL_STRING("254", r.get("input.transfer.high.max"));
    // QFRE (50.0 00.0
    TEST_ASSERT_EQUAL_STRING("50.0", r.get("input.frequency.high"));
    TEST_ASSERT_EQUAL_STRING("0.0", r.get("input.frequency.low"));
    // j: outlets 1-4 queried; QSK4 NAK -> outlet 4 skipped
    TEST_ASSERT_EQUAL_STRING("yes", r.get("outlet.1.switchable"));
    TEST_ASSERT_EQUAL_STRING("on", r.get("outlet.1.status"));
    TEST_ASSERT_EQUAL_STRING("yes", r.get("outlet.2.switchable"));
    TEST_ASSERT_EQUAL_STRING("off", r.get("outlet.2.status"));
    TEST_ASSERT_EQUAL_STRING("on", r.get("outlet.3.status"));
    TEST_ASSERT_NULL(r.d.find("outlet.4.switchable"));
    TEST_ASSERT_TRUE(r.sentCmd("QSK4\r"));
    // QSKT1 (008, QSKT2 (012 in seconds; QSKT3 NAK
    TEST_ASSERT_EQUAL_STRING("480", r.get("outlet.1.delay.shutdown"));
    TEST_ASSERT_EQUAL_STRING("720", r.get("outlet.2.delay.shutdown"));
    TEST_ASSERT_NULL(r.d.find("outlet.3.delay.shutdown"));
    TEST_ASSERT_TRUE(r.e.commandOffered("outlet.1.load.on"));
    TEST_ASSERT_TRUE(r.e.commandOffered("outlet.3.load.off"));
    TEST_ASSERT_FALSE(r.e.commandOffered("outlet.4.load.on"));
}

void test_capabilities_parsing(void) {
    // Only j enabled and a disabled: no r/s/g/c (not published), no ECO mode
    Run r;
    r.t.override("QFLAG\r", "(EjDa\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("yes", r.get("outlet.0.switchable"));
    TEST_ASSERT_EQUAL_STRING("disabled", r.get("ups.beeper.status")); // alarm control disabled
    TEST_ASSERT_NULL(r.d.find("ups.start.auto"));
    TEST_ASSERT_NULL(r.d.find("battery.protection"));
    TEST_ASSERT_TRUE(r.e.commandOffered("beeper.enable"));
    TEST_ASSERT_FALSE(r.e.commandOffered("bypass.start"));
    TEST_ASSERT_NULL(r.d.find("input.transfer.high"));
    TEST_ASSERT_FALSE(r.sentCmd("QHE\r"));
    TEST_ASSERT_FALSE(r.sentCmd("QFRE\r"));

    // strtok_r() skips a leading delimiter: "(EDa" -> enabled "a", as upstream
    Run r2;
    r2.t.override("QFLAG\r", "(EDa\r");
    TEST_ASSERT_TRUE(r2.start());
    TEST_ASSERT_EQUAL_STRING("enabled", r2.get("ups.beeper.status"));

    // No capability at all: the outlets are not queried
    Run r3;
    r3.t.override("QFLAG\r", "(ED\r");
    TEST_ASSERT_TRUE(r3.start());
    TEST_ASSERT_FALSE(r3.sentCmd("QSK1\r"));
    TEST_ASSERT_FALSE(r3.e.commandOffered("beeper.enable"));
}

void test_eco_volt_range_low_nominal_voltage(void) {
    // P01 with output.voltage.nominal 120 (< 200): low 108..117, high 123..132
    Run r;
    r.t.override("QRI\r", "(120.0 008 024.0 60.0\r");
    r.t.override("QHE\r", "(127 112\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("108", r.get("input.transfer.low.min"));
    TEST_ASSERT_EQUAL_STRING("117", r.get("input.transfer.low.max"));
    TEST_ASSERT_EQUAL_STRING("123", r.get("input.transfer.high.min"));
    TEST_ASSERT_EQUAL_STRING("132", r.get("input.transfer.high.max"));

    // ECO mode not supported by P08: value published, no range, no setvar
    Run r8;
    r8.t.override("QPI\r", "(PI08\r");
    TEST_ASSERT_TRUE(r8.start());
    TEST_ASSERT_EQUAL_STRING("242", r8.get("input.transfer.high"));
    TEST_ASSERT_NULL(r8.d.find("input.transfer.high.min"));
    TEST_ASSERT_TRUE(r8.e.setvar(r8.d, "input.transfer.high", "250") == QxEngine::Setvar::Unknown);
    // P08: no bypass limits
    TEST_ASSERT_FALSE(r8.sentCmd("QBYV\r"));
}

// --- Protocols ----------------------------------------------------------------------------

void test_protocol_p09_unskips_two_phase_rows(void) {
    Run r;
    r.t.override("QPI\r", "(PI09\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("P09", r.get("ups.firmware.aux"));
    // (123.4 456.4 789.4 012.4 323.4 223.4\r
    TEST_ASSERT_EQUAL_STRING("123.4", r.get("input.L1-N.voltage"));
    TEST_ASSERT_EQUAL_STRING("456.4", r.get("input.L2-N.voltage"));
    TEST_ASSERT_EQUAL_STRING("12.4", r.get("input.L1-L2.voltage"));
    TEST_ASSERT_NULL(r.d.find("input.L3-N.voltage"));
    TEST_ASSERT_NULL(r.d.find("input.L2-L3.voltage"));
    TEST_ASSERT_EQUAL_STRING("2", r.get("input.L1.current"));    // (002 023 051
    TEST_ASSERT_EQUAL_STRING("23", r.get("input.L2.current"));
    TEST_ASSERT_NULL(r.d.find("input.L3.current"));
    TEST_ASSERT_EQUAL_STRING("253.4", r.get("output.L1-N.voltage")); // (253.4 163.4 ...
    TEST_ASSERT_EQUAL_STRING("143.4", r.get("output.L1-L2.voltage"));
    TEST_ASSERT_EQUAL_STRING("109", r.get("output.L1.current"));
    TEST_ASSERT_EQUAL_STRING("33", r.get("output.L2.power.percent"));
    TEST_ASSERT_NULL(r.d.find("output.L3.power.percent"));
    // Q3YV rows of P09 (answer_len 19): 1-5 and 7-11
    TEST_ASSERT_EQUAL_STRING("303.4", r.get("output.bypass.L1-N.voltage"));
    TEST_ASSERT_EQUAL_STRING("245.4", r.get("output.bypass.L2-N.voltage"));
    TEST_ASSERT_NULL(r.d.find("output.bypass.L3-N.voltage"));
    TEST_ASSERT_NULL(r.d.find("output.bypass.L1-L2.voltage"));
}

void test_protocol_p10_unskips_three_phase_rows(void) {
    Run r;
    r.t.override("QPI\r", "(PI10\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("789.4", r.get("input.L3-N.voltage"));
    TEST_ASSERT_EQUAL_STRING("323.4", r.get("input.L2-L3.voltage")); // from 25
    TEST_ASSERT_EQUAL_STRING("223.4", r.get("input.L1-L3.voltage")); // from 31
    TEST_ASSERT_EQUAL_STRING("51", r.get("input.L3.current"));
    TEST_ASSERT_EQUAL_STRING("23.4", r.get("output.L3-N.voltage"));
    TEST_ASSERT_EQUAL_STRING("89", r.get("output.L3.power.percent"));
    TEST_ASSERT_EQUAL_STRING("126.4", r.get("output.bypass.L3-N.voltage"));
    TEST_ASSERT_EQUAL_STRING("321.4", r.get("output.bypass.L1-L3.voltage"));
}

void test_protocol_p31_battery_type(void) {
    Run r;
    r.t.override("QPI\r", "(PI31\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("P31", r.get("ups.firmware.aux"));
    TEST_ASSERT_EQUAL_STRING("Flooded", r.get("battery.type")); // (01
    TEST_ASSERT_TRUE(r.sentCmd("QGR\r"));
    TEST_ASSERT_FALSE(r.sentCmd("QBYV\r")); // no bypass limits for P31
    TEST_ASSERT_NULL(r.d.find("work_range_type")); // NONUT
    // battery.type setvar unskipped by the protocol: enum Li/Flooded/AGM -> index
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "battery.type", "AGM") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("PBT02\r", r.last());
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "battery.type", "NiMH") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_STRING("PBT02\r", r.last());
    // work_range_type setvar needs ups.conf (getval()): not offered
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "work_range_type", "UPS") == QxEngine::Setvar::Unknown);
}

// --- Status bits, mode, faults, warnings -------------------------------------------------

void test_status_bits(void) {
    struct Case {
        const char* bits;
        const char* status;
    } cases[] = {
        // bits from position 63: type (2), 65 utility fail, 66 battery low, 67 bypass/boost/buck,
        // 68 fault, 69 unknown, 70 test, 71 shutdown, 72 beeper, 73-74 unknown
        { "100000000000", "OL" },           // all clear
        { "101000000000", "OB" },           // utility fail
        { "101100000000", "OB LB" },        // + battery low
        { "100000010000", "CAL OL" },       // test in progress
        { "100000001000", "ALARM OL FSD" }, // shutdown active (+ "Shutdown imminent!")
        { "100010000000", "OL BYPASS" },    // 229.8/234.9 = 0.98, P01: BYPASS
    };
    for (const auto& c : cases) {
        Run r;
        // An unknown mode: the QMOD rows fail and leave the status of QGS alone
        r.t.override("QMOD\r", "(X\r");
        r.t.override("QWS\r", qws({}));
        TEST_ASSERT_TRUE(r.start());
        r.t.override("QGS\r", qgs(c.bits));
        TEST_ASSERT_TRUE_MESSAGE(r.full(), c.bits);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.status, r.status().c_str(), c.bits);
    }
}

void test_status_bypass_boost_trim_and_avr(void) {
    Run r;
    r.t.override("QMOD\r", "(L\r");
    r.t.override("QWS\r", qws({}));
    TEST_ASSERT_TRUE(r.start());
    r.t.override("QGS\r", qgs("100010000000", "220.0", "240.0")); // 1.09: BOOST
    TEST_ASSERT_TRUE(r.full());
    // voltronic_status() gives TRIM/BOOST to both the ups.status and the ups.alarm rows of
    // bit 67, so upstream also raises the alarm "BOOST"/"TRIM"
    TEST_ASSERT_EQUAL_STRING("ALARM OL BOOST", r.status().c_str());
    TEST_ASSERT_EQUAL_STRING("BOOST", r.get("ups.alarm"));
    r.t.override("QGS\r", qgs("100010000000", "240.0", "220.0")); // 0.92: TRIM
    TEST_ASSERT_TRUE(r.full(80000));
    TEST_ASSERT_EQUAL_STRING("ALARM OL TRIM", r.status().c_str());
    TEST_ASSERT_EQUAL_STRING("TRIM", r.get("ups.alarm"));
    r.t.override("QGS\r", qgs("100010000000", "240.0", "100.0")); // < 0.5: the status row fails
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 120000) == QxEngine::Update::Failed);

    // P00: AVR is an alarm, not BYPASS
    Run r0;
    r0.t.override("QPI\r", "(PI00\r");
    r0.t.override("QMOD\r", "(L\r");
    r0.t.override("QWS\r", qws({}));
    TEST_ASSERT_TRUE(r0.start());
    r0.t.override("QGS\r", qgs("100010000000"));
    TEST_ASSERT_TRUE(r0.full());
    TEST_ASSERT_EQUAL_STRING("UPS is in AVR Mode.", r0.get("ups.alarm"));
    TEST_ASSERT_EQUAL_STRING("ALARM OL", r0.status().c_str());
}

void test_ups_type_and_pi71_dashes(void) {
    const char* types[][2] = { { "00", "offline" }, { "01", "line-interactive" }, { "10", "online" } };
    for (const auto& ty : types) {
        Run r;
        char bits[16];
        snprintf(bits, sizeof(bits), "%s0000000001", ty[0]);
        r.t.override("QGS\r", qgs(bits));
        TEST_ASSERT_TRUE(r.start());
        TEST_ASSERT_EQUAL_STRING(ty[1], r.get("ups.type"));
    }
    // PI71: "--" type -> online, "-" bits -> inactive
    Run r;
    r.t.override("QPI\r", "(PI71\r");
    r.t.override("QGS\r", qgs("------------"));
    r.t.override("QMOD\r", "(L\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("online", r.get("ups.type"));
}

void test_beeper_status_with_alarm_control(void) {
    // Alarm control enabled (testing table): bit 72 '1' -> muted
    Run r;
    TEST_ASSERT_TRUE(r.start());
    r.t.override("QGS\r", qgs("100000000100"));
    TEST_ASSERT_TRUE(r.full());
    TEST_ASSERT_EQUAL_STRING("muted", r.get("ups.beeper.status"));
    // Without alarm control: bit 72 '1' -> disabled
    Run r2;
    r2.t.override("QFLAG\r", "(EjDb\r");
    r2.t.override("QGS\r", qgs("100000000100"));
    TEST_ASSERT_TRUE(r2.start());
    TEST_ASSERT_EQUAL_STRING("disabled", r2.get("ups.beeper.status"));
}

void test_mode(void) {
    struct Case {
        const char* answer;
        const char* status;
        const char* alarm;
    } cases[] = {
        { "(L\r", "OL", nullptr },
        { "(B\r", "OB", nullptr },
        { "(Y\r", "OL BYPASS", nullptr },
        { "(T\r", "CAL OL", nullptr },
        { "(S\r", "OL OFF", nullptr },
        { "(D\r", "ALARM OL FSD", "UPS is shutting down! Shutdown imminent!" },
        { "(P\r", "ALARM OL", "UPS is going ON." },
        { "(F\r", "ALARM OL", "Fault reported by UPS." },
    };
    for (const auto& c : cases) {
        Run r;
        r.t.override("QWS\r", qws({}));
        TEST_ASSERT_TRUE(r.start());
        r.t.override("QMOD\r", c.answer);
        TEST_ASSERT_TRUE_MESSAGE(r.full(), c.answer);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.status, r.status().c_str(), c.answer);
        if (c.alarm) TEST_ASSERT_EQUAL_STRING_MESSAGE(c.alarm, r.get("ups.alarm"), c.answer);
        else TEST_ASSERT_NULL_MESSAGE(r.d.find("ups.alarm"), c.answer);
    }
    // An unknown mode fails the two QMOD rows only
    Run r;
    TEST_ASSERT_TRUE(r.start());
    r.t.override("QMOD\r", "(X\r");
    TEST_ASSERT_TRUE(r.full());
}

void test_mode_buzzwords(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    r.t.override("QMOD\r", "(E\r");
    TEST_ASSERT_TRUE(r.full());
    TEST_ASSERT_EQUAL_STRING("vendor:voltronic:ECO-inverter-on", r.get("experimental.ups.mode.buzzwords"));
    // A quick update does not walk QMOD: buzzmode_init() empties the buffer and
    // buzzmode_commit() removes the variable, as upstream
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 42000) == QxEngine::Update::Ok);
    TEST_ASSERT_NULL(r.d.find("experimental.ups.mode.buzzwords"));
    r.t.override("QMOD\r", "(C\r");
    TEST_ASSERT_TRUE(r.full(80000));
    TEST_ASSERT_EQUAL_STRING("vendor:voltronic:freq-converter-on", r.get("experimental.ups.mode.buzzwords"));
}

void test_fault(void) {
    // The QGS fault bit (68) does not unskip QFS upstream (compared with the integer 1)
    Run r;
    TEST_ASSERT_TRUE(r.start());
    r.t.override("QGS\r", qgs("100001000000"));
    TEST_ASSERT_TRUE(r.full());
    TEST_ASSERT_FALSE(r.sentCmd("QFS\r"));

    struct Case {
        const char* qpi;
        const char* qfs;
        const char* alarm;
        bool over;
    } cases[] = {
        // the testing answer: (14 212.1 50.0 005.6 ...: fault 14
        { "(PI01\r", "(14 212.1 50.0 005.6 49.9 006 010.6 343.8 ---.- 026.2 021.8 01101100\r",
          "Inverter short-circuited.", false },
        { "(PI01\r", "(1A\r", "L1 inverter negative power out of acceptable range.", false },
        { "(PI01\r", "(43\r", "Overload fault.", true },
        { "(PI01\r", "(99\r", "Unknown fault [99]", false },
        { "(PI01\r", "(2A\r", "Unknown fault [2A]", false },
        { "(PI31\r", "(07\r", "Overload fault.", true },
        { "(PI31\r", "(1B\r", "Unknown fault [1B]", false },
        { "(PI01\r", "(OK\r", "No fault found", false },
    };
    for (const auto& c : cases) {
        Run rf;
        rf.t.override("QPI\r", c.qpi);
        rf.t.override("QWS\r", qws({}));
        rf.t.override("QMOD\r", "(L\r");
        TEST_ASSERT_TRUE(rf.start());
        int row = rf.rowOf("ups.alarm", 0);
        // the QFS row (unskipped by hand, as upstream would on the fault bit)
        int qfs = -1;
        for (size_t i = 0; i < rf.e.rowCount(); i++) {
            const QxItem& it = rf.e.item((int)i);
            if (it.command && strcmp(it.command, "QFS\r") == 0) qfs = (int)i;
        }
        TEST_ASSERT_TRUE(row >= 0 && qfs >= 0);
        rf.e.clearFlags(qfs, QX_FLAG_SKIP);
        rf.t.override("QFS\r", c.qfs);
        TEST_ASSERT_TRUE_MESSAGE(rf.full(), c.qfs);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.alarm, rf.get("ups.alarm"), c.qfs);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.over ? "ALARM OL OVER" : "ALARM OL", rf.status().c_str(), c.qfs);
        // Skipped again once processed
        TEST_ASSERT_TRUE_MESSAGE(rf.e.flags(qfs) & QX_FLAG_SKIP, c.qfs);
    }
}

void test_warnings(void) {
    // The testing answer: bit #58 known, bit #64 unknown
    Run r;
    r.t.override("QMOD\r", "(L\r");
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_TRUE(r.full());
    TEST_ASSERT_EQUAL_STRING(
        "UPS warnings: Warning!! Cover of maintain switch is open. Unknown warnings [bit: #64]",
        r.get("ups.alarm"));

    struct Case {
        std::vector<int> bits;
        const char* alarm;
        const char* status;
    } cases[] = {
        { { 7 }, "UPS warnings: Low battery.", "ALARM OL LB" },
        { { 8, 55 }, "UPS warnings: Overload alarm. Warning for Battery replace.",
          "ALARM OL OVER RB" },
        { { 58, 59 }, "UPS warnings: Unknown warnings [bit: #59, #60]", "ALARM OL" },
        // Too long to be explicit (>= ST_MAX_VALUE_LEN - 32): the cryptic form
        { { 0, 1, 2, 3, 4, 5, 9, 10, 11, 12, 13, 14, 62 },
          "UPS warnings: Known (see log or manual) [bit: #01, #02, #03, #04, #05, #06, #10, #11, #12, #13, #14, "
          "#15]; Unknown warnings [bit: #63]",
          "ALARM OL" },
    };
    for (const auto& c : cases) {
        Run rw;
        rw.t.override("QMOD\r", "(L\r");
        TEST_ASSERT_TRUE(rw.start());
        rw.t.override("QWS\r", qws(c.bits));
        TEST_ASSERT_TRUE_MESSAGE(rw.full(), c.alarm);
        String expected = c.alarm;
        // RB adds "Replace battery!" after the protocol alarms (ups_alarm_set())
        if (strstr(c.status, "RB")) expected += " Replace battery!";
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.c_str(), rw.get("ups.alarm"), c.alarm);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.status, rw.status().c_str(), c.alarm);
    }

    // No warning: no alarm; a non binary answer fails the row only
    Run rn;
    rn.t.override("QMOD\r", "(L\r");
    rn.t.override("QWS\r", qws({}));
    TEST_ASSERT_TRUE(rn.start());
    TEST_ASSERT_TRUE(rn.full());
    TEST_ASSERT_NULL(rn.d.find("ups.alarm"));
}

void test_battery_runtime_and_serial(void) {
    // NUT issue #2765: a longer last QBV component, read to the end of the reply
    Run r;
    r.t.override("QBV\r", "(041.0 03 01 100 00037\r");
    r.t.override("QID\r", "(000000000000\r"); // non-significant serial: not stored
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("2220", r.get("battery.runtime"));
    TEST_ASSERT_EQUAL_STRING("100", r.get("battery.charge"));
    TEST_ASSERT_NULL(r.d.find("device.serial"));
}

// --- Instant commands ---------------------------------------------------------------------

static QxEngine::Instcmd cmd(Run& r, const char* name, const char* extra = nullptr) {
    return r.e.instcmd(r.d, name, extra);
}

void test_instcmd_simple_commands(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    struct Case {
        const char* name;
        const char* sent;
        bool handled;
    } cases[] = {
        { "load.off", "SOFF\r", false },
        { "load.on", "SON\r", true },
        { "shutdown.stop", "CS\r", true },
        { "test.battery.start.deep", "TL\r", true },
        { "test.battery.start.quick", "T\r", false },
        { "test.battery.stop", "CT\r", false },
        { "beeper.enable", "PEA\r", true },
        { "beeper.disable", "PDA\r", true },
        { "outlet.1.load.on", "SKON1\r", true },
        { "outlet.1.load.off", "SKOFF1\r", false },
        { "outlet.2.load.on", "SKON2\r", true },
        { "outlet.2.load.off", "SKOFF2\r", true },
        { "outlet.3.load.on", "SKON3\r", false },
        { "outlet.3.load.off", "SKOFF3\r", true },
        { "bypass.start", "PEE\r", true },
        { "bypass.stop", "PDE\r", true },
    };
    for (const auto& c : cases) {
        TEST_ASSERT_TRUE_MESSAGE(r.e.commandOffered(c.name), c.name);
        QxEngine::Instcmd rc = cmd(r, c.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.sent, r.last(), c.name);
        TEST_ASSERT_TRUE_MESSAGE(rc == (c.handled ? QxEngine::Instcmd::Handled : QxEngine::Instcmd::Failed), c.name);
    }
    // Skipped rows (outlet 4: QSK4 NAK) are not offered
    size_t n = r.t.sent.size();
    TEST_ASSERT_TRUE(cmd(r, "outlet.4.load.on") == QxEngine::Instcmd::Invalid);
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
}

void test_instcmd_shutdown_and_test_strings(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    // Defaults: offdelay 30, ondelay 180 -> S.5R0003 (not in the table: NAK)
    TEST_ASSERT_TRUE(cmd(r, "shutdown.return") == QxEngine::Instcmd::Failed);
    TEST_ASSERT_EQUAL_STRING("S.5R0003\r", r.last());
    TEST_ASSERT_TRUE(cmd(r, "shutdown.stayoff") == QxEngine::Instcmd::Failed);
    TEST_ASSERT_EQUAL_STRING("S.5R0000\r", r.last());
    // offdelay 18, ondelay 120 -> S.3R0002 (ACK)
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "ups.delay.shutdown", "18") == QxEngine::Setvar::Handled);
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "ups.delay.start", "120") == QxEngine::Setvar::Handled);
    TEST_ASSERT_TRUE(cmd(r, "shutdown.return") == QxEngine::Instcmd::Handled);
    TEST_ASSERT_EQUAL_STRING("S.3R0002\r", r.last());
    // offdelay 120, ondelay 1440 -> S02R0024 (NAK)
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "ups.delay.shutdown", "120") == QxEngine::Setvar::Handled);
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "ups.delay.start", "1440") == QxEngine::Setvar::Handled);
    TEST_ASSERT_TRUE(cmd(r, "shutdown.return") == QxEngine::Instcmd::Failed);
    TEST_ASSERT_EQUAL_STRING("S02R0024\r", r.last());
    // ondelay 0, offdelay 30 -> S.5 (ACK)
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "ups.delay.shutdown", "30") == QxEngine::Setvar::Handled);
    TEST_ASSERT_TRUE(r.e.setvar(r.d, "ups.delay.start", "0") == QxEngine::Setvar::Handled);
    TEST_ASSERT_TRUE(cmd(r, "shutdown.return") == QxEngine::Instcmd::Handled);
    TEST_ASSERT_EQUAL_STRING("S.5\r", r.last());

    // test.battery.start: seconds -> .n (n*6 s) or minutes
    TEST_ASSERT_TRUE(cmd(r, "test.battery.start", "18") == QxEngine::Instcmd::Handled);
    TEST_ASSERT_EQUAL_STRING("T.3\r", r.last());
    TEST_ASSERT_TRUE(cmd(r, "test.battery.start", "120") == QxEngine::Instcmd::Failed);
    TEST_ASSERT_EQUAL_STRING("T02\r", r.last());
    TEST_ASSERT_TRUE(cmd(r, "test.battery.start") == QxEngine::Instcmd::Failed); // 600 s default
    TEST_ASSERT_EQUAL_STRING("T10\r", r.last());
    size_t n = r.t.sent.size();
    TEST_ASSERT_TRUE(cmd(r, "test.battery.start", "6000") == QxEngine::Instcmd::Failed); // > 5940
    TEST_ASSERT_TRUE(cmd(r, "test.battery.start", "11") == QxEngine::Instcmd::Failed);   // < 12
    TEST_ASSERT_TRUE(cmd(r, "test.battery.start", "1.5") == QxEngine::Instcmd::Failed);  // non numerical
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
}

void test_instcmd_beeper_toggle(void) {
    // Alarm control enabled (testing table)
    Run r;
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("enabled", r.get("ups.beeper.status"));
    TEST_ASSERT_TRUE(cmd(r, "beeper.toggle") == QxEngine::Instcmd::Handled);
    TEST_ASSERT_EQUAL_STRING("BZOFF\r", r.last());
    r.d.set("ups.beeper.status", "muted");
    TEST_ASSERT_TRUE(cmd(r, "beeper.toggle") == QxEngine::Instcmd::Handled);
    TEST_ASSERT_EQUAL_STRING("BZON\r", r.last());
    r.d.set("ups.beeper.status", "disabled");
    size_t n = r.t.sent.size();
    TEST_ASSERT_TRUE(cmd(r, "beeper.toggle") == QxEngine::Instcmd::Failed); // "already disabled"
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());

    // Without alarm control: disabled -> BZON
    Run r2;
    r2.t.override("QFLAG\r", "(EjDb\r");
    TEST_ASSERT_TRUE(r2.start());
    r2.d.set("ups.beeper.status", "disabled");
    TEST_ASSERT_TRUE(cmd(r2, "beeper.toggle") == QxEngine::Instcmd::Handled);
    TEST_ASSERT_EQUAL_STRING("BZON\r", r2.last());
}

// The capability strings live in the engine and are cleared by reset(), as the statics of a
// freshly started driver
void test_state_reset_with_the_engine(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start()); // alarm control enabled
    r.e.reset(QX_VOLTRONIC, QxOptions());
    r.t.override("QFLAG\r", "(EjDb\r"); // no 'a' at all
    r.d = UPSData();
    TEST_ASSERT_TRUE(r.start());
    r.d.set("ups.beeper.status", "muted");
    // Without alarm control and neither enabled nor disabled: "BZ" (alarm_control NULL); a
    // leaked "enabled" would give BZON
    cmd(r, "beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("BZ\r", r.last());
}

// --- SET VAR ------------------------------------------------------------------------------

static QxEngine::Setvar set(Run& r, const char* var, const char* val) {
    return r.e.setvar(r.d, var, val);
}

void test_setvar_battery_low_range_reads_ratings(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    // voltronic_batt_low(): 230 V * 4 A < 1000 -> 20..24
    TEST_ASSERT_TRUE(set(r, "battery.voltage.low", "24") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("W0E24\r", r.last());
    size_t n = r.t.sent.size();
    TEST_ASSERT_TRUE(set(r, "battery.voltage.low", "26") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
    // 230 V * 5 A >= 1000 -> 20..28 (W0E26 is not in the table: NAK)
    r.d.set("output.current.nominal", "5");
    TEST_ASSERT_TRUE(set(r, "battery.voltage.low", "26") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_STRING("W0E26\r", r.last());
    // Without the ratings the 24/28 bounds are dropped: no pair, nothing accepted
    r.d.remove("output.current.nominal");
    n = r.t.sent.size();
    TEST_ASSERT_TRUE(set(r, "battery.voltage.low", "22") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
}

void test_setvar_eco_limits(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    // Voltage: the ranges filled at runtime by voltronic_eco_volt()
    TEST_ASSERT_TRUE(set(r, "input.transfer.high", "250") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("HEH250\r", r.last());
    TEST_ASSERT_TRUE(set(r, "input.transfer.low", "210") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("HEL210\r", r.last());
    size_t n = r.t.sent.size();
    TEST_ASSERT_TRUE(set(r, "input.transfer.high", "260") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "input.transfer.low", "205") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
    // Frequency: P01, output.frequency.nominal 50: high 53..60, low 40..47
    TEST_ASSERT_TRUE(set(r, "input.frequency.high", "54.0") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("FREH54.0\r", r.last());
    TEST_ASSERT_TRUE(set(r, "input.frequency.low", "47.0") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("FREL47.0\r", r.last());
    n = r.t.sent.size();
    TEST_ASSERT_TRUE(set(r, "input.frequency.high", "52") == QxEngine::Setvar::Unknown);
    // With 60 Hz the 50 Hz ranges are dropped: high 63..70
    r.d.set("output.frequency.nominal", "60.0");
    TEST_ASSERT_TRUE(set(r, "input.frequency.high", "55") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
    TEST_ASSERT_TRUE(set(r, "input.frequency.high", "65") == QxEngine::Setvar::Unknown); // FREH65.0: NAK
    TEST_ASSERT_EQUAL_STRING("FREH65.0\r", r.last());
}

void test_setvar_capabilities_and_others(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    struct Case {
        const char* var;
        const char* val;
        const char* sent;
        bool handled;
    } cases[] = {
        { "ups.start.auto", "yes", "PER\r", false },
        { "battery.protection", "no", "PDS\r", true },
        { "battery.energysave", "yes", "PEG\r", true },
        { "ups.start.battery", "no", "PDC\r", false },
        { "outlet.0.switchable", "no", "PDJ\r", true },
        { "battery.packs", "4", "BATGN04\r", true },
        { "outlet.2.delay.shutdown", "540", "PSK2009\r", true },
    };
    for (const auto& c : cases) {
        QxEngine::Setvar rc = set(r, c.var, c.val);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.sent, r.last(), c.var);
        TEST_ASSERT_TRUE_MESSAGE(rc == (c.handled ? QxEngine::Setvar::Handled : QxEngine::Setvar::Unknown), c.var);
    }
    size_t n = r.t.sent.size();
    // Same value: nothing to do
    TEST_ASSERT_TRUE(set(r, "ups.start.auto", "no") == QxEngine::Setvar::Handled);
    // Out of enum / range
    TEST_ASSERT_TRUE(set(r, "ups.start.auto", "maybe") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "battery.packs", "100") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "outlet.1.delay.shutdown", "60000") == QxEngine::Setvar::Unknown);
    // NONUT setvars need ups.conf: skipped (battery_number by the INIT walk)
    TEST_ASSERT_TRUE(set(r, "battery_number", "2") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "max_bypass_volt", "260") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "output_phase_angle", "120") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "reset_to_default", "yes") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "eco_mode", "enabled") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_EQUAL_UINT(n, r.t.sent.size());
    TEST_ASSERT_TRUE(r.e.flags(r.rowOf("battery_number", QX_FLAG_SETVAR)) & QX_FLAG_SKIP);
}

void test_setvar_delays(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    // ups.delay.start: 0..599940, truncated to the minute
    TEST_ASSERT_TRUE(set(r, "ups.delay.start", "250") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("240", r.get("ups.delay.start"));
    // ups.delay.shutdown: 12..5940, by 6 s below a minute, by minutes above
    TEST_ASSERT_TRUE(set(r, "ups.delay.shutdown", "59") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("54", r.get("ups.delay.shutdown"));
    TEST_ASSERT_TRUE(set(r, "ups.delay.shutdown", "125") == QxEngine::Setvar::Handled);
    TEST_ASSERT_EQUAL_STRING("120", r.get("ups.delay.shutdown"));
    TEST_ASSERT_TRUE(set(r, "ups.delay.shutdown", "6000") == QxEngine::Setvar::Unknown);
    TEST_ASSERT_TRUE(set(r, "ups.delay.shutdown", "11") == QxEngine::Setvar::Unknown);
}

// A handled setvar makes the next update a full one with the SEMI_STATIC rows (QFLAG, QBV...)
void test_setvar_polls_semi_static_rows(void) {
    Run r;
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_TRUE(set(r, "battery.protection", "no") == QxEngine::Setvar::Handled);
    r.t.override("QFLAG\r", "(EpahcjDbroegfls\r");
    r.t.sent.clear();
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 4000) == QxEngine::Update::Ok);
    TEST_ASSERT_TRUE(r.sentCmd("QFLAG\r"));
    TEST_ASSERT_EQUAL_STRING("no", r.get("battery.protection"));
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_claim_sends_qgs_then_qpi);
    RUN_TEST(test_claim_fails_on_unsupported_protocol_and_drops_input_voltage);
    RUN_TEST(test_nak_answers_claim_no_legacy_protocol);
    RUN_TEST(test_probing_claims_voltronic_first);
    RUN_TEST(test_probing_megatec_ups_passes_voltronic);
    RUN_TEST(test_claim_timeout_sleeps_5s_and_asks_again);
    RUN_TEST(test_prober_passes_the_sleeper);
    RUN_TEST(test_testing_values);
    RUN_TEST(test_apc_srv2ki_e_answers);
    RUN_TEST(test_capabilities_of_testing_table);
    RUN_TEST(test_capabilities_parsing);
    RUN_TEST(test_eco_volt_range_low_nominal_voltage);
    RUN_TEST(test_protocol_p09_unskips_two_phase_rows);
    RUN_TEST(test_protocol_p10_unskips_three_phase_rows);
    RUN_TEST(test_protocol_p31_battery_type);
    RUN_TEST(test_status_bits);
    RUN_TEST(test_status_bypass_boost_trim_and_avr);
    RUN_TEST(test_ups_type_and_pi71_dashes);
    RUN_TEST(test_beeper_status_with_alarm_control);
    RUN_TEST(test_mode);
    RUN_TEST(test_mode_buzzwords);
    RUN_TEST(test_fault);
    RUN_TEST(test_warnings);
    RUN_TEST(test_battery_runtime_and_serial);
    RUN_TEST(test_instcmd_simple_commands);
    RUN_TEST(test_instcmd_shutdown_and_test_strings);
    RUN_TEST(test_instcmd_beeper_toggle);
    RUN_TEST(test_state_reset_with_the_engine);
    RUN_TEST(test_setvar_battery_low_range_reads_ratings);
    RUN_TEST(test_setvar_eco_limits);
    RUN_TEST(test_setvar_capabilities_and_others);
    RUN_TEST(test_setvar_delays);
    RUN_TEST(test_setvar_polls_semi_static_rows);
    return UNITY_END();
}
#endif
#endif
