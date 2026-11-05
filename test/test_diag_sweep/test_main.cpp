#include <unity.h>
#include <string.h>
#include "DiagSweep.h"

// US-119 (issue #123): on-demand read of the FEATURE reports for the USB diagnostics export

static HIDUsageDef usage(uint8_t type, uint8_t id, const char* path = "UPS.X") {
    HIDUsageDef u;
    u.report_type = type;
    u.report_id = id;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    return u;
}

static const std::set<uint16_t> kNoCache;

static bool acceptAll(uint8_t, uint8_t) { return true; }

// The rules of CyberPowerDriver::acceptPollReport (6 and >= 130) and of EatonDriver (254/255)
static bool acceptCyberPower(uint8_t, uint8_t id) { return !(id >= 130 || id == 6); }
static bool acceptEaton(uint8_t, uint8_t id) { return id != 254 && id != 255; }

// Requests every report, answering with fn(id)
template <typename Fn>
static void runAll(DiagSweep& s, Fn fn) {
    DiagSweep::Item it;
    while (s.next(it)) fn(s, it.id);
}

static void answerRead(DiagSweep& s, uint8_t id) {
    uint8_t data[3] = {id, 0x11, 0x22};
    s.record(id, DiagSweep::Result::Read, data, sizeof(data));
}

static std::vector<uint8_t> requestedIds(DiagSweep& s) {
    std::vector<uint8_t> ids;
    runAll(s, [&](DiagSweep& sw, uint8_t id) {
        ids.push_back(id);
        answerRead(sw, id);
    });
    return ids;
}

void setUp(void) {}
void tearDown(void) {}

void test_only_feature_reports_in_order_without_duplicates(void) {
    std::vector<HIDUsageDef> u = {
        usage(1, 0x01), usage(3, 0x20), usage(3, 0x05), usage(2, 0x30),
        usage(3, 0x20), usage(1, 0x05), usage(3, 0x07), usage(3, 0x05),
    };
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    TEST_ASSERT_TRUE(DiagSweep::State::Reading == s.state());
    TEST_ASSERT_EQUAL_UINT16(3, s.total());
    std::vector<uint8_t> ids = requestedIds(s);
    TEST_ASSERT_EQUAL(3, (int)ids.size());
    TEST_ASSERT_EQUAL_UINT8(0x20, ids[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, ids[1]);
    TEST_ASSERT_EQUAL_UINT8(0x07, ids[2]);
    TEST_ASSERT_TRUE(DiagSweep::State::Done == s.state());
}

void test_report_id_zero_is_read(void) {
    // A device without report IDs has only report 0
    std::vector<HIDUsageDef> u = {usage(1, 0), usage(3, 0), usage(3, 0)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    TEST_ASSERT_EQUAL_UINT16(1, s.total());
    DiagSweep::Item it;
    TEST_ASSERT_TRUE(s.next(it));
    TEST_ASSERT_EQUAL_UINT8(3, it.type);
    TEST_ASSERT_EQUAL_UINT8(0, it.id);
}

void test_cached_reports_are_not_read(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01), usage(3, 0x02), usage(3, 0x03)};
    // 0x02 cached as FEATURE, 0x03 cached only as INPUT: still read as FEATURE
    std::set<uint16_t> cached = {DiagSweep::key(3, 0x02), DiagSweep::key(1, 0x03)};
    DiagSweep s;
    s.start(u, cached, acceptAll);
    std::vector<uint8_t> ids = requestedIds(s);
    TEST_ASSERT_EQUAL(2, (int)ids.size());
    TEST_ASSERT_EQUAL_UINT8(0x01, ids[0]);
    TEST_ASSERT_EQUAL_UINT8(0x03, ids[1]);
}

