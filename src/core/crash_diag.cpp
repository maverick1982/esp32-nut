#include "core/crash_diag.h"
#include "core/app_logger.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"

// Set once at boot, read by the panic handler: must live in internal RAM
static DRAM_ATTR volatile bool s_coredump_partition = false;

// Survives esp_restart() (not a power cycle): cause of the last controlled restart
#define RESTART_MAGIC 0x4E555452u // "NUTR"
static RTC_NOINIT_ATTR uint32_t s_restart_magic;
static RTC_NOINIT_ATTR char s_restart_reason[64];
// Controlled restarts in a row (review A7): kept across any reset but a power cycle
#define RESTART_COUNT_MAGIC 0x4E555443u // "NUTC"
static RTC_NOINIT_ATTR uint32_t s_restart_count_magic;
static RTC_NOINIT_ATTR uint8_t s_restart_count;
static bool s_degraded = false;

static esp_reset_reason_t s_reset_reason = ESP_RST_UNKNOWN;
static String s_last_restart_cause;
static bool s_has_crash = false;
static String s_crash_task;
static uint32_t s_crash_pc = 0;
static String s_crash_backtrace;

// Linked with -Wl,--wrap=esp_core_dump_write (platformio.ini): the panic handler calls
// this instead of the core dump writer. Without a coredump partition the original
// writer faults inside the panic handler, which has already disabled the watchdogs,
// and the board hangs until someone power cycles it.
extern "C" void __real_esp_core_dump_write(void *info);
extern "C" void IRAM_ATTR __wrap_esp_core_dump_write(void *info) {
    if (s_coredump_partition) {
        __real_esp_core_dump_write(info);
    }
}

static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INTERRUPT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

static bool isCrashReset(esp_reset_reason_t r) {
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

static void readCoreDumpSummary() {
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    // A dump left by an older crash stays in flash: only report it after a crash reset
    if (!s_coredump_partition || !isCrashReset(s_reset_reason)) return;
    if (esp_core_dump_image_check() != ESP_OK) return;

    esp_core_dump_summary_t *summary = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
    if (!summary) return;
    if (esp_core_dump_get_summary(summary) == ESP_OK) {
        s_has_crash = true;
        s_crash_task = String(summary->exc_task);
        s_crash_pc = summary->exc_pc;
        s_crash_backtrace = "";
        for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
            char addr[12];
            snprintf(addr, sizeof(addr), "0x%08lx", (unsigned long)summary->exc_bt_info.bt[i]);
            if (i > 0) s_crash_backtrace += " ";
            s_crash_backtrace += addr;
        }
        if (summary->exc_bt_info.corrupted) s_crash_backtrace += " |<-CORRUPTED";
    }
    free(summary);
#endif
}

namespace CrashDiag {

void begin() {
    s_coredump_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                    ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                    NULL) != NULL;
    s_reset_reason = esp_reset_reason();

    if (s_restart_magic == RESTART_MAGIC && s_reset_reason == ESP_RST_SW) {
        s_restart_reason[sizeof(s_restart_reason) - 1] = '\0';
        s_last_restart_cause = String(s_restart_reason);
    }
    s_restart_magic = 0;

    if (s_restart_count_magic != RESTART_COUNT_MAGIC || s_reset_reason == ESP_RST_POWERON ||
        s_reset_reason == ESP_RST_BROWNOUT) {
        s_restart_count_magic = RESTART_COUNT_MAGIC;
        s_restart_count = 0;
    }

    readCoreDumpSummary();
}

void logBootInfo() {
    AppLogger::log("INFO", "[DIAG] Reset reason: %s", resetReasonName(s_reset_reason));
    if (s_last_restart_cause.length() > 0) {
        AppLogger::log("WARN", "[DIAG] Last controlled restart: %s (%u in a row)",
                       s_last_restart_cause.c_str(), (unsigned)s_restart_count);
    }
    if (s_has_crash) {
        AppLogger::log("ERROR", "[DIAG] Last crash: task '%s', PC 0x%08lx", s_crash_task.c_str(), (unsigned long)s_crash_pc);
        AppLogger::log("ERROR", "[DIAG] Backtrace: %s", s_crash_backtrace.c_str());
    }
    if (!s_coredump_partition) {
        AppLogger::log("WARN", "[DIAG] No coredump partition: crash details are not saved. "
                               "Flash the full image once with the web installer to add it (OTA cannot).");
    }
}

void recordControlledRestart(const char* reason) {
    strncpy(s_restart_reason, reason ? reason : "", sizeof(s_restart_reason) - 1);
    s_restart_reason[sizeof(s_restart_reason) - 1] = '\0';
    s_restart_magic = RESTART_MAGIC;
    if (s_restart_count < 255) s_restart_count++;
}

uint8_t consecutiveRestarts() {
    return s_restart_count;
}

void clearConsecutiveRestarts() {
    s_restart_count = 0;
}

void setDegraded(bool degraded) {
    s_degraded = degraded;
}

bool hasCoredumpPartition() {
    return s_coredump_partition;
}

void fillJson(JsonObject obj) {
    obj["reset_reason"] = resetReasonName(s_reset_reason);
    obj["coredump_partition"] = (bool)s_coredump_partition;
    if (s_last_restart_cause.length() > 0) {
        obj["last_restart_cause"] = s_last_restart_cause;
    }
    obj["consecutive_restarts"] = s_restart_count;
    // Fragmentation shows up in multi-day soak tests (review S6)
    obj["heap_free"] = (uint32_t)esp_get_free_heap_size();
    obj["heap_min_free"] = (uint32_t)esp_get_minimum_free_heap_size();
    obj["heap_largest_block"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (s_degraded) {
        obj["degraded"] = true;
    }
    if (s_has_crash) {
        JsonObject crash = obj["last_crash"].to<JsonObject>();
        crash["task"] = s_crash_task;
        char pc[12];
        snprintf(pc, sizeof(pc), "0x%08lx", (unsigned long)s_crash_pc);
        crash["pc"] = pc;
        crash["backtrace"] = s_crash_backtrace;
    }
}

} // namespace CrashDiag
