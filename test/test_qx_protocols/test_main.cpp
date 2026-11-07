#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <functional>
#include "QxEngine.h"
#include "QxProtocols.h"
#include "QxBlazerCommon.h"
#include "QxTestingTransport.h"
#include "QxTestingTables.h"

// US-133/US-134: the ported protocols replayed with the testing tables of NUT, in the
// order of nutdrv_qx: claim -> initups -> initinfo -> updateinfo. Expected values come
// from the from/to positions of the upstream tables on the testing answers.

void setUp(void) {}
void tearDown(void) {}

// The testing_t tables (MEGATEC_TESTING, Q1_TESTING, MUSTEK_TESTING, ...) are in
// test/mocks/QxTestingTables.h, shared with test_qx_probe

struct Run {
    QxTestingTransport t;
    QxEngine e;
    UPSData d;
    Run(const QxSubdriver& sub, const QxTesting* table, const QxOptions& opt = QxOptions())
        : t(table, sub.rejected), e(sub, t, opt) {}
    // claim -> initups -> initinfo (t = 0 ms) -> first updateinfo (t = 2 000 ms, quick)
    bool start() {
        if (!e.claim(d)) return false;
        e.initUps();
        if (!e.initInfo(d, 0)) return false;
        return e.updateInfo(d, 2000) == QxEngine::Update::Ok;
    }
    const char* get(const char* key) {
        const String* v = d.find(key);
        return v ? v->c_str() : "(missing)";
    }
    String status() const { return UPSData::computeUPSStatusString(d); }
};

// The Q1 answer with the given input/output voltage (5 chars) and status bits (8 chars)
static const char* q1Answer(const char* vin, const char* vout, const char* bits) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "(%s 195.0 %s 014 49.0 22.7 30.0 %s\r", vin, vout, bits);
    return buf;
}

static int rowOf(const QxEngine& e, const char* name, int from) {
    for (size_t r = 0; r < e.rowCount(); r++) {
        const QxItem& it = e.item((int)r);
        if (QxEngine::equalsIgnoreCase(it.info_type, name) && it.from == from) return (int)r;
    }
    return -1;
}

// --- Replay of testing_t ---------------------------------------------------------------

void test_megatec_testing_values(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", r.get("driver.version.data"));
    // (215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r
    //  01234567890123456789012345678901234567890123456
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage"));       // 1-5
    TEST_ASSERT_EQUAL_STRING("195.0", r.get("input.voltage.fault")); // 7-11
    TEST_ASSERT_EQUAL_STRING("230.0", r.get("output.voltage"));      // 13-17
    TEST_ASSERT_EQUAL_STRING("14", r.get("ups.load"));               // 19-21 "014", %.0f
    TEST_ASSERT_EQUAL_STRING("49.0", r.get("input.frequency"));      // 23-26
    TEST_ASSERT_EQUAL_STRING("22.7", r.get("battery.voltage"));      // 28-31, qx_multiply_battvolt passes it on
    TEST_ASSERT_EQUAL_STRING("30.0", r.get("ups.temperature"));      // 33-36
    TEST_ASSERT_EQUAL_STRING("online", r.get("ups.type"));           // 42 '0'
    TEST_ASSERT_EQUAL_STRING("disabled", r.get("ups.beeper.status")); // 45 '0'
    // #230.0 000 024.0 50.0\r
    // 0123456789012345678901
    TEST_ASSERT_EQUAL_STRING("230", r.get("input.voltage.nominal"));  // 1-5, %.0f
    TEST_ASSERT_EQUAL_STRING("0.0", r.get("input.current.nominal"));  // 7-9 "000", %.1f
    TEST_ASSERT_EQUAL_STRING("24.0", r.get("battery.voltage.nominal")); // 11-15
    TEST_ASSERT_EQUAL_STRING("50", r.get("input.frequency.nominal")); // 17-20, %.0f
    // #NOT_A_LIVE_UPS  TESTING    TESTING   \r
    // 012345678901234567890123456789012345678
    TEST_ASSERT_EQUAL_STRING("NOT_A_L", r.get("device.mfr"));   // 1-7, trimmed
    TEST_ASSERT_EQUAL_STRING("IVE_UPS ", r.get("ups.serial")); // 8-15, no trim
    TEST_ASSERT_EQUAL_STRING("TESTI", r.get("device.model"));   // 17-21
    TEST_ASSERT_EQUAL_STRING("NG", r.get("battery.runtime"));   // 22-26 "NG   " trimmed, %s: as NUT
    TEST_ASSERT_EQUAL_STRING("TESTING", r.get("ups.firmware")); // 28-end, trimmed
    // QX_FLAG_ABSENT server-side vars: DEFAULT_ONDELAY / DEFAULT_OFFDELAY
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start"));
    TEST_ASSERT_EQUAL_STRING("30", r.get("ups.delay.shutdown"));
    // Battery guesstimation (qx_initbattery): low = 104*24/120, high = 130*24/120,
    // charge = 100*(22.7-20.8)/(26-20.8) = 36.5
    TEST_ASSERT_EQUAL_STRING("20.80", r.get("battery.voltage.low"));
    TEST_ASSERT_EQUAL_STRING("26.00", r.get("battery.voltage.high"));
    TEST_ASSERT_EQUAL_STRING("37", r.get("battery.charge"));
    TEST_ASSERT_EQUAL_STRING("OL", r.status().c_str());
    TEST_ASSERT_NULL(r.d.find("ups.alarm"));
}

