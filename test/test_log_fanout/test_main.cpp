#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include "LogFanout.h"

// Log fan-out (issue #69): boot hold, per-output level and rate limit, summary lines

class FakeOutput : public ILogOutput {
public:
    explicit FakeOutput(const char* n) : _name(n) {}
    const char* name() const override { return _name; }
    bool enabled() const override { return is_enabled; }
    void poll(uint32_t) override { polls++; }
    bool ready() const override { return is_ready; }
    bool deliver(const LogRecord& r) override {
        lines.push_back(std::string(r.msg));
        return accept;
    }

    bool is_enabled = true;
    bool is_ready = true;
    bool accept = true;
    int polls = 0;
    std::vector<std::string> lines;
    void setEnabledAt(uint32_t t) { _enabled_at = t; }

private:
    const char* _name;
};

static LogRecord rec(LogLevel level, const char* msg, bool notice = false) {
    LogRecord r;
    r.uptime_ms = 0;
    r.level = level;
    r.notice = notice;
    strncpy(r.msg, msg, sizeof(r.msg) - 1);
    r.msg[sizeof(r.msg) - 1] = '\0';
    return r;
}

void setUp(void) {}
void tearDown(void) {}

void test_each_output_has_its_own_level(void) {
    LogFanout f;
    FakeOutput a("syslog"), b("mqtt");
    a.filter().configure(LogLevel::INFO, 10);
    b.filter().configure(LogLevel::ERROR, 10);
    f.add(&a);
    f.add(&b);
    f.deliver(rec(LogLevel::INFO, "info"), 1000);
    f.deliver(rec(LogLevel::ERROR, "error"), 1000);
    TEST_ASSERT_EQUAL(2, (int)a.lines.size());
    TEST_ASSERT_EQUAL(1, (int)b.lines.size());
    TEST_ASSERT_EQUAL_STRING("error", b.lines[0].c_str());
    TEST_ASSERT_EQUAL_UINT32(2, a.stats().sent);
    TEST_ASSERT_EQUAL_UINT32(1, b.stats().sent);
}

void test_disabled_output_gets_nothing_and_does_not_hold(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.is_enabled = false;
    a.is_ready = false;
    f.add(&a);
    TEST_ASSERT_FALSE(f.shouldHold(0));
    f.deliver(rec(LogLevel::ERROR, "x"), 0);
    TEST_ASSERT_EQUAL(0, (int)a.lines.size());
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().not_ready);
}

void test_boot_lines_are_held_until_the_network_is_up(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.is_ready = false;
    f.add(&a);
    TEST_ASSERT_TRUE(f.shouldHold(5000));
    a.is_ready = true;
    TEST_ASSERT_FALSE(f.shouldHold(5000));
}

void test_hold_ends_after_one_minute_then_lines_are_dropped(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.is_ready = false;
    f.add(&a);
    TEST_ASSERT_FALSE(f.shouldHold(LogFanout::HOLD_MS));
    f.deliver(rec(LogLevel::WARN, "lost"), LogFanout::HOLD_MS);
    TEST_ASSERT_EQUAL(0, (int)a.lines.size());
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().not_ready);
}

void test_rate_limit_then_one_summary_line(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.filter().configure(LogLevel::INFO, 1);  // burst 20
    f.add(&a);
    for (int i = 0; i < 25; i++) f.deliver(rec(LogLevel::ERROR, "burst"), 0);
    TEST_ASSERT_EQUAL(20, (int)a.lines.size());
    TEST_ASSERT_EQUAL_UINT32(5, a.stats().rate_limited);

    // One second later one token is back: the summary goes first, then the line
    f.deliver(rec(LogLevel::INFO, "next"), 1000);
    TEST_ASSERT_EQUAL(22, (int)a.lines.size());
    TEST_ASSERT_EQUAL_STRING("[LOG] 5 lines not sent (rate limit 1/s)", a.lines[20].c_str());
    TEST_ASSERT_EQUAL_STRING("next", a.lines[21].c_str());
}

