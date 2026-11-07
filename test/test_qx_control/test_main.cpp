#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "QxEngine.h"
#include "QxProtocols.h"
#include "QxBlazerCommon.h"
#include "QxTestingTransport.h"
#include "QxTestingTables.h"
#include "QxControl.h"
#include "QxFakeControlPort.h"

// US-135: commands and SET VAR of the Q* protocols, as instcmd() and setvar() of
// nut_repo/drivers/nutdrv_qx.c, on the fake transport: the exact strings sent to the UPS and
// the result from the protocol's accepted/rejected replies; then QxControl through a fake
// port that plays the host (QxFakeControlPort), which must never reach the HID accessor.

void setUp(void) {}
void tearDown(void) {}

struct Run {
    QxTestingTransport t;
    QxEngine e;
    UPSData d;
    Run(const QxSubdriver& sub, const QxTesting* table) : t(table, sub.rejected), e(sub, t) {}
    // claim -> initups -> initinfo (t = 0 ms) -> first updateinfo (t = 2 000 ms, quick)
    bool start() {
        if (!e.claim(d)) return false;
        e.initUps();
        if (!e.initInfo(d, 0)) return false;
        return e.updateInfo(d, 2000) == QxEngine::Update::Ok;
    }
    QxEngine::Instcmd cmd(const char* name, const char* extradata = nullptr) {
        return e.instcmd(d, name, extradata);
    }
    QxEngine::Setvar set(const char* name, const char* value) { return e.setvar(d, name, value); }
    const char* get(const char* key) {
        const String* v = d.find(key);
        return v ? v->c_str() : "(missing)";
    }
    std::string last() const { return t.sent.empty() ? std::string("(none)") : t.sent.back(); }
};

static const QxEngine::Instcmd HANDLED = QxEngine::Instcmd::Handled;
static const QxEngine::Instcmd FAILED = QxEngine::Instcmd::Failed;
static const QxEngine::Instcmd INVALID = QxEngine::Instcmd::Invalid;

// --- A protocol of the tests: the branches no ported protocol reaches -------------------
// Rows with the shape of the upstream tables: load.*.delay commands (the fallbacks of
// instcmd()), a writable ups.start.auto (enum), a string and a two-range variable sent to
// the UPS, the server-side ups.delay.*. Every command row formats its value with %s.

static int fmtValue(QxEngine& qx, const QxItem& item, char* value, size_t valuelen) {
    (void)qx;
    char v[64];
    snprintf(v, sizeof(v), "%s", value);
    snprintf(value, valuelen, item.command, v);
    return 0;
}

static bool claimAlways(QxEngine& qx) {
    (void)qx;
    return true;
}

static const QxInfoRw R_START_AUTO[] = { { "yes", nullptr }, { "no", nullptr }, { "", nullptr } };
static const QxInfoRw R_ID[] = { { "4", nullptr }, { "", nullptr } };
static const QxInfoRw R_PACKS[] = { { "1", nullptr }, { "5", nullptr }, { "10", nullptr }, { "20", nullptr }, { "", nullptr } };