void test_megatec_claim_sends_status_then_vendor(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.e.claim(r.d));
    TEST_ASSERT_EQUAL_UINT(2, r.t.sent.size());
    TEST_ASSERT_EQUAL_STRING("Q1\r", r.t.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("I\r", r.t.sent[1].c_str());
}

void test_megatec_battery_charge_recomputed_by_full_update(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    // 24.3 V: 100*(24.3-20.8)/5.2 = 67.3
    r.t.override("Q1\r", "(215.0 195.0 230.0 014 49.0 24.3 30.0 00000000\r");
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("24.3", r.get("battery.voltage"));
    TEST_ASSERT_EQUAL_STRING("67", r.get("battery.charge"));
}

void test_q1_testing_values(void) {
    Run r(QX_Q1, Q1_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("Q1 0.08", r.get("driver.version.data"));
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage"));
    TEST_ASSERT_EQUAL_STRING("195.0", r.get("input.voltage.fault"));
    TEST_ASSERT_EQUAL_STRING("230.0", r.get("output.voltage"));
    TEST_ASSERT_EQUAL_STRING("14", r.get("ups.load"));
    TEST_ASSERT_EQUAL_STRING("49.0", r.get("input.frequency"));
    TEST_ASSERT_EQUAL_STRING("22.7", r.get("battery.voltage"));
    TEST_ASSERT_EQUAL_STRING("30.0", r.get("ups.temperature"));
    TEST_ASSERT_EQUAL_STRING("online", r.get("ups.type"));
    TEST_ASSERT_EQUAL_STRING("disabled", r.get("ups.beeper.status"));
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start"));
    TEST_ASSERT_EQUAL_STRING("30", r.get("ups.delay.shutdown"));
    // No F/I rows, no nominal battery voltage: nothing to guess
    TEST_ASSERT_NULL(r.d.find("input.voltage.nominal"));
    TEST_ASSERT_NULL(r.d.find("ups.firmware"));
    TEST_ASSERT_NULL(r.d.find("battery.charge"));
    TEST_ASSERT_EQUAL_STRING("OL", r.status().c_str());
    // The light claim sends only the status query
    TEST_ASSERT_EQUAL_STRING("Q1\r", r.t.sent[0].c_str());
    TEST_ASSERT_TRUE(r.t.sent.size() < 2 || r.t.sent[1] != "I\r");
}

void test_table_row_counts(void) {
    QxTestingTransport t(nullptr);
    QxEngine m(QX_MEGATEC, t);
    QxEngine q(QX_Q1, t);
    TEST_ASSERT_EQUAL_UINT(36, m.rowCount());
    TEST_ASSERT_EQUAL_UINT(27, q.rowCount());
    // Rows of the upstream tables without the closing one
    QxEngine mu(QX_MUSTEK, t);
    QxEngine mo(QX_MEGATEC_OLD, t);
    QxEngine z(QX_ZINTO, t);
    QxEngine in(QX_INNOVATAE, t);
    TEST_ASSERT_EQUAL_UINT(34, mu.rowCount());
    TEST_ASSERT_EQUAL_UINT(34, mo.rowCount());
    TEST_ASSERT_EQUAL_UINT(34, z.rowCount());
    TEST_ASSERT_EQUAL_UINT(33, in.rowCount());
}

// --- US-134: mustek, megatec/old, zinto, innovatae ---------------------------------------

// The values shared by the four testing tables: the status answer
// (215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r and the default delays
static void assertCommonTestingValues(Run& r) {
    // (215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r
    //  01234567890123456789012345678901234567890123456
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage"));        // 1-5
    TEST_ASSERT_EQUAL_STRING("195.0", r.get("input.voltage.fault"));  // 7-11
    TEST_ASSERT_EQUAL_STRING("230.0", r.get("output.voltage"));       // 13-17
    TEST_ASSERT_EQUAL_STRING("14", r.get("ups.load"));                // 19-21 "014", %.0f
    TEST_ASSERT_EQUAL_STRING("49.0", r.get("input.frequency"));       // 23-26
    TEST_ASSERT_EQUAL_STRING("22.7", r.get("battery.voltage"));       // 28-31
    TEST_ASSERT_EQUAL_STRING("30.0", r.get("ups.temperature"));       // 33-36
    TEST_ASSERT_EQUAL_STRING("online", r.get("ups.type"));            // 42 '0'
    TEST_ASSERT_EQUAL_STRING("disabled", r.get("ups.beeper.status")); // 45 '0'
    TEST_ASSERT_EQUAL_STRING("180", r.get("ups.delay.start"));
    TEST_ASSERT_EQUAL_STRING("30", r.get("ups.delay.shutdown"));
    TEST_ASSERT_EQUAL_STRING("OL", r.status().c_str());
    TEST_ASSERT_NULL(r.d.find("ups.alarm"));
}

// mustek, megatec/old and zinto: F and I/FW? as the mustek table (I at 1-15, 17-26, 28-37)
static void assertBlazerTestingValues(const QxSubdriver& sub, const QxTesting* table, const char* version) {
    Run r(sub, table);
    TEST_ASSERT_TRUE_MESSAGE(r.start(), version);
    TEST_ASSERT_EQUAL_STRING(version, r.get("driver.version.data"));
    assertCommonTestingValues(r);
    // #230.0 000 024.0 50.0\r
    // 0123456789012345678901
    TEST_ASSERT_EQUAL_STRING("230", r.get("input.voltage.nominal"));    // 1-5, %.0f
    TEST_ASSERT_EQUAL_STRING("0.0", r.get("input.current.nominal"));    // 7-9 "000", %.1f
    TEST_ASSERT_EQUAL_STRING("24.0", r.get("battery.voltage.nominal")); // 11-15
    TEST_ASSERT_EQUAL_STRING("50", r.get("input.frequency.nominal"));   // 17-20, %.0f
    // #NOT_A_LIVE_UPS  TESTING    TESTING   \r
    // 012345678901234567890123456789012345678
    TEST_ASSERT_EQUAL_STRING("NOT_A_LIVE_UPS", r.get("device.mfr")); // 1-15, trimmed
    TEST_ASSERT_EQUAL_STRING("TESTING", r.get("device.model"));      // 17-26, trimmed
    TEST_ASSERT_EQUAL_STRING("TESTING", r.get("ups.firmware"));      // 28-37, trimmed
    // Battery guesstimation as megatec: 24 V nominal, 22.7 V
    TEST_ASSERT_EQUAL_STRING("37", r.get("battery.charge"));
}

void test_mustek_testing_values(void) {
    assertBlazerTestingValues(QX_MUSTEK, MUSTEK_TESTING, "Mustek 0.08");
}

void test_megatec_old_testing_values(void) {
    assertBlazerTestingValues(QX_MEGATEC_OLD, MEGATEC_OLD_TESTING, "Megatec/old 0.08");
}

void test_zinto_testing_values(void) {
    assertBlazerTestingValues(QX_ZINTO, ZINTO_TESTING, "Zinto 0.07");
}

void test_innovatae_testing_values(void) {
    Run r(QX_INNOVATAE, INNOVATAE_TESTING);
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("INNOVATAE 0.01", r.get("driver.version.data"));
    assertCommonTestingValues(r);
    // Static row with QX_FLAG_ABSENT: its default
    TEST_ASSERT_EQUAL_STRING("Ippon", r.get("device.mfr"));
    // #230.0 004 024.0 50.0\r
    // 0123456789012345678901
    TEST_ASSERT_EQUAL_STRING("230", r.get("input.voltage.nominal"));
    TEST_ASSERT_EQUAL_STRING("4.0", r.get("input.current.nominal")); // 7-9 "004", %.1f
    TEST_ASSERT_EQUAL_STRING("24.0", r.get("battery.voltage.nominal"));
    TEST_ASSERT_EQUAL_STRING("50", r.get("input.frequency.nominal"));
    // The testing answer to I is 37 bytes ("#" + 27 x "2" + "R1.00.48" + "\r"), shorter
    // than answer_len 38: qx_process_answer() rejects it and the walk skips the row, as in NUT
    TEST_ASSERT_NULL(r.d.find("ups.firmware"));
    TEST_ASSERT_TRUE(r.e.flags(r.e.findNutInfo("ups.firmware", 0, 0)) & QX_FLAG_SKIP);
}

// The commands each claim sends: blazer_claim_light only the status query, blazer_claim
// the status and the vendor query
static void assertClaimSends(const QxSubdriver& sub, const QxTesting* table, const char* first, const char* second) {
    Run r(sub, table);
    TEST_ASSERT_TRUE_MESSAGE(r.e.claim(r.d), sub.name);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(second ? 2 : 1, r.t.sent.size(), sub.name);
    TEST_ASSERT_EQUAL_STRING(first, r.t.sent[0].c_str());
    if (second) TEST_ASSERT_EQUAL_STRING(second, r.t.sent[1].c_str());
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage"));
}

void test_new_protocols_claim_commands(void) {
    assertClaimSends(QX_MUSTEK, MUSTEK_TESTING, "QS\r", nullptr);
    assertClaimSends(QX_MEGATEC_OLD, MEGATEC_OLD_TESTING, "D\r", nullptr);
    assertClaimSends(QX_ZINTO, ZINTO_TESTING, "Q1\r", "FW?\r");
    assertClaimSends(QX_INNOVATAE, INNOVATAE_TESTING, "Q1\r", nullptr);
}

// Innovatae rejects a query with "NAK\r" (its subdriver_t rejected string)
void test_innovatae_nak_rejected(void) {
    Run r(QX_INNOVATAE, INNOVATAE_TESTING);
    r.t.override("Q1\r", "NAK\r");
    TEST_ASSERT_FALSE(r.e.claim(r.d));
    TEST_ASSERT_NULL(r.d.find("input.voltage"));
}

// --- Probing order and protocol names -----------------------------------------------------

// subdriver_list[] of nutdrv_qx.c:95-117 restricted to the ported protocols
void test_subdriver_list_order(void) {
    const char* expected[] = { "Mustek 0.08", "Megatec/old 0.08", "Megatec 0.10", "Zinto 0.07",
                               "INNOVATAE 0.01", "Q1 0.08" };
    size_t n = sizeof(expected) / sizeof(expected[0]);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_NOT_NULL(QX_SUBDRIVER_LIST[i]);
        TEST_ASSERT_EQUAL_STRING(expected[i], QX_SUBDRIVER_LIST[i]->name);
    }
    TEST_ASSERT_NULL(QX_SUBDRIVER_LIST[n]);
}

