#include <unity.h>
#include <string.h>
#include "core/twdt_task_list.h"

// US-110: names of the tasks that did not reset the task watchdog, kept for one boot

static const char* CAPTION = "Task watchdog got triggered. "
                             "The following tasks/users did not reset the watchdog in time:";

// Same sequence of messages as esp_task_wdt_print_triggered_tasks() (task_wdt.c)
static void feedEntry(TwdtTaskList* l, const char* name, const char* cpu) {
    twdt_list_feed(l, "\n - ");
    twdt_list_feed(l, name);
    twdt_list_feed(l, cpu);
}

static TwdtTaskList list;

void setUp(void) {
    memset(&list, 0x5A, sizeof(list)); // RTC memory is not cleared
    twdt_list_reset(&list);
}
void tearDown(void) {}

void test_single_task_with_cpu(void) {
    twdt_list_feed(&list, CAPTION);
    feedEntry(&list, "ups_poll", " (CPU 0/1)");
    TEST_ASSERT_EQUAL_STRING("ups_poll (CPU 0/1)", list.text);
    TEST_ASSERT_EQUAL_UINT8(1, list.count);
    TEST_ASSERT_EQUAL_UINT8(0, list.truncated);
}

void test_two_tasks_separated_by_comma(void) {
    twdt_list_feed(&list, CAPTION);
    feedEntry(&list, "loopTask", " (CPU 1)");
    feedEntry(&list, "IDLE0", " (CPU 0)");
    TEST_ASSERT_EQUAL_STRING("loopTask (CPU 1), IDLE0 (CPU 0)", list.text);
    TEST_ASSERT_EQUAL_UINT8(2, list.count);
}

void test_caption_only_collects_nothing(void) {
    twdt_list_feed(&list, CAPTION);
    TEST_ASSERT_EQUAL_UINT8(0, list.count);
    TEST_ASSERT_EQUAL_STRING("", list.text);
}

void test_long_names_are_cut_without_overflow(void) {
    struct {
        TwdtTaskList l;
        uint8_t canary[16];
    } guarded;
    memset(&guarded, 0xA5, sizeof(guarded));
    twdt_list_reset(&guarded.l);

    twdt_list_feed(&guarded.l, CAPTION);
    for (int i = 0; i < 10; i++) {
        feedEntry(&guarded.l, "sixteen_chars_xx", " (CPU 0/1)");
    }
    TEST_ASSERT_EQUAL_UINT8(TWDT_TASKS_MAX_LEN - 1, guarded.l.len);
    TEST_ASSERT_EQUAL_CHAR('\0', guarded.l.text[TWDT_TASKS_MAX_LEN - 1]);
    TEST_ASSERT_EQUAL_size_t(TWDT_TASKS_MAX_LEN - 1, strlen(guarded.l.text));
    TEST_ASSERT_EQUAL_UINT8(1, guarded.l.truncated);
    TEST_ASSERT_EQUAL_UINT8(10, guarded.l.count);
    for (size_t i = 0; i < sizeof(guarded.canary); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5, guarded.canary[i]);
    }
}

void test_null_message_or_list_is_ignored(void) {
    twdt_list_feed(&list, NULL);
    twdt_list_feed(NULL, "ups_poll");
    twdt_list_reset(NULL);
    TEST_ASSERT_EQUAL_UINT8(0, list.count);
    TEST_ASSERT_EQUAL_STRING("", list.text);
}