static const QxItem TEST_QX2NUT[] = {
    // A value read by the full walks only: shows when a poll is full
    { "ups.serial",         0, nullptr, "SN\r", 0, 0, 1, 0, "%s", 0, nullptr, nullptr, nullptr },
    { "load.on.delay",     0, nullptr, "N%s\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, fmtValue },
    { "load.off.delay",     0, nullptr, "F%s\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, fmtValue },
    { "ups.start.auto",     QX_ST_FLAG_RW, R_START_AUTO, "A%s\r", 0, 0, 0, 0, nullptr, QX_FLAG_SETVAR | QX_FLAG_ENUM, nullptr, nullptr, fmtValue },
    { "ups.id",             QX_ST_FLAG_RW | QX_ST_FLAG_STRING, R_ID, "ID%s\r", 0, 0, 0, 0, nullptr, QX_FLAG_SETVAR, nullptr, nullptr, fmtValue },
    { "battery.packs",      QX_ST_FLAG_RW, R_PACKS, "BP%s\r", 0, 0, 0, 0, nullptr, QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, fmtValue },
    { "ups.delay.start",    QX_ST_FLAG_RW, BLAZER_R_ONDELAY,  nullptr, 0, 0, 0, 0, "180", QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, blazerProcessSetvar },
    { "ups.delay.shutdown", QX_ST_FLAG_RW, BLAZER_R_OFFDELAY, nullptr, 0, 0, 0, 0, "30",  QX_FLAG_ABSENT | QX_FLAG_SETVAR | QX_FLAG_RANGE, nullptr, nullptr, blazerProcessSetvar },
    { nullptr, 0, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr }
};

static const QxSubdriver QX_TEST = { "Test 0.01", claimAlways, TEST_QX2NUT, nullptr, nullptr, "ACK", nullptr };

// Every command of the test protocol is accepted, or handled without reply
static const QxTesting TEST_TESTING[] = {
    { "SN\r", "#1234\r", -1 },
    { "Ayes\r", "ACK", -1 },
    { "Ano\r", "ACK", -1 },
    { "N0\r", "", -1 },
    { "N-1\r", "", -1 },
    { "N180\r", "", -1 },
    { "F0\r", "", -1 },
    { "F30\r", "", -1 },
    { nullptr, nullptr, 0 }
};

static int rowOf(const QxEngine& e, const char* name) {
    for (size_t r = 0; r < e.rowCount(); r++) {
        if (QxEngine::equalsIgnoreCase(e.item((int)r).info_type, name)) return (int)r;
    }
    return -1;
}

// --- instcmd(): megatec ----------------------------------------------------------------

// One command of megatec accepted with "ACK": the exact string sent
static void expectMegatec(const char* name, const char* extradata, const char* sent) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override(sent, "ACK");
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL_MESSAGE((int)HANDLED, (int)r.cmd(name, extradata), name);
    TEST_ASSERT_EQUAL_MESSAGE(before + 1, r.t.sent.size(), name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(sent, r.last().c_str(), name);
}

void test_megatec_command_strings(void) {
    expectMegatec("beeper.toggle", nullptr, "Q\r");
    expectMegatec("load.off", nullptr, "S00R0000\r");
    expectMegatec("load.on", nullptr, "C\r");
    // Defaults ups.delay.shutdown 30 (.5 minutes) and ups.delay.start 180 (3 minutes)
    expectMegatec("shutdown.return", nullptr, "S.5R0003\r");
    expectMegatec("shutdown.stayoff", nullptr, "S.5R0000\r");
    expectMegatec("shutdown.stop", nullptr, "C\r");
    // Default test time 600 s = 10 minutes; 120 s = 2 minutes
    expectMegatec("test.battery.start", nullptr, "T10\r");
    expectMegatec("test.battery.start", "120", "T02\r");
    expectMegatec("test.battery.start", "", "T10\r"); // "" is no value
    expectMegatec("test.battery.start.quick", nullptr, "T\r");
    expectMegatec("test.battery.start.deep", nullptr, "TL\r");
    expectMegatec("test.battery.stop", nullptr, "CT\r");
}

void test_command_without_preprocess_ignores_the_value(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("S00R0000\r", "");
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("load.off", "45"));
    TEST_ASSERT_EQUAL_STRING("S00R0000\r", r.last().c_str());
}

void test_no_reply_is_handled(void) {
    // megatec_testing answers "" to Q, C, TL, T, CT (the commands with no reply)
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("beeper.toggle"));
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("test.battery.start.deep"));
}