void test_protocol_name_match(void) {
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_MUSTEK, "mustek"));
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_MUSTEK, "MUSTEK"));
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_MEGATEC_OLD, "megatec/old"));
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_MEGATEC_OLD, "Megatec/Old"));
    TEST_ASSERT_FALSE(qxProtocolMatches(QX_MEGATEC_OLD, "megatec-old"));
    TEST_ASSERT_FALSE(qxProtocolMatches(QX_MEGATEC_OLD, "megatec"));
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_MEGATEC, "megatec"));
    TEST_ASSERT_FALSE(qxProtocolMatches(QX_MEGATEC, "Megatec 0.10"));
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_INNOVATAE, "innovatae"));
    TEST_ASSERT_TRUE(qxProtocolMatches(QX_Q1, "q1"));
    TEST_ASSERT_FALSE(qxProtocolMatches(QX_ZINTO, ""));
    TEST_ASSERT_FALSE(qxProtocolMatches(QX_ZINTO, nullptr));
    // "megatec" matches only Megatec 0.10 in the list
    int matches = 0;
    for (const QxSubdriver* const* s = QX_SUBDRIVER_LIST; *s; s++) {
        if (qxProtocolMatches(**s, "megatec")) {
            matches++;
            TEST_ASSERT_EQUAL_STRING("Megatec 0.10", (*s)->name);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, matches);
}

