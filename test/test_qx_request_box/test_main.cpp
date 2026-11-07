#include <unity.h>
#include <string.h>
#include <functional>
#include "QxRequestBox.h"

// US-137 (review of US-136, point 1): the Q* request box of the host, extracted from
// USBHostUPS into QxRequestBox, with fake locks and semaphores. One thread: the poll task is
// simulated inside the caller's wait (FakeSync::onWait) and inside a run (FakeTarget::onRun).

void setUp(void) {}
void tearDown(void) {}

namespace {

struct FakeSync : IQxRequestSync {
    int depth = 0;       // lock nesting (the host lock is recursive)
    int maxDepth = 0;
    bool semaphores = true;
    bool pollTask = false;
    bool callerFree = true;  // takeCaller() result
    bool done = false;       // the binary semaphore
    int takes = 0, gives = 0, waits = 0, signals = 0, drains = 0;
    uint32_t lastWaitMs = 0;
    std::function<void()> onWait; // what the poll task does while the caller waits

    void lock() override {
        depth++;
        if (depth > maxDepth) maxDepth = depth;
    }
    void unlock() override { depth--; }
    bool ready() const override { return semaphores; }
    bool takeCaller(uint32_t ms) override {
        lastWaitMs = ms;
        takes++;
        return callerFree;
    }
    void giveCaller() override { gives++; }
    bool waitDone(uint32_t ms) override {
        lastWaitMs = ms;
        waits++;
        TEST_ASSERT_EQUAL(0, depth); // never wait with the data lock held
        if (onWait) onWait();
        bool given = done;
        done = false;
        return given;
    }
    void signalDone() override {
        signals++;
        done = true;
    }
    void drainDone() override {
        drains++;
        done = false;
    }
    bool inPollTask() const override { return pollTask; }
};

struct FakeTarget : IQxRequestTarget {
    bool driver = true;
    bool ready = true;
    uint32_t wait = 7000;
    CommandResult cmdResult = CommandResult::OK;
    NutTable::SetStatus setResult = NutTable::SetStatus::OK;
    int runs = 0;
    std::string lastName, lastParam;
    bool lastParamNull = false;
    const FakeSync* sync = nullptr;
    std::function<void()> onRun;

    bool hasDriver() const override { return driver; }
    bool readyToPoll() const override { return ready; }
    uint32_t waitMs() const override { return wait; }
    CommandResult runCommand(const char* name, const char* param) override {
        note(name, param);
        return cmdResult;
    }
    NutTable::SetStatus runSetVariable(const char* name, const char* value) override {
        note(name, value);
        return setResult;
    }
    void note(const char* name, const char* param) {
        runs++;
        if (sync) TEST_ASSERT_EQUAL(0, sync->depth); // no lock during the Q* command
        lastName = name;
        lastParamNull = param == nullptr;
        lastParam = param ? param : "";
        if (onRun) onRun();
    }
};

QxRequest command(const char* name, const char* param = "") {
    QxRequest r;
    r.setvar = false;
    snprintf(r.name, sizeof(r.name), "%s", name);
    snprintf(r.value, sizeof(r.value), "%s", param);
    return r;
}

QxRequest setvar(const char* name, const char* value) {
    QxRequest r = command(name, value);
    r.setvar = true;
    return r;
}

struct Rig {
    FakeSync sync;
    FakeTarget target;
    QxRequestBox box{sync};
    Rig() {
        target.sync = &sync;
        // By default the poll task serves the request during the wait
        sync.onWait = [this]() { box.service(true, target); };
    }
};

} // namespace

void test_command_answered_by_the_poll_task(void) {
    Rig r;
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)req.cmd);
    TEST_ASSERT_EQUAL(1, r.target.runs);
    TEST_ASSERT_EQUAL_STRING("beeper.toggle", r.target.lastName.c_str());
    TEST_ASSERT_TRUE(r.target.lastParamNull); // "" = no value
    TEST_ASSERT_EQUAL(1, r.sync.takes);
    TEST_ASSERT_EQUAL(1, r.sync.gives);
    TEST_ASSERT_EQUAL(1, r.sync.signals);
    TEST_ASSERT_EQUAL_UINT32(7000, r.sync.lastWaitMs);
    TEST_ASSERT_FALSE(r.box.current().pending);
    TEST_ASSERT_EQUAL(0, r.sync.depth);
}