void test_echoed_command_is_rejected(void) {
    // The UPS echoes a command it refuses: "S.5R0003\r" is not in megatec_testing, so the
    // fake transport echoes it, as the TESTING branch of nutdrv_qx does
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("shutdown.return"));
    TEST_ASSERT_EQUAL_STRING("S.5R0003\r", r.last().c_str());
    // Any reply other than accepted fails
    r.t.override("Q\r", "NAK");
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("beeper.toggle"));
    // accepted is compared without case, as strcasecmp()
    r.t.override("Q\r", "ack");
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("beeper.toggle"));
}

void test_rejected_reply_of_the_protocol_fails(void) {
    // innovatae: rejected "NAK\r" fails in qx_process_answer(); accepted "ACK"
    Run r(QX_INNOVATAE, INNOVATAE_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("test.battery.start.quick")); // T -> ACK
    TEST_ASSERT_EQUAL_STRING("T\r", r.last().c_str());
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("test.battery.start"));        // T10 -> NAK
    TEST_ASSERT_EQUAL_STRING("T10\r", r.last().c_str());
}

void test_transport_error_fails(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.failNext(1);
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("beeper.toggle"));
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("beeper.toggle"));
}

void test_failed_preprocess_sends_nothing(void) {
    // battery test time out of range [60..5940] seconds
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("test.battery.start", "30"));
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("test.battery.start", "6000"));
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
}

void test_command_not_in_the_protocol_is_invalid(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)INVALID, (int)r.cmd("shutdown.reboot"));
    TEST_ASSERT_EQUAL((int)INVALID, (int)r.cmd("load.off.delay", "10"));
    TEST_ASSERT_EQUAL((int)INVALID, (int)r.cmd("beeper.enable"));
    TEST_ASSERT_EQUAL((int)INVALID, (int)r.cmd(nullptr));
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
}

void test_skipped_command_row_is_invalid(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.e.addFlags(rowOf(r.e, "beeper.toggle"), QX_FLAG_SKIP);
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)INVALID, (int)r.cmd("beeper.toggle"));
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
}

static bool sentAfter(const Run& r, size_t from, const char* cmd) {
    for (size_t i = from; i < r.t.sent.size(); i++) {
        if (r.t.sent[i] == cmd) return true;
    }
    return false;
}

void test_handled_command_makes_the_next_poll_full(void) {
    // data_has_changed: the next updateinfo is a full walk (I\r for ups.serial), even 2 s later
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)QxEngine::Update::Ok, (int)r.e.updateInfo(r.d, 4000));
    TEST_ASSERT_FALSE(sentAfter(r, from, "I\r")); // quick poll only

    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("beeper.toggle"));
    from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)QxEngine::Update::Ok, (int)r.e.updateInfo(r.d, 6000));
    TEST_ASSERT_TRUE(sentAfter(r, from, "I\r"));

    // A failed command changes nothing
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("shutdown.return"));
    from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)QxEngine::Update::Ok, (int)r.e.updateInfo(r.d, 8000));
    TEST_ASSERT_FALSE(sentAfter(r, from, "I\r"));
}

// --- instcmd(): fallbacks on load.*.delay ----------------------------------------------

void test_load_fallbacks_use_the_delay_rows(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("load.off"));
    TEST_ASSERT_EQUAL_STRING("F0\r", r.last().c_str());
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("load.on"));
    TEST_ASSERT_EQUAL_STRING("N0\r", r.last().c_str());
    // The delay rows themselves, with their parameter
    r.t.override("F45\r", "");
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("load.off.delay", "45"));
    TEST_ASSERT_EQUAL_STRING("F45\r", r.last().c_str());
}

void test_shutdown_return_fallback_sets_start_auto_first(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.d.set("ups.start.auto", "no");
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("shutdown.return"));
    TEST_ASSERT_EQUAL(from + 3, r.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("Ayes\r", r.t.sent[from].c_str());
    TEST_ASSERT_EQUAL_STRING("N180\r", r.t.sent[from + 1].c_str()); // ups.delay.start
    TEST_ASSERT_EQUAL_STRING("F30\r", r.t.sent[from + 2].c_str());  // ups.delay.shutdown
}