// --- Status (blazer_process_status_bits) -------------------------------------------------

static void assertFullStatus(const char* vin, const char* vout, const char* bits, const char* expected) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("Q1\r", q1Answer(vin, vout, bits));
    TEST_ASSERT_TRUE_MESSAGE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Ok, bits);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, r.status().c_str(), bits);
}

void test_status_on_battery(void) { assertFullStatus("215.0", "230.0", "10000000", "OB"); }
void test_status_on_battery_low(void) { assertFullStatus("215.0", "230.0", "11000000", "OB LB"); }
void test_status_boost(void) { assertFullStatus("215.0", "230.0", "00100000", "OL BOOST"); } // 230/215 = 1.07
void test_status_bypass(void) { assertFullStatus("230.0", "230.0", "00100000", "OL BYPASS"); }
void test_status_trim(void) { assertFullStatus("240.0", "220.0", "00100000", "OL TRIM"); }    // 220/240 = 0.92
// Upstream nutdrv_qx writes "OL CAL"; the shared computation puts CAL first (usbhid-ups order)
void test_status_calibration(void) { assertFullStatus("215.0", "230.0", "00000100", "CAL OL"); }

void test_status_bypass_output_too_low_fails(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("Q1\r", q1Answer("230.0", "100.0", "00100000"));
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Failed);
    TEST_ASSERT_EQUAL_STRING("OL", r.status().c_str());
}

