#include <unity.h>
#include <string.h>
#include <functional>
#include <string>
#include "QxEngine.h"
#include "QxTestingTransport.h"
#include "QxTestingTables.h"
#include "QxProtocols.h"

// US-133: the Q* engine on hand-written tables. The semantics are those of
// nut_repo/drivers/nutdrv_qx.c: qx_process()/qx_process_answer()/ups_infoval_set()
// (4869-5072), qx_ups_walk() (4489-4783), upsdrv_updateinfo() (3525-3601).

void setUp(void) {}
void tearDown(void) {}

// A claim that runs the body of the test with the data bound (process() and infovalSet()
// write UPSData only inside a public call of the engine)
static std::function<bool(QxEngine&)> g_body;
static bool runBody(QxEngine& qx) { return g_body ? g_body(qx) : false; }

static size_t countSent(const QxTestingTransport& t, const char* cmd) {
    size_t n = 0;
    for (const auto& s : t.sent) {
        if (s == cmd) n++;
    }
    return n;
}

// --- Table 1: processing of one row -----------------------------------------------------

//    > [X1\r]
//    < [(123.4 05 ABC  #\r]
//       01234567890123456
static int statusPre(QxEngine& qx, const QxItem& item, char* value, size_t len) {
    (void)item;
    const char c = qx.value()[0];
    if (c == '1') snprintf(value, len, "OL");
    else if (c == '0') snprintf(value, len, "!OL");
    else return -1;
    return 0;
}

static int alarmPre(QxEngine& qx, const QxItem& item, char* value, size_t len) {
    (void)item;
    const char c = qx.value()[0];
    if (c != '0' && c != '1') return -1;
    snprintf(value, len, "%s", c == '1' ? "Test alarm!" : "");
    return 0;
}

static int failPre(QxEngine& qx, const QxItem& item, char* value, size_t len) {
    (void)qx; (void)item; (void)value; (void)len;
    return -1;
}