void test_accept_excludes_cyberpower_reports(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x04), usage(3, 6), usage(3, 0x07), usage(3, 130), usage(3, 227)};
    DiagSweep s;
    s.start(u, kNoCache, acceptCyberPower);
    std::vector<uint8_t> ids = requestedIds(s);
    TEST_ASSERT_EQUAL(2, (int)ids.size());
    TEST_ASSERT_EQUAL_UINT8(0x04, ids[0]);
    TEST_ASSERT_EQUAL_UINT8(0x07, ids[1]);
}

void test_accept_excludes_eaton_reports(void) {
    std::vector<HIDUsageDef> u = {usage(3, 254), usage(3, 0x01), usage(3, 255), usage(3, 0x14)};
    DiagSweep s;
    s.start(u, kNoCache, acceptEaton);
    std::vector<uint8_t> ids = requestedIds(s);
    TEST_ASSERT_EQUAL(2, (int)ids.size());
    TEST_ASSERT_EQUAL_UINT8(0x01, ids[0]);
    TEST_ASSERT_EQUAL_UINT8(0x14, ids[1]);
}

void test_results_and_counters(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01), usage(3, 227), usage(3, 0x09)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);

    uint8_t data[2] = {0x01, 0x64};
    s.record(0x01, DiagSweep::Result::Read, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT16(1, s.done());
    s.record(227, DiagSweep::Result::Empty, nullptr, 0);
    s.record(0x09, DiagSweep::Result::Failed, nullptr, 0);

    TEST_ASSERT_TRUE(DiagSweep::State::Done == s.state());
    TEST_ASSERT_EQUAL_UINT16(3, s.total());
    TEST_ASSERT_EQUAL_UINT16(3, s.done());
    TEST_ASSERT_EQUAL_UINT16(1, s.read());
    TEST_ASSERT_EQUAL_UINT16(1, s.empty());
    TEST_ASSERT_EQUAL_UINT16(1, s.failed());

    // A failed report has no entry: it is only counted
    const auto& r = s.results();
    TEST_ASSERT_EQUAL(2, (int)r.size());
    TEST_ASSERT_EQUAL_UINT8(0x01, r[0].id);
    TEST_ASSERT_EQUAL_UINT8(3, r[0].type);
    TEST_ASSERT_FALSE(r[0].empty);
    TEST_ASSERT_EQUAL(2, (int)r[0].data.size());
    TEST_ASSERT_EQUAL_UINT8(0x64, r[0].data[1]);
    TEST_ASSERT_EQUAL_UINT8(227, r[1].id);
    TEST_ASSERT_TRUE(r[1].empty);
    TEST_ASSERT_EQUAL(0, (int)r[1].data.size());

    DiagSweep::Item it;
    TEST_ASSERT_FALSE(s.next(it));
}

void test_read_with_zero_bytes_counts_as_empty(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    s.record(0x01, DiagSweep::Result::Read, nullptr, 0);
    TEST_ASSERT_EQUAL_UINT16(0, s.read());
    TEST_ASSERT_EQUAL_UINT16(1, s.empty());
    TEST_ASSERT_TRUE(s.results()[0].empty);
}

void test_record_of_another_report_is_ignored(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01), usage(3, 0x02)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    uint8_t data[1] = {7};
    s.record(0x02, DiagSweep::Result::Read, data, 1);
    TEST_ASSERT_EQUAL_UINT16(0, s.done());
    TEST_ASSERT_EQUAL(0, (int)s.results().size());
}

void test_interrupt_keeps_results_read_so_far(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01), usage(3, 0x02), usage(3, 0x03), usage(3, 0x04)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    answerRead(s, 0x01);
    s.record(0x02, DiagSweep::Result::Empty, nullptr, 0);
    s.interrupt();

    TEST_ASSERT_TRUE(DiagSweep::State::Interrupted == s.state());
    TEST_ASSERT_EQUAL_UINT16(4, s.total());
    TEST_ASSERT_EQUAL_UINT16(2, s.done());
    TEST_ASSERT_EQUAL_UINT16(1, s.read());
    TEST_ASSERT_EQUAL_UINT16(1, s.empty());
    TEST_ASSERT_EQUAL(2, (int)s.results().size());

    // No more requests, and late results change nothing
    DiagSweep::Item it;
    TEST_ASSERT_FALSE(s.next(it));
    answerRead(s, 0x03);
    TEST_ASSERT_EQUAL_UINT16(2, s.done());

    // interrupt() after the end does not turn Done into Interrupted
    DiagSweep d;
    d.start({usage(3, 0x01)}, kNoCache, acceptAll);
    answerRead(d, 0x01);
    d.interrupt();
    TEST_ASSERT_TRUE(DiagSweep::State::Done == d.state());
}