void test_shutdown_stayoff_fallback(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.d.set("ups.start.auto", "yes");
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("shutdown.stayoff"));
    TEST_ASSERT_EQUAL(from + 3, r.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("Ano\r", r.t.sent[from].c_str());
    TEST_ASSERT_EQUAL_STRING("N-1\r", r.t.sent[from + 1].c_str());
    TEST_ASSERT_EQUAL_STRING("F30\r", r.t.sent[from + 2].c_str());
}

void test_fallback_without_start_auto_sends_only_the_delays(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("shutdown.return"));
    TEST_ASSERT_EQUAL(from + 2, r.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("N180\r", r.t.sent[from].c_str());
}

void test_refused_start_auto_stops_the_shutdown(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.d.set("ups.start.auto", "no");
    r.t.override("Ayes\r", "Ayes\r"); // echoed: refused
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("shutdown.return"));
    TEST_ASSERT_EQUAL(from + 1, r.t.sent.size()); // no load command sent
}

void test_failed_on_delay_stops_the_fallback(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("N180\r", "N180\r");
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)FAILED, (int)r.cmd("shutdown.return"));
    TEST_ASSERT_EQUAL(from + 1, r.t.sent.size()); // load.off.delay not sent
}

// --- commandOffered() ------------------------------------------------------------------

void test_commands_offered_by_megatec(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    const char* offered[] = { "beeper.toggle", "load.off", "load.on", "shutdown.return", "shutdown.stayoff",
                              "shutdown.stop", "test.battery.start", "test.battery.start.deep",
                              "test.battery.start.quick", "test.battery.stop" };
    for (const char* c : offered) TEST_ASSERT_TRUE_MESSAGE(r.e.commandOffered(c), c);
    const char* missing[] = { "load.off.delay", "load.on.delay", "shutdown.reboot", "beeper.enable",
                              "ups.delay.start", "input.voltage" };
    for (const char* c : missing) TEST_ASSERT_FALSE_MESSAGE(r.e.commandOffered(c), c);
    TEST_ASSERT_FALSE(r.e.commandOffered(nullptr));
    r.e.addFlags(rowOf(r.e, "test.battery.stop"), QX_FLAG_SKIP);
    TEST_ASSERT_FALSE(r.e.commandOffered("test.battery.stop"));
}

void test_load_commands_derived_from_the_delay_rows(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_TRUE(r.e.commandOffered("load.off"));
    TEST_ASSERT_TRUE(r.e.commandOffered("load.on"));
    TEST_ASSERT_TRUE(r.e.commandOffered("load.off.delay"));
    // nutdrv_qx lists no shutdown.return/stayoff for the fallbacks (QxControl adds them)
    TEST_ASSERT_FALSE(r.e.commandOffered("shutdown.return"));
    r.e.addFlags(rowOf(r.e, "load.off.delay"), QX_FLAG_SKIP);
    TEST_ASSERT_FALSE(r.e.commandOffered("load.off"));
}

// --- setvar() --------------------------------------------------------------------------

static const QxEngine::Setvar SET_OK = QxEngine::Setvar::Handled;
static const QxEngine::Setvar SET_UNKNOWN = QxEngine::Setvar::Unknown;

void test_delay_start_within_range(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start")); // dfl of the ABSENT row
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.start", "600"));
    TEST_ASSERT_EQUAL_STRING("600", r.get("ups.delay.start"));
    // Truncated to the minute by blazer_process_setvar()
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.start", "90"));
    TEST_ASSERT_EQUAL_STRING("60", r.get("ups.delay.start"));
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.start", "0"));
    TEST_ASSERT_EQUAL_STRING("0", r.get("ups.delay.start"));
    // Server-side variable: nothing reaches the UPS
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
}

void test_delay_out_of_range_is_refused(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    size_t before = r.t.sent.size();
    // Range 0..599940 (blazer_r_ondelay): STAT_SET_UNKNOWN, as upstream
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.start", "600000"));
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start"));
    // Non numerical values
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.start", "abc"));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.start", "-5"));
    // Value not given
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.start", ""));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.start", nullptr));
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start"));
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
}