void test_transport_failures_are_counted(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.accept = false;
    f.add(&a);
    f.deliver(rec(LogLevel::INFO, "x"), 0);
    TEST_ASSERT_EQUAL_UINT32(0, a.stats().sent);
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().failed);
}

void test_poll_reaches_every_output_and_the_limit_of_outputs(void) {
    LogFanout f;
    FakeOutput o1("1"), o2("2"), o3("3"), o4("4"), o5("5");
    TEST_ASSERT_TRUE(f.add(&o1));
    TEST_ASSERT_TRUE(f.add(&o2));
    TEST_ASSERT_TRUE(f.add(&o3));
    TEST_ASSERT_TRUE(f.add(&o4));
    TEST_ASSERT_FALSE(f.add(&o5));
    TEST_ASSERT_FALSE(f.add(nullptr));
    f.poll(0);
    TEST_ASSERT_EQUAL(1, o1.polls);
    TEST_ASSERT_EQUAL(1, o4.polls);
    TEST_ASSERT_EQUAL(0, o5.polls);
}

void test_lines_are_held_after_enabling_from_the_web_ui(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.is_ready = false;
    a.setEnabledAt(600000);  // enabled ten minutes after the boot
    f.add(&a);
    TEST_ASSERT_TRUE(f.shouldHold(600000 + 5000));
    TEST_ASSERT_FALSE(f.shouldHold(600000 + LogFanout::HOLD_MS));
}

void test_notice_skips_level_and_rate_limit(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.filter().configure(LogLevel::ERROR, 1);  // burst 20
    f.add(&a);
    for (int i = 0; i < 25; i++) f.deliver(rec(LogLevel::ERROR, "burst"), 0);
    TEST_ASSERT_EQUAL(20, (int)a.lines.size());
    // An INFO notice in the middle of the burst: below the level and over the limit, sent
    f.deliver(rec(LogLevel::INFO, "[SYSLOG] Settings changed by 'admin': minimum level INFO -> ERROR", true), 0);
    TEST_ASSERT_EQUAL(21, (int)a.lines.size());
    TEST_ASSERT_EQUAL_STRING("[SYSLOG] Settings changed by 'admin': minimum level INFO -> ERROR", a.lines[20].c_str());
    // It does not use a token, nor report the refused lines
    TEST_ASSERT_EQUAL_UINT32(5, a.filter().pendingSuppressed());
}

void test_notice_still_needs_an_enabled_and_ready_output(void) {
    LogFanout f;
    FakeOutput a("syslog");
    a.is_enabled = false;
    f.add(&a);
    f.deliver(rec(LogLevel::INFO, "n", true), 0);
    TEST_ASSERT_EQUAL(0, (int)a.lines.size());
    a.is_enabled = true;
    a.is_ready = false;
    f.deliver(rec(LogLevel::INFO, "n", true), LogFanout::HOLD_MS);
    TEST_ASSERT_EQUAL(0, (int)a.lines.size());
    TEST_ASSERT_EQUAL_UINT32(1, a.stats().not_ready);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_each_output_has_its_own_level);
    RUN_TEST(test_disabled_output_gets_nothing_and_does_not_hold);
    RUN_TEST(test_boot_lines_are_held_until_the_network_is_up);
    RUN_TEST(test_hold_ends_after_one_minute_then_lines_are_dropped);
    RUN_TEST(test_rate_limit_then_one_summary_line);
    RUN_TEST(test_transport_failures_are_counted);
    RUN_TEST(test_poll_reaches_every_output_and_the_limit_of_outputs);
    RUN_TEST(test_lines_are_held_after_enabling_from_the_web_ui);
    RUN_TEST(test_notice_skips_level_and_rate_limit);
    RUN_TEST(test_notice_still_needs_an_enabled_and_ready_output);
    return UNITY_END();
}