void test_command_with_a_value(void) {
    Rig r;
    QxRequest req = command("test.battery.start", "05");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_FALSE(r.target.lastParamNull);
    TEST_ASSERT_EQUAL_STRING("05", r.target.lastParam.c_str());
}

void test_setvar_answered_by_the_poll_task(void) {
    Rig r;
    r.target.setResult = NutTable::SetStatus::CONVERSION_FAILED;
    QxRequest req = setvar("ups.delay.shutdown", "abc");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::CONVERSION_FAILED, (int)req.set);
    TEST_ASSERT_EQUAL_STRING("abc", r.target.lastParam.c_str());
}

void test_not_connected_without_a_ready_driver(void) {
    {
        Rig r;
        r.target.driver = false;
        QxRequest req = command("beeper.toggle");
        TEST_ASSERT_TRUE(r.box.post(req, r.target));
        TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)req.cmd);
        TEST_ASSERT_EQUAL((int)NutTable::SetStatus::NOT_CONNECTED, (int)req.set);
        TEST_ASSERT_EQUAL(0, r.sync.takes);
        TEST_ASSERT_EQUAL(0, r.target.runs);
    }
    {
        Rig r;
        r.target.ready = false;
        QxRequest req = command("beeper.toggle");
        TEST_ASSERT_TRUE(r.box.post(req, r.target));
        TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)req.cmd);
        TEST_ASSERT_EQUAL(0, r.sync.takes);
    }
}

void test_abandoned_when_the_wait_is_over(void) {
    Rig r;
    r.sync.onWait = nullptr; // the poll task is busy: no answer in time
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_FALSE(r.box.post(req, r.target));
    TEST_ASSERT_FALSE(r.box.current().pending);
    TEST_ASSERT_EQUAL(1, r.sync.gives);
    // The poll task comes later: nothing to serve
    TEST_ASSERT_FALSE(r.box.service(true, r.target));
    TEST_ASSERT_EQUAL(0, r.target.runs);
    TEST_ASSERT_EQUAL(0, r.sync.signals);
}

void test_late_result_of_an_abandoned_request_is_dropped(void) {
    Rig r;
    uint32_t gen = 0;
    // The caller's wait ends while the poll task runs the command
    r.target.onRun = [&]() {
        gen = r.box.current().gen;
        r.box.abandon(gen);
    };
    QxRequest req = command("beeper.toggle");
    r.box.post(req, r.target);
    TEST_ASSERT_EQUAL(1, r.target.runs);
    TEST_ASSERT_TRUE(gen != 0);
    // The result never reached the box nor the caller
    TEST_ASSERT_EQUAL(0, r.sync.signals);
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)r.box.current().cmd);
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)req.cmd);
}

void test_answer_of_another_generation_is_ignored(void) {
    Rig r;
    // First request abandoned
    r.sync.onWait = nullptr;
    QxRequest first = command("beeper.toggle");
    r.box.post(first, r.target);
    const uint32_t oldGen = r.box.current().gen;
    // Second request: an answer for the old generation leaves it pending
    r.sync.onWait = [&]() {
        TEST_ASSERT_TRUE(r.box.current().pending);
        TEST_ASSERT_TRUE(r.box.current().gen != oldGen);
        r.box.answer(CommandResult::NOT_CONNECTED, NutTable::SetStatus::NOT_CONNECTED, oldGen);
        TEST_ASSERT_TRUE(r.box.current().pending);
        TEST_ASSERT_EQUAL(0, r.sync.signals);
        r.box.service(true, r.target);
    };
    QxRequest second = command("beeper.toggle");
    TEST_ASSERT_TRUE(r.box.post(second, r.target));
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)second.cmd);
}