void test_delay_shutdown_range_and_truncation(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    // Range 12..600 (blazer_r_offdelay); below 60 s truncated to a multiple of 6
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.shutdown", "45"));
    TEST_ASSERT_EQUAL_STRING("42", r.get("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.shutdown", "150"));
    TEST_ASSERT_EQUAL_STRING("120", r.get("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.shutdown", "600"));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.shutdown", "11"));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.shutdown", "601"));
    TEST_ASSERT_EQUAL_STRING("600", r.get("ups.delay.shutdown"));
}

void test_same_value_is_nothing_to_do(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.shutdown", "30"));
    TEST_ASSERT_EQUAL_STRING("30", r.get("ups.delay.shutdown"));
}

void test_variable_not_writable_is_unknown(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.beeper.status", "enabled"));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("input.voltage", "230"));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.start.auto", "yes"));
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set(nullptr, "1"));
    r.e.addFlags(rowOf(r.e, "ups.delay.start"), QX_FLAG_SKIP);
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.delay.start", "60"));
}

void test_delays_set_change_the_shutdown_command(void) {
    // offdelay 120 s = 2 minutes, ondelay 60 s = 1 minute
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.shutdown", "120"));
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.start", "60"));
    r.t.override("S02R0001\r", "ACK");
    TEST_ASSERT_EQUAL((int)HANDLED, (int)r.cmd("shutdown.return"));
    TEST_ASSERT_EQUAL_STRING("S02R0001\r", r.last().c_str());
}

void test_server_side_set_keeps_the_poll_quick(void) {
    // setvar() of a QX_FLAG_ABSENT row does not set data_has_changed
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.delay.start", "300"));
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)QxEngine::Update::Ok, (int)r.e.updateInfo(r.d, 4000));
    TEST_ASSERT_FALSE(sentAfter(r, from, "I\r"));
}

void test_enum_variable_sent_to_the_ups(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.start.auto", "yes")); // ACK
    TEST_ASSERT_EQUAL_STRING("Ayes\r", r.last().c_str());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.start.auto", "NO"));  // enum without case
    TEST_ASSERT_EQUAL_STRING("ANO\r", r.last().c_str());
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.start.auto", "maybe"));
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
    // Echoed by the UPS: refused, reported as STAT_SET_UNKNOWN
    r.t.override("Ayes\r", "Ayes\r");
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.start.auto", "yes"));
}

void test_device_set_makes_the_next_poll_full(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("1234", r.get("ups.serial"));
    size_t from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)QxEngine::Update::Ok, (int)r.e.updateInfo(r.d, 4000));
    TEST_ASSERT_FALSE(sentAfter(r, from, "SN\r")); // quick poll only
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.start.auto", "yes"));
    from = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)QxEngine::Update::Ok, (int)r.e.updateInfo(r.d, 6000));
    TEST_ASSERT_TRUE(sentAfter(r, from, "SN\r"));
}

void test_string_variable_length(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("IDab12\r", "ACK");
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("ups.id", "ab12"));
    TEST_ASSERT_EQUAL_STRING("IDab12\r", r.last().c_str());
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("ups.id", "abcde")); // aux 4
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
}

void test_range_with_two_pairs(void) {
    Run r(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("BP12\r", "ACK");
    r.t.override("BP3\r", "");
    size_t before = r.t.sent.size();
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("battery.packs", "7"));  // between 1..5 and 10..20
    TEST_ASSERT_EQUAL((int)SET_UNKNOWN, (int)r.set("battery.packs", "21"));
    TEST_ASSERT_EQUAL(before, r.t.sent.size());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("battery.packs", "12"));
    TEST_ASSERT_EQUAL_STRING("BP12\r", r.last().c_str());
    TEST_ASSERT_EQUAL((int)SET_OK, (int)r.set("battery.packs", "3"));
    TEST_ASSERT_EQUAL_STRING("BP3\r", r.last().c_str());
}