static const QxItem T1[] = {
    { "input.voltage", 0, nullptr, "X1\r", 17, '(', 1, 5, "%.1f", 0, nullptr, nullptr, nullptr },
    { "ups.load", 0, nullptr, "X1\r", 17, '(', 7, 8, "%.0f", 0, nullptr, nullptr, nullptr },
    { "device.model", 0, nullptr, "X1\r", 17, '(', 10, 15, "%s", QX_FLAG_TRIM, nullptr, nullptr, nullptr },
    { "device.serial", 0, nullptr, "X1\r", 17, '(', 10, 0, "%s", 0, nullptr, nullptr, nullptr },
    { "experimental.x", 0, nullptr, "X1\r", 17, '(', 1, 5, "%s", QX_FLAG_NONUT, nullptr, nullptr, nullptr },
    { "battery.charge", 0, nullptr, "X1\r", 17, '(', 1, 5, "%.0f", 0, nullptr, nullptr, failPre },
    { "bad.bounds", 0, nullptr, "X1\r", 17, '(', 5, 1, "%s", 0, nullptr, nullptr, nullptr },
    { "ups.status", 0, nullptr, "S1\r", 4, '(', 1, 1, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, statusPre },
    { "ups.alarm", 0, nullptr, "S1\r", 4, '(', 2, 2, nullptr, 0, nullptr, nullptr, alarmPre },
    { "beeper.toggle", 0, nullptr, "Q\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { nullptr, 0, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr }
};

static const QxTesting T1_TESTING[] = {
    { "X1\r", "(123.4 05 ABC  #\r", -1 },
    { "S1\r", "(11\r", -1 },
    { nullptr, nullptr, 0 }
};

static const QxSubdriver SUB1 = { "Test1 0.01", runBody, T1, nullptr, nullptr, "ACK", "NAK\r" };

struct Fixture1 {
    QxTestingTransport t{T1_TESTING, SUB1.rejected};
    QxEngine e{SUB1, t};
    UPSData d;
    // process + infovalSet of the row named name; returns the infovalSet result, or -2
    // when process() failed
    int row(const char* name) {
        int result = 0;
        g_body = [&](QxEngine& qx) {
            int r = qx.findNutInfo(name, 0, 0);
            TEST_ASSERT_TRUE_MESSAGE(r >= 0, name);
            if (qx.process(r)) { result = -2; return true; }
            result = qx.infovalSet(r);
            return true;
        };
        e.claim(d);
        g_body = nullptr;
        return result;
    }
};

void test_valid_answer_values_formatted(void) {
    Fixture1 f;
    TEST_ASSERT_EQUAL_INT(1, f.row("input.voltage"));
    TEST_ASSERT_EQUAL_STRING("123.4", f.d.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_INT(1, f.row("ups.load"));
    TEST_ASSERT_EQUAL_STRING("5", f.d.get("ups.load").c_str()); // "05" with %.0f
    TEST_ASSERT_EQUAL_INT(1, f.row("device.model"));
    TEST_ASSERT_EQUAL_STRING("ABC", f.d.get("device.model").c_str()); // "ABC  #" trimmed of "# "
    TEST_ASSERT_EQUAL_INT(1, f.row("device.serial"));
    TEST_ASSERT_EQUAL_STRING("ABC  #", f.d.get("device.serial").c_str()); // to = 0: up to '\r'
}

void test_short_answer_discarded(void) {
    Fixture1 f;
    f.t.override("X1\r", "(123.4\r");
    TEST_ASSERT_EQUAL_INT(-2, f.row("input.voltage"));
    TEST_ASSERT_NULL(f.d.find("input.voltage"));
}

void test_wrong_leading_character_discarded(void) {
    Fixture1 f;
    f.t.override("X1\r", "#123.4 05 ABC  #\r");
    TEST_ASSERT_EQUAL_INT(-2, f.row("input.voltage"));
    TEST_ASSERT_NULL(f.d.find("input.voltage"));
}

void test_rejected_answer_discarded_case_insensitive(void) {
    Fixture1 f;
    f.t.override("X1\r", "nak\r");
    TEST_ASSERT_EQUAL_INT(-2, f.row("input.voltage"));
    TEST_ASSERT_NULL(f.d.find("input.voltage"));
}

void test_to_before_from_discarded(void) {
    Fixture1 f;
    TEST_ASSERT_EQUAL_INT(-2, f.row("bad.bounds"));
    TEST_ASSERT_NULL(f.d.find("bad.bounds"));
}

void test_non_numeric_value_discarded(void) {
    Fixture1 f;
    f.t.override("X1\r", "(12a.4 05 ABC  #\r");
    TEST_ASSERT_EQUAL_INT(-1, f.row("input.voltage"));
    TEST_ASSERT_NULL(f.d.find("input.voltage"));
}

void test_preprocess_failure_discarded(void) {
    Fixture1 f;
    TEST_ASSERT_EQUAL_INT(-1, f.row("battery.charge"));
    TEST_ASSERT_NULL(f.d.find("battery.charge"));
}

void test_nonut_returns_one_without_writing(void) {
    Fixture1 f;
    TEST_ASSERT_EQUAL_INT(1, f.row("experimental.x"));
    TEST_ASSERT_NULL(f.d.find("experimental.x"));
}

void test_transport_error_discarded(void) {
    Fixture1 f;
    f.t.failNext(1);
    TEST_ASSERT_EQUAL_INT(-2, f.row("input.voltage"));
    TEST_ASSERT_NULL(f.d.find("input.voltage"));
}

void test_status_and_alarm_rows_return_zero(void) {
    Fixture1 f;
    TEST_ASSERT_EQUAL_INT(0, f.row("ups.status"));
    TEST_ASSERT_TRUE(f.e.status() & QX_STATUS(QX_OL));
    TEST_ASSERT_EQUAL_INT(0, f.row("ups.alarm"));
    TEST_ASSERT_NULL(f.d.find("ups.status"));
    TEST_ASSERT_NULL(f.d.find("ups.alarm")); // alarms are applied by a full update only
}

void test_update_status_set_clear_and_unknown(void) {
    Fixture1 f;
    f.e.updateStatus("OL");
    f.e.updateStatus("lb");
    TEST_ASSERT_EQUAL_UINT(QX_STATUS(QX_OL) | QX_STATUS(QX_LB), f.e.status());
    f.e.updateStatus("!OL");
    TEST_ASSERT_EQUAL_UINT(QX_STATUS(QX_LB), f.e.status());
    f.e.updateStatus("CAL");
    f.e.updateStatus("FSD");
    TEST_ASSERT_EQUAL_UINT(QX_STATUS(QX_LB) | QX_STATUS(QX_CALIB) | QX_STATUS(QX_FSD), f.e.status());
    f.e.updateStatus("NOTASTATUS");
    f.e.updateStatus("!NOTASTATUS");
    TEST_ASSERT_EQUAL_UINT(QX_STATUS(QX_LB) | QX_STATUS(QX_CALIB) | QX_STATUS(QX_FSD), f.e.status());
}

void test_find_nut_info_flags(void) {
    Fixture1 f;
    TEST_ASSERT_EQUAL_INT(0, f.e.findNutInfo("INPUT.VOLTAGE", 0, 0));
    TEST_ASSERT_EQUAL_INT(9, f.e.findNutInfo("beeper.toggle", QX_FLAG_CMD, 0));
    TEST_ASSERT_EQUAL_INT(-1, f.e.findNutInfo("beeper.toggle", QX_FLAG_CMD, QX_FLAG_CMD));
    TEST_ASSERT_EQUAL_INT(-1, f.e.findNutInfo("input.voltage", QX_FLAG_CMD, 0));
    TEST_ASSERT_EQUAL_INT(-1, f.e.findNutInfo("no.such.var", 0, 0));
    f.e.addFlags(9, QX_FLAG_SKIP);
    TEST_ASSERT_EQUAL_INT(-1, f.e.findNutInfo("beeper.toggle", QX_FLAG_CMD, QX_FLAG_SKIP));
    f.e.clearFlags(9, QX_FLAG_SKIP);
    TEST_ASSERT_EQUAL_INT(9, f.e.findNutInfo("beeper.toggle", QX_FLAG_CMD, QX_FLAG_SKIP));
}

void test_same_value_not_written_again(void) {
    Fixture1 f;
    f.row("input.voltage");
    uint32_t writes = f.d.writes();
    f.row("input.voltage");
    TEST_ASSERT_EQUAL_UINT32(writes, f.d.writes());
}

void test_getinfo_outside_a_call_is_a_no_op(void) {
    Fixture1 f;
    f.d.set("input.voltage", "1.0");
    TEST_ASSERT_NULL(f.e.getinfo("input.voltage"));
    f.e.setinfo("input.voltage", "2.0");
    TEST_ASSERT_EQUAL_STRING("1.0", f.d.get("input.voltage").c_str());
}

// --- Table 2: walk and poll classes -----------------------------------------------------

//    > [A\r]                > [B\r]             > [C\r]
//    < [(1-250\r]           < [#FW1 900\r]      < [#X\r] (shorter than expected)
//       0123456                012345678
static int trimPre(QxEngine& qx, const QxItem& item, char* value, size_t len) {
    (void)item;
    const char c = qx.value()[0];
    if (c == 't') snprintf(value, len, "TRIM");
    else if (c == '-') value[0] = '\0';
    else return -1;
    return 0;
}

static const QxItem T2[] = {
    { "ups.status", 0, nullptr, "A\r", 7, '(', 1, 1, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, statusPre },
    { "ups.status", 0, nullptr, "A\r", 7, '(', 2, 2, nullptr, QX_FLAG_QUICK_POLL, nullptr, nullptr, trimPre },
    { "ups.load", 0, nullptr, "A\r", 7, '(', 3, 4, "%.0f", 0, nullptr, nullptr, nullptr },
    { "ups.alarm", 0, nullptr, "A\r", 7, '(', 5, 5, nullptr, 0, nullptr, nullptr, alarmPre },
    { "ups.firmware", 0, nullptr, "B\r", 9, '#', 1, 3, "%s", QX_FLAG_STATIC, nullptr, nullptr, nullptr },
    { "input.transfer.high", 0, nullptr, "B\r", 9, '#', 5, 7, "%.0f", QX_FLAG_SEMI_STATIC, nullptr, nullptr, nullptr },
    { "ups.serial", 0, nullptr, "C\r", 10, '#', 1, 1, "%s", 0, nullptr, nullptr, nullptr },
    { "ups.delay.shutdown", QX_ST_FLAG_RW, nullptr, nullptr, 0, 0, 0, 0, "30", QX_FLAG_ABSENT | QX_FLAG_SETVAR, nullptr, nullptr, nullptr },
    { "beeper.toggle", 0, nullptr, "Q\r", 0, 0, 0, 0, nullptr, QX_FLAG_CMD, nullptr, nullptr, nullptr },
    { nullptr, 0, nullptr, nullptr, 0, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr }
};

static const QxTesting T2_TESTING[] = {
    { "A\r", "(1-250\r", -1 },
    { "B\r", "#FW1 900\r", -1 },
    { "C\r", "#X\r", -1 },
    { nullptr, nullptr, 0 }
};

static const QxSubdriver SUB2 = { "Test2 0.02", nullptr, T2, nullptr, nullptr, "ACK", nullptr };

struct Fixture2 {
    QxTestingTransport t{T2_TESTING};
    QxEngine e{SUB2, t};
    UPSData d;
    void init(uint32_t now = 1000) {
        TEST_ASSERT_TRUE(e.initInfo(d, now));
        t.sent.clear();
    }
    String status() const { return UPSData::computeUPSStatusString(d); }
};

void test_init_info_values_absent_default_and_one_command_per_group(void) {
    Fixture2 f;
    TEST_ASSERT_TRUE(f.e.initInfo(f.d, 1000));
    TEST_ASSERT_EQUAL_STRING("Test2 0.02", f.d.get("driver.version.data").c_str());
    TEST_ASSERT_EQUAL_STRING("25", f.d.get("ups.load").c_str());
    TEST_ASSERT_EQUAL_STRING("FW1", f.d.get("ups.firmware").c_str());
    TEST_ASSERT_EQUAL_STRING("900", f.d.get("input.transfer.high").c_str());
    TEST_ASSERT_EQUAL_STRING("30", f.d.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_NULL(f.d.find("ups.serial"));
    // One command per group of rows with the same command; the CMD row sends nothing
    TEST_ASSERT_EQUAL_UINT(3, f.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("A\r", f.t.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("B\r", f.t.sent[1].c_str());
    TEST_ASSERT_EQUAL_STRING("C\r", f.t.sent[2].c_str());
    // The status is published by updateInfo(), not by initInfo()
    TEST_ASSERT_NULL(f.d.find("ups.status.ac_present"));
}

void test_init_info_skips_rows_already_set(void) {
    Fixture2 f;
    f.d.set("ups.firmware", "pre");
    f.d.set("input.transfer.high", "100");
    f.d.set("ups.delay.shutdown", "60");
    TEST_ASSERT_TRUE(f.e.initInfo(f.d, 1000));
    TEST_ASSERT_EQUAL_UINT(0, countSent(f.t, "B\r"));
    TEST_ASSERT_EQUAL_STRING("pre", f.d.get("ups.firmware").c_str());
    TEST_ASSERT_EQUAL_STRING("60", f.d.get("ups.delay.shutdown").c_str());
}

void test_init_info_fails_when_a_quick_poll_row_fails(void) {
    Fixture2 f;
    f.t.override("A\r", "(\r");
    TEST_ASSERT_FALSE(f.e.initInfo(f.d, 1000));
}

void test_row_failed_in_init_is_skipped_for_good(void) {
    Fixture2 f;
    f.init();
    f.e.noteDataChanged();
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_UINT(0, countSent(f.t, "C\r"));
}

void test_poll_classes_and_pollfreq(void) {
    Fixture2 f;
    f.init(1000);
    // Quick right after initInfo: only the QUICK_POLL rows
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_UINT(1, f.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("A\r", f.t.sent[0].c_str());
    // 30 000 ms later still quick, ups.load not read again
    f.t.override("A\r", "(1-990\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 31000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("25", f.d.get("ups.load").c_str());
    // 30 001 ms: full, but STATIC never and SEMI_STATIC not without a change
    f.t.sent.clear();
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 31001) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("99", f.d.get("ups.load").c_str());
    TEST_ASSERT_EQUAL_UINT(1, f.t.sent.size());
    TEST_ASSERT_EQUAL_UINT(0, countSent(f.t, "B\r"));
    // A command/setvar forces a full update that reads SEMI_STATIC too, once
    f.t.override("B\r", "#FW2 950\r");
    f.e.noteDataChanged();
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 31002) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("950", f.d.get("input.transfer.high").c_str());
    TEST_ASSERT_EQUAL_STRING("FW1", f.d.get("ups.firmware").c_str());
    f.t.sent.clear();
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 31003) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_UINT(0, countSent(f.t, "B\r"));
}

void test_stale_after_five_failures_and_reestablished(void) {
    Fixture2 f;
    f.init();
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    uint32_t writes = f.d.writes();
    f.t.failNext(5);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Failed);
    }
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Stale);
    TEST_ASSERT_EQUAL_UINT32(writes, f.d.writes());
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_TRUE(f.e.reestablished());
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_FALSE(f.e.reestablished());
}

// As upsdrv_updateinfo(): "re-established" once retry > MAXTRIES, i.e. after 4 failures,
// one before the data go stale; 3 failures are silent
void test_reestablished_threshold_as_upstream(void) {
    Fixture2 f;
    f.init();
    f.t.failNext(3);
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Failed);
    }
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_FALSE(f.e.reestablished());
    f.t.failNext(4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Failed);
    }
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_TRUE(f.e.reestablished());
}