void test_not_connected_when_the_ups_is_detached(void) {
    Rig r;
    // releaseBoundState(): answer(NOT_CONNECTED) with gen 0 closes whatever is pending
    r.sync.onWait = [&]() { r.box.answer(CommandResult::NOT_CONNECTED, NutTable::SetStatus::NOT_CONNECTED); };
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)req.cmd);
    TEST_ASSERT_EQUAL(1, r.sync.signals);
    TEST_ASSERT_EQUAL(0, r.target.runs);
    // Nothing pending: a later answer does nothing
    r.box.answer(CommandResult::FAILED, NutTable::SetStatus::FAILED);
    TEST_ASSERT_EQUAL(1, r.sync.signals);
}

void test_failed_while_the_link_is_paused(void) {
    Rig r;
    bool ran = true;
    r.sync.onWait = [&]() { ran = r.box.service(false, r.target); };
    QxRequest req = setvar("ups.delay.start", "60");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_FALSE(ran);
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)req.cmd);
    TEST_ASSERT_EQUAL((int)NutTable::SetStatus::FAILED, (int)req.set);
    TEST_ASSERT_EQUAL(0, r.target.runs);
}

void test_driver_gone_during_the_wait(void) {
    Rig r;
    bool ran = true;
    r.sync.onWait = [&]() {
        r.target.driver = false;
        ran = r.box.service(true, r.target);
    };
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_FALSE(ran);
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)req.cmd);
    TEST_ASSERT_EQUAL(0, r.target.runs);
}

void test_request_from_the_poll_task_runs_at_once(void) {
    Rig r;
    r.sync.pollTask = true;
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_TRUE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)req.cmd);
    TEST_ASSERT_EQUAL(1, r.target.runs);
    TEST_ASSERT_EQUAL(0, r.sync.takes);
    TEST_ASSERT_EQUAL(0, r.sync.waits);
    TEST_ASSERT_EQUAL(0, r.sync.drains);
    TEST_ASSERT_FALSE(r.box.current().pending);
}

void test_busy_caller_mutex_leaves_the_box_untouched(void) {
    Rig r;
    r.sync.callerFree = false;
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_FALSE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL_UINT32(0, r.box.current().gen);
    TEST_ASSERT_FALSE(r.box.current().pending);
    TEST_ASSERT_EQUAL(0, r.sync.waits);
    TEST_ASSERT_EQUAL(0, r.sync.gives);
}

void test_no_semaphores_fails_without_waiting(void) {
    Rig r;
    r.sync.semaphores = false;
    QxRequest req = command("beeper.toggle");
    TEST_ASSERT_FALSE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL(0, r.sync.takes);
}

void test_service_without_a_request_does_nothing(void) {
    Rig r;
    TEST_ASSERT_FALSE(r.box.service(true, r.target));
    TEST_ASSERT_EQUAL(0, r.target.runs);
    TEST_ASSERT_EQUAL(0, r.sync.signals);
}

void test_stale_give_is_drained_before_and_after(void) {
    Rig r;
    r.sync.done = true; // left by an abandoned request
    r.sync.onWait = nullptr;
    QxRequest req = command("beeper.toggle");
    // Without the drain the stale give would look like an answer
    TEST_ASSERT_FALSE(r.box.post(req, r.target));
    TEST_ASSERT_EQUAL(2, r.sync.drains);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_command_answered_by_the_poll_task);
    RUN_TEST(test_command_with_a_value);
    RUN_TEST(test_setvar_answered_by_the_poll_task);
    RUN_TEST(test_not_connected_without_a_ready_driver);
    RUN_TEST(test_abandoned_when_the_wait_is_over);
    RUN_TEST(test_late_result_of_an_abandoned_request_is_dropped);
    RUN_TEST(test_answer_of_another_generation_is_ignored);
    RUN_TEST(test_not_connected_when_the_ups_is_detached);
    RUN_TEST(test_failed_while_the_link_is_paused);
    RUN_TEST(test_driver_gone_during_the_wait);
    RUN_TEST(test_request_from_the_poll_task_runs_at_once);
    RUN_TEST(test_busy_caller_mutex_leaves_the_box_untouched);
    RUN_TEST(test_no_semaphores_fails_without_waiting);
    RUN_TEST(test_service_without_a_request_does_nothing);
    RUN_TEST(test_stale_give_is_drained_before_and_after);
    return UNITY_END();
}
#endif
#endif