// --- QxControl through the port --------------------------------------------------------

struct Ctl : Run {
    QxFakeControlPort p;
    Ctl(const QxSubdriver& sub, const QxTesting* table) : Run(sub, table), p(e, d) {}
    CommandResult exec(const char* name, const char* param = nullptr) { return p.executeCommand(name, param); }
    NutTable::SetStatus set(const char* name, const char* value) {
        return QxControl::instance().setVariable(p, name, value);
    }
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (const auto* c : QxControl::instance().supportedCommands(p)) out.push_back(c->name);
        return out;
    }
    // A Q* control never uses the HID accessor nor the HID beeper write
    void checkNoHid() const {
        TEST_ASSERT_EQUAL_MESSAGE(0, p.hidCalls, "hidPort() called");
        TEST_ASSERT_EQUAL_MESSAGE(0, p.beeperCalls, "setBeeper() called");
    }
};

static std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i];
    return s;
}

static const QxTesting* testingOf(const QxSubdriver* sub) {
    if (sub == &QX_MUSTEK) return MUSTEK_TESTING;
    if (sub == &QX_MEGATEC_OLD) return MEGATEC_OLD_TESTING;
    if (sub == &QX_MEGATEC) return MEGATEC_TESTING;
    if (sub == &QX_ZINTO) return ZINTO_TESTING;
    if (sub == &QX_HUNNOX) return HUNNOX_TESTING;
    if (sub == &QX_INNOVATAE) return INNOVATAE_TESTING;
    if (sub == &QX_Q1) return Q1_TESTING;
    return nullptr;
}

void test_commands_of_every_ported_protocol(void) {
    // The ten blazer-common commands of every ported table plus shutdown.default, in the order
    // of the UpsCommands.h catalog
    const char* expected = "beeper.toggle,test.battery.start,test.battery.start.quick,test.battery.start.deep,"
                           "test.battery.stop,load.off,load.on,shutdown.return,shutdown.stayoff,shutdown.stop,"
                           "shutdown.default";
    int n = 0;
    for (const QxSubdriver* const* s = QX_SUBDRIVER_LIST; *s; s++, n++) {
        const QxTesting* table = testingOf(*s);
        TEST_ASSERT_NOT_NULL_MESSAGE(table, (*s)->name);
        Ctl c(**s, table);
        TEST_ASSERT_TRUE_MESSAGE(c.start(), (*s)->name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, joined(c.names()).c_str(), (*s)->name);
        c.checkNoHid();
    }
    TEST_ASSERT_EQUAL(7, n);
}

void test_commands_of_the_fallbacks(void) {
    Ctl c(QX_TEST, TEST_TESTING);
    TEST_ASSERT_TRUE(c.start());
    TEST_ASSERT_EQUAL_STRING("load.off,load.on,load.off.delay,load.on.delay,shutdown.return,shutdown.stayoff,"
                             "shutdown.default", joined(c.names()).c_str());
    // Without the ups.delay.* rows upsdrv_shutdown() cannot run
    c.e.addFlags(rowOf(c.e, "ups.delay.start"), QX_FLAG_SKIP);
    TEST_ASSERT_EQUAL_STRING("load.off,load.on,load.off.delay,load.on.delay,shutdown.return,shutdown.stayoff",
                             joined(c.names()).c_str());
}

void test_skipped_row_leaves_the_list(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    c.e.addFlags(rowOf(c.e, "beeper.toggle"), QX_FLAG_SKIP);
    c.e.addFlags(rowOf(c.e, "shutdown.return"), QX_FLAG_SKIP);
    // shutdown.default goes with shutdown.return (no load.*.delay fallback in megatec)
    TEST_ASSERT_EQUAL_STRING("test.battery.start,test.battery.start.quick,test.battery.start.deep,"
                             "test.battery.stop,load.off,load.on,shutdown.stayoff,shutdown.stop",
                             joined(c.names()).c_str());
}