void test_start_again_forgets_previous_results(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01), usage(3, 0x02)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    answerRead(s, 0x01);
    s.record(0x02, DiagSweep::Result::Failed, nullptr, 0);
    s.interrupt();

    s.start(u, {DiagSweep::key(3, 0x01)}, acceptAll);
    TEST_ASSERT_TRUE(DiagSweep::State::Reading == s.state());
    TEST_ASSERT_EQUAL_UINT16(1, s.total());
    TEST_ASSERT_EQUAL_UINT16(0, s.done());
    TEST_ASSERT_EQUAL_UINT16(0, s.read());
    TEST_ASSERT_EQUAL_UINT16(0, s.failed());
    TEST_ASSERT_EQUAL(0, (int)s.results().size());
    DiagSweep::Item it;
    TEST_ASSERT_TRUE(s.next(it));
    TEST_ASSERT_EQUAL_UINT8(0x02, it.id);
}

void test_clear_goes_back_to_idle(void) {
    std::vector<HIDUsageDef> u = {usage(3, 0x01), usage(3, 0x02)};
    DiagSweep s;
    TEST_ASSERT_TRUE(DiagSweep::State::Idle == s.state());
    s.start(u, kNoCache, acceptAll);
    answerRead(s, 0x01);
    s.clear();
    TEST_ASSERT_TRUE(DiagSweep::State::Idle == s.state());
    TEST_ASSERT_EQUAL_UINT16(0, s.total());
    TEST_ASSERT_EQUAL_UINT16(0, s.done());
    TEST_ASSERT_EQUAL_UINT16(0, s.read());
    TEST_ASSERT_EQUAL(0, (int)s.results().size());
    DiagSweep::Item it;
    TEST_ASSERT_FALSE(s.next(it));
}

void test_no_feature_report_is_done_at_once(void) {
    std::vector<HIDUsageDef> u = {usage(1, 0x01), usage(2, 0x02)};
    DiagSweep s;
    s.start(u, kNoCache, acceptAll);
    TEST_ASSERT_TRUE(DiagSweep::State::Done == s.state());
    TEST_ASSERT_EQUAL_UINT16(0, s.total());
    DiagSweep::Item it;
    TEST_ASSERT_FALSE(s.next(it));

    // Every FEATURE report already cached: nothing to read either
    std::vector<HIDUsageDef> f = {usage(3, 0x01)};
    s.start(f, {DiagSweep::key(3, 0x01)}, acceptAll);
    TEST_ASSERT_TRUE(DiagSweep::State::Done == s.state());
    TEST_ASSERT_EQUAL_UINT16(0, s.total());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_only_feature_reports_in_order_without_duplicates);
    RUN_TEST(test_report_id_zero_is_read);
    RUN_TEST(test_cached_reports_are_not_read);
    RUN_TEST(test_accept_excludes_cyberpower_reports);
    RUN_TEST(test_accept_excludes_eaton_reports);
    RUN_TEST(test_results_and_counters);
    RUN_TEST(test_read_with_zero_bytes_counts_as_empty);
    RUN_TEST(test_record_of_another_report_is_ignored);
    RUN_TEST(test_interrupt_keeps_results_read_so_far);
    RUN_TEST(test_start_again_forgets_previous_results);
    RUN_TEST(test_clear_goes_back_to_idle);
    RUN_TEST(test_no_feature_report_is_done_at_once);
    return UNITY_END();
}