void test_status_shutdown_active_gives_fsd_and_alarm(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("Q1\r", q1Answer("215.0", "230.0", "00000010"));
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("Shutdown imminent!", r.get("ups.alarm"));
    TEST_ASSERT_EQUAL_STRING("ALARM OL FSD", r.status().c_str());
}

void test_status_ups_failed_alarm(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("Q1\r", q1Answer("215.0", "230.0", "00010000"));
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("UPS selftest failed!", r.get("ups.alarm"));
    TEST_ASSERT_EQUAL_STRING("ALARM OL", r.status().c_str());
}

void test_beeper_enabled(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("Q1\r", q1Answer("215.0", "230.0", "00000001"));
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("enabled", r.get("ups.beeper.status"));
}

// ups.type is QX_FLAG_STATIC: read in initinfo only
void test_ups_type_line_interactive_static(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    r.t.override("Q1\r", q1Answer("215.0", "230.0", "00001000"));
    TEST_ASSERT_TRUE(r.start());
    TEST_ASSERT_EQUAL_STRING("offline / line interactive", r.get("ups.type"));
    r.t.override("Q1\r", q1Answer("215.0", "230.0", "00000000"));
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("offline / line interactive", r.get("ups.type"));
}

// --- Invalid answers ---------------------------------------------------------------------

void test_short_status_answer_not_claimed(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    r.t.override("Q1\r", "(215.0 195.0\r");
    TEST_ASSERT_FALSE(r.e.claim(r.d));
    TEST_ASSERT_NULL(r.d.find("input.voltage"));
}