void test_no_protocol_recognised_offers_nothing(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    c.p.recognised = false;
    TEST_ASSERT_EQUAL(0, (int)c.names().size());
    size_t before = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)c.exec("beeper.toggle"));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)c.exec("shutdown.default"));
    TEST_ASSERT_EQUAL(before, c.t.sent.size());
    c.checkNoHid();
}

void test_not_connected(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    c.p.connected = false;
    TEST_ASSERT_EQUAL(0, (int)c.names().size());
    size_t before = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)c.exec("beeper.toggle"));
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::NOT_CONNECTED, (int)c.set("ups.delay.start", "60"));
    TEST_ASSERT_EQUAL(before, c.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("180", c.get("ups.delay.start"));
    c.checkNoHid();
}

void test_command_not_offered_reaches_nothing(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    size_t before = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)c.exec("beeper.enable"));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)c.exec("load.off.delay", "10"));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)c.exec("shutdown.reboot"));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)QxControl::instance().executeCommand(c.p, nullptr, nullptr));
    TEST_ASSERT_EQUAL(before, c.t.sent.size());
    c.checkNoHid();
}

void test_commands_through_the_port(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)c.exec("beeper.toggle"));
    TEST_ASSERT_EQUAL_STRING("Q\r", c.last().c_str());
    c.t.override("T02\r", "ACK");
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)c.exec("test.battery.start", "120"));
    TEST_ASSERT_EQUAL_STRING("T02\r", c.last().c_str());
    // Echoed: refused
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)c.exec("test.battery.start"));
    TEST_ASSERT_EQUAL_STRING("T10\r", c.last().c_str());
    // Bad value: refused by blazer_process_command() before sending (STAT_INSTCMD_FAILED)
    size_t before = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)c.exec("test.battery.start", "30"));
    TEST_ASSERT_EQUAL(before, c.t.sent.size());
    c.checkNoHid();
}

void test_default_shutdown_stops_then_returns(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    c.t.override("S.5R0003\r", "ACK");
    size_t from = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)c.exec("shutdown.default"));
    TEST_ASSERT_EQUAL(from + 2, c.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("C\r", c.t.sent[from].c_str());          // shutdown.stop, no reply
    TEST_ASSERT_EQUAL_STRING("S.5R0003\r", c.t.sent[from + 1].c_str()); // shutdown.return
    TEST_ASSERT_EQUAL_STRING("shutdown.default,shutdown.stop,shutdown.return", joined(c.p.commands).c_str());
    TEST_ASSERT_EQUAL_STRING(",shutdown.return", joined(c.p.defaults).c_str());
    c.checkNoHid();
}

void test_default_shutdown_goes_on_when_no_shutdown_is_pending(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    c.t.override("C\r", "C\r"); // shutdown.stop refused
    c.t.override("S.5R0003\r", "ACK");
    size_t from = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)c.exec("shutdown.default"));
    TEST_ASSERT_EQUAL(from + 4, c.t.sent.size());
    for (size_t i = 0; i < 3; i++) TEST_ASSERT_EQUAL_STRING("C\r", c.t.sent[from + i].c_str());
    TEST_ASSERT_EQUAL_STRING("S.5R0003\r", c.t.sent[from + 3].c_str());
    TEST_ASSERT_EQUAL_STRING(",shutdown.return", joined(c.p.defaults).c_str());
}

void test_default_shutdown_fails_after_three_attempts(void) {
    // "S.5R0003\r" is echoed: shutdown.return refused every time
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    size_t from = c.t.sent.size();
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)c.exec("shutdown.default"));
    TEST_ASSERT_EQUAL(from + 4, c.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("C\r", c.t.sent[from].c_str());
    for (size_t i = 1; i < 4; i++) TEST_ASSERT_EQUAL_STRING("S.5R0003\r", c.t.sent[from + i].c_str());
    TEST_ASSERT_EQUAL_STRING("", joined(c.p.defaults).c_str()); // only the reset at the start
    TEST_ASSERT_EQUAL(1, (int)c.p.defaults.size());
    c.checkNoHid();
}

