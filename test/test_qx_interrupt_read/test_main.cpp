#include <unity.h>
#include <string.h>
#include <deque>
#include "EventBacklog.h"
#include "FirstData.h"

// US-136: interrupt IN chunks of a Q* command taken from the poll task's event queue, with
// the other events kept in order for after the command (ADR 0013, section 3)

void setUp(void) {}
void tearDown(void) {}

struct FakeEvent {
    enum Type : uint8_t { CONNECTED, INPUT_REPORT, DISCONNECTED, TRANSFER_ERROR };
    Type type;
    int handle;
    uint8_t data[8];
    uint16_t length;
};

struct FakeTraits {
    using Event = FakeEvent;
    using Handle = int;
    static bool isInput(const Event& e) { return e.type == FakeEvent::INPUT_REPORT; }
    static bool isDisconnect(const Event& e) { return e.type == FakeEvent::DISCONNECTED; }
    static bool isTransferError(const Event& e) { return e.type == FakeEvent::TRANSFER_ERROR; }
    static Handle handle(const Event& e) { return e.handle; }
    static const uint8_t* data(const Event& e) { return e.data; }
    static size_t length(const Event& e) { return e.length; }
};

// The event queue: an empty receive waits the time asked on the fake clock
struct FakeQueue {
    std::deque<FakeEvent> q;
    uint32_t clock = 1000;
    bool receive(FakeEvent& ev, uint32_t waitMs) {
        if (q.empty()) {
            clock += waitMs;
            return false;
        }
        ev = q.front();
        q.pop_front();
        return true;
    }
    uint32_t now() { return clock; }
};

static FakeEvent ev(FakeEvent::Type t, int handle, const char* text = "") {
    FakeEvent e = {};
    e.type = t;
    e.handle = handle;
    e.length = (uint16_t)strlen(text);
    memcpy(e.data, text, e.length);
    return e;
}

static const int BOUND = 7;

void test_chunk_of_the_bound_interface_is_returned(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, BOUND, "(230.0"));
    uint8_t buf[8] = {0};
    size_t got = 0;
    uint32_t dropped = 0;
    ChunkRead r = readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped);
    TEST_ASSERT_TRUE(r == ChunkRead::Chunk);
    TEST_ASSERT_EQUAL_UINT(6, got);
    TEST_ASSERT_EQUAL_MEMORY("(230.0", buf, 6);
    TEST_ASSERT_EQUAL_UINT(0, backlog.size());
}

void test_chunk_is_truncated_to_the_buffer(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, BOUND, "ABCDEFGH"));
    uint8_t buf[3] = {0};
    size_t got = 0;
    uint32_t dropped = 0;
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped) ==
                     ChunkRead::Chunk);
    TEST_ASSERT_EQUAL_UINT(3, got);
    TEST_ASSERT_EQUAL_MEMORY("ABC", buf, 3);
}

void test_other_events_are_kept_and_foreign_inputs_dropped(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    q.q.push_back(ev(FakeEvent::CONNECTED, 9));
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, 9, "x"));
    q.q.push_back(ev(FakeEvent::TRANSFER_ERROR, 9));
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, BOUND, "ok"));
    uint8_t buf[8];
    size_t got = 0;
    uint32_t dropped = 0;
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped) ==
                     ChunkRead::Chunk);
    TEST_ASSERT_EQUAL_UINT32(1, dropped);
    TEST_ASSERT_EQUAL_UINT(2, backlog.size());
    FakeEvent out;
    TEST_ASSERT_TRUE(backlog.pop(out));
    TEST_ASSERT_EQUAL(FakeEvent::CONNECTED, out.type);
    TEST_ASSERT_TRUE(backlog.pop(out));
    TEST_ASSERT_EQUAL(FakeEvent::TRANSFER_ERROR, out.type);
    TEST_ASSERT_FALSE(backlog.pop(out));
}

void test_disconnection_ends_the_wait_and_stays_in_order(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    q.q.push_back(ev(FakeEvent::CONNECTED, 9));
    q.q.push_back(ev(FakeEvent::DISCONNECTED, BOUND));
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, BOUND, "late"));
    uint8_t buf[8];
    size_t got = 0;
    uint32_t dropped = 0;
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped) ==
                     ChunkRead::Gone);
    TEST_ASSERT_EQUAL_UINT(0, got);
    FakeEvent out;
    TEST_ASSERT_TRUE(backlog.pop(out));
    TEST_ASSERT_EQUAL(FakeEvent::CONNECTED, out.type);
    TEST_ASSERT_TRUE(backlog.pop(out));
    TEST_ASSERT_EQUAL(FakeEvent::DISCONNECTED, out.type);
    // What came after the disconnection is still in the queue
    TEST_ASSERT_EQUAL_UINT(1, q.q.size());
}

void test_transfer_error_of_the_bound_interface_ends_the_wait(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    q.q.push_back(ev(FakeEvent::TRANSFER_ERROR, BOUND));
    uint8_t buf[8];
    size_t got = 0;
    uint32_t dropped = 0;
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped) ==
                     ChunkRead::Error);
    TEST_ASSERT_EQUAL_UINT(1, backlog.size());
}