void test_wrong_leading_character_not_claimed(void) {
    Run r(QX_Q1, Q1_TESTING);
    r.t.override("Q1\r", "#215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r");
    TEST_ASSERT_FALSE(r.e.claim(r.d));
    TEST_ASSERT_NULL(r.d.find("input.voltage"));
}

void test_bad_status_bit_fails_update_without_writing(void) {
    Run r(QX_MEGATEC, MEGATEC_TESTING);
    TEST_ASSERT_TRUE(r.start());
    r.t.override("Q1\r", q1Answer("215.0", "230.0", "x0000000"));
    TEST_ASSERT_TRUE(r.e.updateInfo(r.d, 4000) == QxEngine::Update::Failed);
    TEST_ASSERT_EQUAL_STRING("1", r.get("ups.status.ac_present"));
    TEST_ASSERT_EQUAL_STRING("OL", r.status().c_str());
}

// Without a vendor answer blazer_claim fails and removes input.voltage
void test_megatec_without_vendor_not_claimed(void) {
    Run r(QX_MEGATEC, MEGATEC_NO_VENDOR);
    TEST_ASSERT_FALSE(r.e.claim(r.d));
    TEST_ASSERT_NULL(r.d.find("input.voltage"));
}

// novendor: the claim stops after the status, the I rows are skipped (blazer_initups)
void test_megatec_novendor(void) {
    QxOptions opt;
    opt.novendor = true;
    Run r(QX_MEGATEC, MEGATEC_NO_VENDOR, opt);
    TEST_ASSERT_TRUE(r.start());
    for (const char* name : { "device.mfr", "ups.serial", "device.model", "battery.runtime", "ups.firmware" }) {
        int row = r.e.findNutInfo(name, 0, 0);
        TEST_ASSERT_TRUE_MESSAGE(row >= 0, name);
        TEST_ASSERT_TRUE_MESSAGE(r.e.flags(row) & QX_FLAG_SKIP, name);
    }
    for (const auto& s : r.t.sent) TEST_ASSERT_FALSE(s == "I\r");
    TEST_ASSERT_NULL(r.d.find("ups.firmware"));
    TEST_ASSERT_EQUAL_STRING("215.0", r.get("input.voltage"));
}

// norating skips the F rows
void test_megatec_norating(void) {
    QxOptions opt;
    opt.norating = true;
    Run r(QX_MEGATEC, MEGATEC_TESTING, opt);
    TEST_ASSERT_TRUE(r.start());
    for (const auto& s : r.t.sent) TEST_ASSERT_FALSE(s == "F\r");
    TEST_ASSERT_NULL(r.d.find("input.voltage.nominal"));
}

// ignoresab skips the 'Shutdown Active' bit, in both protocols
void test_ignoresab(void) {
    QxOptions opt;
    opt.ignoresab = true;
    Run m(QX_MEGATEC, MEGATEC_TESTING, opt);
    Run q(QX_Q1, Q1_TESTING, opt);
    m.e.initUps();
    q.e.initUps();
    TEST_ASSERT_TRUE(m.e.flags(rowOf(m.e, "ups.status", 44)) & QX_FLAG_SKIP);
    TEST_ASSERT_TRUE(q.e.flags(rowOf(q.e, "ups.status", 44)) & QX_FLAG_SKIP);
    TEST_ASSERT_FALSE(m.e.flags(rowOf(m.e, "ups.status", 43)) & QX_FLAG_SKIP);
    // FSD is no longer reported
    TEST_ASSERT_TRUE(m.start());
    m.t.override("Q1\r", q1Answer("215.0", "230.0", "00000010"));
    TEST_ASSERT_TRUE(m.e.updateInfo(m.d, 40000) == QxEngine::Update::Ok);
    TEST_ASSERT_EQUAL_STRING("OL", m.status().c_str());
}

// --- blazer-common command and setvar preprocess ------------------------------------------

static std::function<bool(QxEngine&)> g_body;
static bool runBody(QxEngine& qx) { return g_body ? g_body(qx) : false; }