void test_consume_valid_record_only_once(void) {
    feedEntry(&list, "ups_poll", " (CPU 0/1)");
    uint32_t magic = TWDT_LIST_MAGIC;
    char out[TWDT_TASKS_MAX_LEN + 4];

    TEST_ASSERT_TRUE(twdt_list_consume(&magic, &list, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("ups_poll (CPU 0/1)", out);
    TEST_ASSERT_EQUAL_UINT32(0, magic);

    // Next boot (another TWDT reset too): the record is spent
    TEST_ASSERT_FALSE(twdt_list_consume(&magic, &list, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_consume_other_reset_shows_nothing_and_clears(void) {
    feedEntry(&list, "ups_poll", " (CPU 0/1)");
    uint32_t magic = TWDT_LIST_MAGIC;
    char out[TWDT_TASKS_MAX_LEN + 4];

    TEST_ASSERT_FALSE(twdt_list_consume(&magic, &list, false, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT32(0, magic);
}

void test_consume_wrong_magic_shows_nothing(void) {
    feedEntry(&list, "ups_poll", " (CPU 0/1)");
    uint32_t magic = 0x12345678u;
    char out[TWDT_TASKS_MAX_LEN + 4];

    TEST_ASSERT_FALSE(twdt_list_consume(&magic, &list, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT32(0, magic);
}

void test_consume_without_tasks_shows_nothing(void) {
    twdt_list_feed(&list, CAPTION);
    uint32_t magic = TWDT_LIST_MAGIC;
    char out[TWDT_TASKS_MAX_LEN + 4];

    TEST_ASSERT_FALSE(twdt_list_consume(&magic, &list, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, magic);
}

void test_consume_truncated_record_ends_with_dots(void) {
    for (int i = 0; i < 10; i++) {
        feedEntry(&list, "sixteen_chars_xx", " (CPU 0/1)");
    }
    uint32_t magic = TWDT_LIST_MAGIC;
    char out[TWDT_TASKS_MAX_LEN + 4];

    TEST_ASSERT_TRUE(twdt_list_consume(&magic, &list, true, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(TWDT_TASKS_MAX_LEN - 1 + 3, strlen(out));
    TEST_ASSERT_EQUAL_STRING("...", out + strlen(out) - 3);
}

void test_consume_small_output_buffer(void) {
    feedEntry(&list, "ups_poll", " (CPU 0/1)");
    uint32_t magic = TWDT_LIST_MAGIC;
    char out[12];
    memset(out, 0x7E, sizeof(out));

    TEST_ASSERT_TRUE(twdt_list_consume(&magic, &list, true, out, 8));
    TEST_ASSERT_EQUAL_STRING("ups_pol", out);
    TEST_ASSERT_EQUAL_HEX8(0x7E, (uint8_t)out[8]);
}

void test_consume_garbage_record_stays_in_bounds(void) {
    memset(&list, 'A', sizeof(list)); // no terminator, count and len random
    uint32_t magic = TWDT_LIST_MAGIC;
    char out[TWDT_TASKS_MAX_LEN + 4];

    TEST_ASSERT_TRUE(twdt_list_consume(&magic, &list, true, out, sizeof(out)));
    TEST_ASSERT_TRUE(strlen(out) <= TWDT_TASKS_MAX_LEN - 1 + 3);
    TEST_ASSERT_EQUAL_UINT32(0, magic);
}

void test_feed_after_garbage_len_stays_in_bounds(void) {
    memset(&list, 0xFF, sizeof(list)); // len 255: never reset (defensive)
    list.count = 0;
    list.in_entry = 0;
    feedEntry(&list, "ups_poll", " (CPU 0/1)");
    TEST_ASSERT_EQUAL_CHAR('\0', list.text[TWDT_TASKS_MAX_LEN - 1]);
    TEST_ASSERT_TRUE(list.len <= TWDT_TASKS_MAX_LEN - 1);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_single_task_with_cpu);
    RUN_TEST(test_two_tasks_separated_by_comma);
    RUN_TEST(test_caption_only_collects_nothing);
    RUN_TEST(test_long_names_are_cut_without_overflow);
    RUN_TEST(test_null_message_or_list_is_ignored);
    RUN_TEST(test_consume_valid_record_only_once);
    RUN_TEST(test_consume_other_reset_shows_nothing_and_clears);
    RUN_TEST(test_consume_wrong_magic_shows_nothing);
    RUN_TEST(test_consume_without_tasks_shows_nothing);
    RUN_TEST(test_consume_truncated_record_ends_with_dots);
    RUN_TEST(test_consume_small_output_buffer);
    RUN_TEST(test_consume_garbage_record_stays_in_bounds);
    RUN_TEST(test_feed_after_garbage_len_stays_in_bounds);
    return UNITY_END();
}