void test_status_bits_written_as_flags(void) {
    Fixture2 f;
    f.init();
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("1", f.d.get("ups.status.ac_present").c_str());
    TEST_ASSERT_EQUAL_STRING("0", f.d.get("ups.status.fsd").c_str());
    TEST_ASSERT_EQUAL_STRING("OL", f.status().c_str());
    f.t.override("A\r", "(0-250\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("OB", f.status().c_str());
}

// A bit set by a quick update stays until the next full update (ups_status is cleared only
// there, nutdrv_qx.c:3545)
void test_quick_bit_persists_until_full_update(void) {
    Fixture2 f;
    f.init(1000);
    f.t.override("A\r", "(1t250\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("OL TRIM", f.status().c_str());
    f.t.override("A\r", "(1-250\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 4000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("OL TRIM", f.status().c_str());
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("OL", f.status().c_str());
}

void test_alarm_applied_by_full_update_only(void) {
    Fixture2 f;
    f.init(1000);
    f.t.override("A\r", "(1-251\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_NULL(f.d.find("ups.alarm"));
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("Test alarm!", f.d.get("ups.alarm").c_str());
    TEST_ASSERT_EQUAL_STRING("ALARM OL", f.status().c_str());
    f.t.override("A\r", "(1-250\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, 80000) == QxEngine::Update::Ok);
    TEST_ASSERT_NULL(f.d.find("ups.alarm"));
    TEST_ASSERT_EQUAL_STRING("OL", f.status().c_str());
}

void test_pollfreq_across_millis_wrap(void) {
    const uint32_t start = 0xFFFFFFFFUL - 9999UL;
    Fixture2 f;
    f.init(start);
    f.t.override("A\r", "(1-990\r");
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, start + 30000UL) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("25", f.d.get("ups.load").c_str()); // quick
    TEST_ASSERT_TRUE(f.e.updateInfo(f.d, start + 30001UL) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("99", f.d.get("ups.load").c_str()); // full
}

// --- Walk one Q* command at a time (US-136) ----------------------------------------------

static void assertSameData(const UPSData& a, const UPSData& b, const char* what) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(a.getAll().size(), b.getAll().size(), what);
    for (const auto& p : a.getAll()) {
        const String* v = b.find(p.key.c_str());
        TEST_ASSERT_NOT_NULL_MESSAGE(v, p.key.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(p.value.c_str(), v->c_str(), p.key.c_str());
    }
}

// Runs initInfoStep() until done, checking that each call sends at most one command
static QxEngine::Phase initBySteps(QxEngine& e, QxTestingTransport& t, UPSData& d, uint32_t now, int& steps) {
    QxEngine::Phase p;
    steps = 0;
    do {
        size_t before = t.sent.size();
        p = e.initInfoStep(d, now);
        TEST_ASSERT_TRUE(t.sent.size() - before <= 1);
        steps++;
    } while (p == QxEngine::Phase::Pending && steps < 100);
    return p;
}

static QxEngine::Update updateBySteps(QxEngine& e, QxTestingTransport& t, UPSData& d, uint32_t now) {
    QxEngine::Update u;
    int steps = 0;
    do {
        size_t before = t.sent.size();
        u = e.updateInfoStep(d, now);
        TEST_ASSERT_TRUE(t.sent.size() - before <= 1);
        steps++;
    } while (u == QxEngine::Update::Pending && steps < 100);
    return u;
}

void test_steps_match_the_whole_walk_on_every_ported_protocol(void) {
    struct P { const QxSubdriver* sub; const QxTesting* table; } protocols[] = {
        { &QX_MEGATEC, MEGATEC_TESTING }, { &QX_Q1, Q1_TESTING }, { &QX_MUSTEK, MUSTEK_TESTING },
        { &QX_MEGATEC_OLD, MEGATEC_OLD_TESTING }, { &QX_ZINTO, ZINTO_TESTING },
        { &QX_INNOVATAE, INNOVATAE_TESTING },
    };
    for (const auto& p : protocols) {
        QxTestingTransport ta(p.table, p.sub->rejected), tb(p.table, p.sub->rejected);
        QxEngine a(*p.sub, ta), b(*p.sub, tb);
        UPSData da, db;
        TEST_ASSERT_TRUE_MESSAGE(a.claim(da), p.sub->name);
        TEST_ASSERT_TRUE_MESSAGE(b.claim(db), p.sub->name);
        a.initUps();
        b.initUps();
        TEST_ASSERT_TRUE_MESSAGE(a.initInfo(da, 0), p.sub->name);
        int steps = 0;
        TEST_ASSERT_TRUE_MESSAGE(initBySteps(b, tb, db, 0, steps) == QxEngine::Phase::Ok, p.sub->name);
        TEST_ASSERT_TRUE_MESSAGE(steps > 1, p.sub->name);
        TEST_ASSERT_FALSE(b.walkPending());
        // Quick, then full (after pollfreq)
        const uint32_t times[] = { 2000, 40000 };
        for (uint32_t now : times) {
            TEST_ASSERT_TRUE_MESSAGE(a.updateInfo(da, now) == QxEngine::Update::Ok, p.sub->name);
            TEST_ASSERT_TRUE_MESSAGE(updateBySteps(b, tb, db, now) == QxEngine::Update::Ok, p.sub->name);
        }
        assertSameData(da, db, p.sub->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(a.status(), b.status(), p.sub->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(ta.sent.size(), tb.sent.size(), p.sub->name);
    }
}

void test_init_step_sends_one_command_per_group(void) {
    Fixture2 f;
    // A, B, C: three commands, three steps with a command; rows sharing A send nothing more
    TEST_ASSERT_TRUE(f.e.initInfoStep(f.d, 1000) == QxEngine::Phase::Pending);
    TEST_ASSERT_EQUAL_UINT(1, f.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("A\r", f.t.sent[0].c_str());
    TEST_ASSERT_TRUE(f.e.walkPending());
    TEST_ASSERT_TRUE(f.e.initInfoStep(f.d, 1000) == QxEngine::Phase::Pending);
    TEST_ASSERT_EQUAL_UINT(2, f.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("B\r", f.t.sent[1].c_str());
    QxEngine::Phase p;
    do {
        p = f.e.initInfoStep(f.d, 1000);
    } while (p == QxEngine::Phase::Pending);
    TEST_ASSERT_TRUE(p == QxEngine::Phase::Ok);
    TEST_ASSERT_EQUAL_UINT(3, f.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("25", f.d.get("ups.load").c_str());
    TEST_ASSERT_EQUAL_STRING("FW1", f.d.get("ups.firmware").c_str());
}

void test_update_step_is_pending_until_the_walk_ends(void) {
    Fixture2 f;
    f.init(1000);
    f.e.noteDataChanged(); // full walk with the semi-static B
    TEST_ASSERT_TRUE(f.e.updateInfoStep(f.d, 2000) == QxEngine::Update::Pending);
    // Nothing committed yet: the status is written at the end of the walk
    TEST_ASSERT_NULL(f.d.find("ups.status.ac_present"));
    TEST_ASSERT_TRUE(updateBySteps(f.e, f.t, f.d, 2000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("OL", f.status().c_str());
    TEST_ASSERT_EQUAL_UINT(1, countSent(f.t, "B\r"));
}

void test_abort_walk_restarts_init_with_the_same_result(void) {
    Fixture2 a;
    TEST_ASSERT_TRUE(a.e.initInfo(a.d, 1000));
    Fixture2 b;
    TEST_ASSERT_TRUE(b.e.initInfoStep(b.d, 1000) == QxEngine::Phase::Pending);
    TEST_ASSERT_TRUE(b.e.initInfoStep(b.d, 1000) == QxEngine::Phase::Pending);
    b.e.abortWalk();
    TEST_ASSERT_FALSE(b.e.walkPending());
    int steps = 0;
    TEST_ASSERT_TRUE(initBySteps(b.e, b.t, b.d, 1000, steps) == QxEngine::Phase::Ok);
    assertSameData(a.d, b.d, "abort");
}

void test_update_steps_go_stale_like_the_whole_walk(void) {
    Fixture2 f;
    f.init();
    TEST_ASSERT_TRUE(updateBySteps(f.e, f.t, f.d, 1000) == QxEngine::Update::Ok);
    f.t.failNext(5);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(updateBySteps(f.e, f.t, f.d, 1000) == QxEngine::Update::Failed);
    }
    TEST_ASSERT_TRUE(updateBySteps(f.e, f.t, f.d, 1000) == QxEngine::Update::Stale);
    TEST_ASSERT_TRUE(updateBySteps(f.e, f.t, f.d, 1000) == QxEngine::Update::Ok);
    TEST_ASSERT_TRUE(f.e.reestablished());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_answer_values_formatted);
    RUN_TEST(test_short_answer_discarded);
    RUN_TEST(test_wrong_leading_character_discarded);
    RUN_TEST(test_rejected_answer_discarded_case_insensitive);
    RUN_TEST(test_to_before_from_discarded);
    RUN_TEST(test_non_numeric_value_discarded);
    RUN_TEST(test_preprocess_failure_discarded);
    RUN_TEST(test_nonut_returns_one_without_writing);
    RUN_TEST(test_transport_error_discarded);
    RUN_TEST(test_status_and_alarm_rows_return_zero);
    RUN_TEST(test_update_status_set_clear_and_unknown);
    RUN_TEST(test_find_nut_info_flags);
    RUN_TEST(test_same_value_not_written_again);
    RUN_TEST(test_getinfo_outside_a_call_is_a_no_op);
    RUN_TEST(test_init_info_values_absent_default_and_one_command_per_group);
    RUN_TEST(test_init_info_skips_rows_already_set);
    RUN_TEST(test_init_info_fails_when_a_quick_poll_row_fails);
    RUN_TEST(test_row_failed_in_init_is_skipped_for_good);
    RUN_TEST(test_poll_classes_and_pollfreq);
    RUN_TEST(test_stale_after_five_failures_and_reestablished);
    RUN_TEST(test_reestablished_threshold_as_upstream);
    RUN_TEST(test_status_bits_written_as_flags);
    RUN_TEST(test_quick_bit_persists_until_full_update);
    RUN_TEST(test_alarm_applied_by_full_update_only);
    RUN_TEST(test_pollfreq_across_millis_wrap);
    RUN_TEST(test_steps_match_the_whole_walk_on_every_ported_protocol);
    RUN_TEST(test_init_step_sends_one_command_per_group);
    RUN_TEST(test_update_step_is_pending_until_the_walk_ends);
    RUN_TEST(test_abort_walk_restarts_init_with_the_same_result);
    RUN_TEST(test_update_steps_go_stale_like_the_whole_walk);
    return UNITY_END();
}
#endif
#endif