void test_set_variable_through_the_port(void) {
    Ctl c(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(c.start());
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::OK, (int)c.set("ups.delay.shutdown", "120"));
    TEST_ASSERT_EQUAL_STRING("120", c.get("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::UNKNOWN, (int)c.set("ups.delay.shutdown", "700"));
    TEST_ASSERT_EQUAL_STRING("120", c.get("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::UNKNOWN, (int)c.set("ups.beeper.status", "enabled"));
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::UNKNOWN, (int)c.set(nullptr, "1"));
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::CONVERSION_FAILED, (int)c.set("ups.delay.start", nullptr));
    // The new delay reaches the shutdown command
    c.t.override("S02R0003\r", "ACK");
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)c.exec("shutdown.return"));
    TEST_ASSERT_EQUAL_STRING("S02R0003\r", c.last().c_str());
    c.checkNoHid();
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    // instcmd(): megatec
    RUN_TEST(test_megatec_command_strings);
    RUN_TEST(test_command_without_preprocess_ignores_the_value);
    RUN_TEST(test_no_reply_is_handled);
    RUN_TEST(test_echoed_command_is_rejected);
    RUN_TEST(test_rejected_reply_of_the_protocol_fails);
    RUN_TEST(test_transport_error_fails);
    RUN_TEST(test_failed_preprocess_sends_nothing);
    RUN_TEST(test_command_not_in_the_protocol_is_invalid);
    RUN_TEST(test_skipped_command_row_is_invalid);
    RUN_TEST(test_handled_command_makes_the_next_poll_full);
    // instcmd(): fallbacks
    RUN_TEST(test_load_fallbacks_use_the_delay_rows);
    RUN_TEST(test_shutdown_return_fallback_sets_start_auto_first);
    RUN_TEST(test_shutdown_stayoff_fallback);
    RUN_TEST(test_fallback_without_start_auto_sends_only_the_delays);
    RUN_TEST(test_refused_start_auto_stops_the_shutdown);
    RUN_TEST(test_failed_on_delay_stops_the_fallback);
    // commandOffered()
    RUN_TEST(test_commands_offered_by_megatec);
    RUN_TEST(test_load_commands_derived_from_the_delay_rows);
    // setvar()
    RUN_TEST(test_delay_start_within_range);
    RUN_TEST(test_delay_out_of_range_is_refused);
    RUN_TEST(test_delay_shutdown_range_and_truncation);
    RUN_TEST(test_same_value_is_nothing_to_do);
    RUN_TEST(test_variable_not_writable_is_unknown);
    RUN_TEST(test_delays_set_change_the_shutdown_command);
    RUN_TEST(test_server_side_set_keeps_the_poll_quick);
    RUN_TEST(test_enum_variable_sent_to_the_ups);
    RUN_TEST(test_device_set_makes_the_next_poll_full);
    RUN_TEST(test_string_variable_length);
    RUN_TEST(test_range_with_two_pairs);
    // QxControl
    RUN_TEST(test_commands_of_every_ported_protocol);
    RUN_TEST(test_commands_of_the_fallbacks);
    RUN_TEST(test_skipped_row_leaves_the_list);
    RUN_TEST(test_no_protocol_recognised_offers_nothing);
    RUN_TEST(test_not_connected);
    RUN_TEST(test_command_not_offered_reaches_nothing);
    RUN_TEST(test_commands_through_the_port);
    RUN_TEST(test_default_shutdown_stops_then_returns);
    RUN_TEST(test_default_shutdown_goes_on_when_no_shutdown_is_pending);
    RUN_TEST(test_default_shutdown_fails_after_three_attempts);
    RUN_TEST(test_set_variable_through_the_port);
    return UNITY_END();
}