// blazer_process_command() of the megatec row cmd, with the given delays (nullptr: unset)
// and value (the instcmd extradata)
static String command(const char* cmd, const char* offdelay, const char* ondelay, const char* value, int* rc) {
    QxSubdriver sub = QX_MEGATEC;
    sub.claim = runBody;
    QxTestingTransport t(MEGATEC_TESTING);
    QxEngine e(sub, t);
    UPSData d;
    if (offdelay) d.set("ups.delay.shutdown", offdelay);
    if (ondelay) d.set("ups.delay.start", ondelay);
    char buf[QX_SMALLBUF];
    snprintf(buf, sizeof(buf), "%s", value);
    g_body = [&](QxEngine& qx) {
        int row = qx.findNutInfo(cmd, QX_FLAG_CMD, 0);
        TEST_ASSERT_TRUE_MESSAGE(row >= 0, cmd);
        *rc = blazerProcessCommand(qx, qx.item(row), buf, sizeof(buf));
        return true;
    };
    e.claim(d);
    g_body = nullptr;
    return String(buf);
}

static bool inMegatecTesting(const String& cmd) {
    for (const QxTesting* t = MEGATEC_TESTING; t->cmd; t++) {
        if (cmd == t->cmd) return true;
    }
    return false;
}

void test_command_strings_of_megatec_testing(void) {
    int rc = 0;
    String c = command("shutdown.return", "180", "0", "", &rc);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("S03\r", c.c_str());
    TEST_ASSERT_TRUE(inMegatecTesting(c));

    c = command("shutdown.return", "120", "300", "", &rc);
    TEST_ASSERT_EQUAL_STRING("S02R0005\r", c.c_str());
    TEST_ASSERT_TRUE(inMegatecTesting(c));

    c = command("shutdown.stayoff", "30", "180", "", &rc);
    TEST_ASSERT_EQUAL_STRING("S.5R0000\r", c.c_str());
    TEST_ASSERT_TRUE(inMegatecTesting(c));

    c = command("test.battery.start", "30", "180", "240", &rc);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("T04\r", c.c_str());
    TEST_ASSERT_TRUE(inMegatecTesting(c));
}