void test_empty_queue_times_out_after_the_time_asked(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    uint8_t buf[8];
    size_t got = 0;
    uint32_t dropped = 0;
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped) ==
                     ChunkRead::Timeout);
    TEST_ASSERT_EQUAL_UINT32(2000, q.clock);
    // Zero wait: one look at the queue
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 0, dropped) ==
                     ChunkRead::Timeout);
    TEST_ASSERT_EQUAL_UINT32(2000, q.clock);
}

void test_full_backlog_stops_without_taking_more(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 2> backlog;
    q.q.push_back(ev(FakeEvent::CONNECTED, 1));
    q.q.push_back(ev(FakeEvent::CONNECTED, 2));
    q.q.push_back(ev(FakeEvent::CONNECTED, 3));
    uint8_t buf[8];
    size_t got = 0;
    uint32_t dropped = 0;
    TEST_ASSERT_TRUE(readInterruptChunk<FakeTraits>(q, backlog, BOUND, buf, sizeof(buf), got, 1000, dropped) ==
                     ChunkRead::Error);
    TEST_ASSERT_EQUAL_UINT(2, backlog.size());
    TEST_ASSERT_EQUAL_UINT(1, q.q.size());
    TEST_ASSERT_EQUAL(3, q.q.front().handle);
}

void test_discard_drops_stale_chunks_and_keeps_the_rest(void) {
    FakeQueue q;
    EventBacklog<FakeEvent, 4> backlog;
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, BOUND, "old"));
    q.q.push_back(ev(FakeEvent::CONNECTED, 9));
    q.q.push_back(ev(FakeEvent::INPUT_REPORT, 9, "x"));
    q.q.push_back(ev(FakeEvent::DISCONNECTED, 9));
    uint32_t dropped = 0;
    discardInputChunks<FakeTraits>(q, backlog, BOUND, dropped);
    TEST_ASSERT_TRUE(q.q.empty());
    TEST_ASSERT_EQUAL_UINT32(1, dropped);
    TEST_ASSERT_EQUAL_UINT(2, backlog.size());
    FakeEvent out;
    TEST_ASSERT_TRUE(backlog.pop(out));
    TEST_ASSERT_EQUAL(FakeEvent::CONNECTED, out.type);
    TEST_ASSERT_TRUE(backlog.pop(out));
    TEST_ASSERT_EQUAL(FakeEvent::DISCONNECTED, out.type);
}

void test_backlog_is_a_fifo_across_wraps(void) {
    EventBacklog<FakeEvent, 3> b;
    FakeEvent out;
    for (int round = 0; round < 3; round++) {
        TEST_ASSERT_TRUE(b.push(ev(FakeEvent::CONNECTED, 1)));
        TEST_ASSERT_TRUE(b.push(ev(FakeEvent::CONNECTED, 2)));
        TEST_ASSERT_TRUE(b.pop(out));
        TEST_ASSERT_EQUAL(1, out.handle);
        TEST_ASSERT_TRUE(b.pop(out));
        TEST_ASSERT_EQUAL(2, out.handle);
    }
    TEST_ASSERT_TRUE(b.push(ev(FakeEvent::CONNECTED, 1)));
    TEST_ASSERT_TRUE(b.push(ev(FakeEvent::CONNECTED, 2)));
    TEST_ASSERT_TRUE(b.push(ev(FakeEvent::CONNECTED, 3)));
    TEST_ASSERT_FALSE(b.push(ev(FakeEvent::CONNECTED, 4)));
    b.clear();
    TEST_ASSERT_EQUAL_UINT(0, b.size());
}

// --- firstDataReady() -------------------------------------------------------------------

void test_first_data_hid_ready_on_status_or_after_the_limit(void) {
    bool timedOut = true;
    TEST_ASSERT_TRUE(firstDataReady(true, true, true, 100, 30000, timedOut));
    TEST_ASSERT_FALSE(timedOut);
    TEST_ASSERT_FALSE(firstDataReady(true, false, true, 29999, 30000, timedOut));
    TEST_ASSERT_TRUE(firstDataReady(false, false, true, 30000, 30000, timedOut));
    TEST_ASSERT_TRUE(timedOut);
}

void test_first_data_qx_waits_without_limit(void) {
    bool timedOut = true;
    TEST_ASSERT_FALSE(firstDataReady(false, false, false, 3600000, 30000, timedOut));
    TEST_ASSERT_FALSE(timedOut);
    TEST_ASSERT_FALSE(firstDataReady(true, false, false, 3600000, 30000, timedOut));
    TEST_ASSERT_TRUE(firstDataReady(true, true, false, 10, 30000, timedOut));
    TEST_ASSERT_FALSE(timedOut);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_chunk_of_the_bound_interface_is_returned);
    RUN_TEST(test_chunk_is_truncated_to_the_buffer);
    RUN_TEST(test_other_events_are_kept_and_foreign_inputs_dropped);
    RUN_TEST(test_disconnection_ends_the_wait_and_stays_in_order);
    RUN_TEST(test_transfer_error_of_the_bound_interface_ends_the_wait);
    RUN_TEST(test_empty_queue_times_out_after_the_time_asked);
    RUN_TEST(test_full_backlog_stops_without_taking_more);
    RUN_TEST(test_discard_drops_stale_chunks_and_keeps_the_rest);
    RUN_TEST(test_backlog_is_a_fifo_across_wraps);
    RUN_TEST(test_first_data_hid_ready_on_status_or_after_the_limit);
    RUN_TEST(test_first_data_qx_waits_without_limit);
    return UNITY_END();
}
#endif
#endif
