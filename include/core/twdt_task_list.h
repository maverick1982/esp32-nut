#ifndef TWDT_TASK_LIST_H
#define TWDT_TASK_LIST_H

#include <stddef.h>
#include <stdint.h>

/**
 * Tasks that did not reset the task watchdog in time (US-110, issue #112).
 *
 * On a TWDT timeout ESP-IDF prints the guilty tasks only on the serial console, and the
 * core dump holds the backtrace of the task the interrupt hit (often IDLE1), not of the
 * blocked one. The TWDT ISR hook in CrashDiag feeds the lines of
 * esp_task_wdt_print_triggered_tasks() into this record, kept in RTC memory, and the next
 * boot logs it once.
 *
 * Feed and reset run inside the watchdog interrupt, right before the panic: no heap, no
 * locks, no library calls, fixed size (names past the limit are cut).
 */

#define TWDT_TASKS_MAX_LEN 96
#define TWDT_LIST_MAGIC 0x4E555457u // "NUTW"

struct TwdtTaskList {
    char text[TWDT_TASKS_MAX_LEN]; // "ups_poll (CPU 0/1), IDLE0 (CPU 0)"
    uint8_t len;
    uint8_t count;     // tasks collected
    uint8_t in_entry;  // a "\n - " line opened an entry: the next messages belong to it
    uint8_t truncated; // some text did not fit
};

static inline void twdt_list_reset(TwdtTaskList* l) {
    if (!l) return;
    l->text[0] = '\0';
    l->len = 0;
    l->count = 0;
    l->in_entry = 0;
    l->truncated = 0;
}

static inline void twdt_list_append(TwdtTaskList* l, const char* s) {
    if (l->len >= TWDT_TASKS_MAX_LEN) l->len = TWDT_TASKS_MAX_LEN - 1;
    while (*s) {
        if (l->len >= TWDT_TASKS_MAX_LEN - 1) {
            l->truncated = 1;
            break;
        }
        l->text[l->len++] = *s++;
    }
    l->text[l->len] = '\0';
}

// Message handler of esp_task_wdt_print_triggered_tasks(): it sends the caption, then
// for every task "\n - ", the name and " (CPU 0)" / " (CPU 1)" / " (CPU 0/1)"
static inline void twdt_list_feed(TwdtTaskList* l, const char* msg) {
    if (!l || !msg) return;
    if (msg[0] == '\n' && msg[1] == ' ' && msg[2] == '-' && msg[3] == ' ' && msg[4] == '\0') {
        if (l->count > 0) twdt_list_append(l, ", ");
        if (l->count < 255) l->count++;
        l->in_entry = 1;
        return;
    }
    if (l->in_entry) twdt_list_append(l, msg); // before the first entry: the caption
}

// Valid only for the boot right after a TWDT reset, and only once: the magic is always
// cleared. Copies the names into out (with "..." when they were cut) and returns true,
// or leaves out empty and returns false. The record may be RTC garbage after a power
// cycle: the copy stays inside both buffers.
static inline bool twdt_list_consume(uint32_t* magic, const TwdtTaskList* rec, bool reset_is_task_wdt,
                                     char* out, size_t out_size) {
    bool ok = magic && rec && *magic == TWDT_LIST_MAGIC && reset_is_task_wdt && rec->count > 0;
    if (magic) *magic = 0;
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!ok) return false;

    size_t n = 0;
    while (n < TWDT_TASKS_MAX_LEN - 1 && n < out_size - 1 && rec->text[n] != '\0') {
        out[n] = rec->text[n];
        n++;
    }
    if (rec->truncated) {
        const char* dots = "...";
        while (*dots && n < out_size - 1) out[n++] = *dots++;
    }
    out[n] = '\0';
    return n > 0;
}

#endif // TWDT_TASK_LIST_H