void test_command_defaults_and_ranges(void) {
    int rc = 0;
    // Defaults offdelay 30, ondelay 180 (3 minutes)
    TEST_ASSERT_EQUAL_STRING("S.5R0003\r", command("shutdown.return", "30", "180", "", &rc).c_str());
    TEST_ASSERT_EQUAL_STRING("S.5\r", command("shutdown.return", "30", "0", "", &rc).c_str());
    TEST_ASSERT_EQUAL_STRING("S02R0000\r", command("shutdown.stayoff", "120", "180", "", &rc).c_str());
    // Missing ups.delay.* read as 0 (NUT would crash on strtol(NULL))
    TEST_ASSERT_EQUAL_STRING("S.0\r", command("shutdown.return", nullptr, nullptr, "", &rc).c_str());
    // Default test time 600 s = 10 minutes
    TEST_ASSERT_EQUAL_STRING("T10\r", command("test.battery.start", "30", "180", "", &rc).c_str());
    TEST_ASSERT_EQUAL_INT(0, rc);
    command("test.battery.start", "30", "180", "30", &rc);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    command("test.battery.start", "30", "180", "6000", &rc);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    command("shutdown.stayoff", "-6", "180", "", &rc);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    // A command without a preprocess entry is unknown to blazer_process_command()
    command("load.off", "30", "180", "", &rc);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

static int setvar(const char* var, const char* in, char* out, size_t len) {
    QxTestingTransport t(nullptr);
    QxEngine e(QX_MEGATEC, t);
    int row = e.findNutInfo(var, QX_FLAG_SETVAR, 0);
    TEST_ASSERT_TRUE_MESSAGE(row >= 0, var);
    snprintf(out, len, "%s", in);
    return blazerProcessSetvar(e, e.item(row), out, len);
}

void test_setvar_truncation(void) {
    char v[32];
    TEST_ASSERT_EQUAL_INT(0, setvar("ups.delay.start", "250", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("240", v);
    TEST_ASSERT_EQUAL_INT(0, setvar("ups.delay.shutdown", "59", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("54", v);
    TEST_ASSERT_EQUAL_INT(0, setvar("ups.delay.shutdown", "125", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("120", v);
    TEST_ASSERT_EQUAL_INT(-1, setvar("ups.delay.shutdown", "-1", v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(-1, setvar("ups.delay.start", "", v, sizeof(v)));
}

void test_delay_ranges(void) {
    TEST_ASSERT_EQUAL_STRING("0", BLAZER_R_ONDELAY[0].value);
    TEST_ASSERT_EQUAL_STRING("599940", BLAZER_R_ONDELAY[1].value);
    TEST_ASSERT_EQUAL_STRING("", BLAZER_R_ONDELAY[2].value);
    TEST_ASSERT_EQUAL_STRING("12", BLAZER_R_OFFDELAY[0].value);
    TEST_ASSERT_EQUAL_STRING("600", BLAZER_R_OFFDELAY[1].value);
}

// --- dfl guard: the formats are passed to snprintf ---------------------------------------

static bool dflAllowed(const QxItem& it) {
    const char* f = it.dfl;
    if (!f || strcmp(f, "%s") == 0 || strcmp(f, "%f") == 0) return true;
    if (it.qxflags & QX_FLAG_ABSENT) return strchr(f, '%') == nullptr;
    // %.Nf
    if (f[0] != '%' || f[1] != '.') return false;
    const char* p = f + 2;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    return p[0] == 'f' && p[1] == '\0';
}

void test_dfl_formats_are_safe(void) {
    for (const QxSubdriver* const* s = QX_SUBDRIVER_LIST; *s; s++) {
        for (const QxItem* it = (*s)->qx2nut; it->info_type; it++) {
            TEST_ASSERT_TRUE_MESSAGE(dflAllowed(*it), it->info_type);
        }
    }
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_megatec_testing_values);
    RUN_TEST(test_megatec_claim_sends_status_then_vendor);
    RUN_TEST(test_megatec_battery_charge_recomputed_by_full_update);
    RUN_TEST(test_q1_testing_values);
    RUN_TEST(test_table_row_counts);
    RUN_TEST(test_mustek_testing_values);
    RUN_TEST(test_megatec_old_testing_values);
    RUN_TEST(test_zinto_testing_values);
    RUN_TEST(test_innovatae_testing_values);
    RUN_TEST(test_new_protocols_claim_commands);
    RUN_TEST(test_innovatae_nak_rejected);
    RUN_TEST(test_subdriver_list_order);
    RUN_TEST(test_protocol_name_match);
    RUN_TEST(test_status_on_battery);
    RUN_TEST(test_status_on_battery_low);
    RUN_TEST(test_status_boost);
    RUN_TEST(test_status_bypass);
    RUN_TEST(test_status_trim);
    RUN_TEST(test_status_calibration);
    RUN_TEST(test_status_bypass_output_too_low_fails);
    RUN_TEST(test_status_shutdown_active_gives_fsd_and_alarm);
    RUN_TEST(test_status_ups_failed_alarm);
    RUN_TEST(test_beeper_enabled);
    RUN_TEST(test_ups_type_line_interactive_static);
    RUN_TEST(test_short_status_answer_not_claimed);
    RUN_TEST(test_wrong_leading_character_not_claimed);
    RUN_TEST(test_bad_status_bit_fails_update_without_writing);
    RUN_TEST(test_megatec_without_vendor_not_claimed);
    RUN_TEST(test_megatec_novendor);
    RUN_TEST(test_megatec_norating);
    RUN_TEST(test_ignoresab);
    RUN_TEST(test_command_strings_of_megatec_testing);
    RUN_TEST(test_command_defaults_and_ranges);
    RUN_TEST(test_setvar_truncation);
    RUN_TEST(test_delay_ranges);
    RUN_TEST(test_dfl_formats_are_safe);
    return UNITY_END();
}
#endif
#endif
