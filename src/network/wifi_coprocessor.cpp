#include "network/wifi_coprocessor.h"

#include <Arduino.h>
#include <stdio.h>
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "core/app_logger.h"

// ESP-Hosted exists only on the chips that need a coprocessor for Wi-Fi (the P4): the one
// target #if ADR 0010 allows, a library that exists only on some chips, in a net adapter
#if CONFIG_ESP_HOSTED_ENABLED
#include <WiFi.h>
#include "esp32-hal-hosted.h"
#endif

namespace {

WifiCoprocessorStatus s_status;

#if CONFIG_ESP_HOSTED_ENABLED
SemaphoreHandle_t s_done = nullptr;
volatile bool s_ok = false;

// hostedInitWiFi() resets the coprocessor and waits for its handshake: it may block for long
// when the coprocessor does not answer, so it runs here and never in the loopTask
void probeTask(void*) {
    s_ok = hostedInitWiFi();
    xSemaphoreGive(s_done);
    vTaskDelete(nullptr);
}

uint8_t clampVersion(uint32_t v) {
    return v > 255 ? 255 : (uint8_t)v;
}
#endif

}  // namespace

namespace WifiCoprocessorProbe {

const WifiCoprocessorStatus& status() {
    return s_status;
}

bool run(const WifiCoprocessor& c, uint32_t timeout_ms, void (*tick)(uint32_t now)) {
    s_status = WifiCoprocessorStatus();
    s_status.present = c.present;
    if (!c.present) return false;
#if CONFIG_ESP_HOSTED_ENABLED
    const uint32_t start = millis();
    if (!WiFi.setPins(c.clk, c.cmd, c.d0, c.d1, c.d2, c.d3, c.reset)) {
        snprintf(s_status.error, sizeof(s_status.error), "SDIO pins refused by the Wi-Fi library");
        return false;
    }
    if (!s_done) s_done = xSemaphoreCreateBinary();
    if (!s_done || xTaskCreate(probeTask, "wifi_cp_probe", 4096, nullptr, 2, nullptr) != pdPASS) {
        snprintf(s_status.error, sizeof(s_status.error), "no memory for the start of the coprocessor");
        return false;
    }
    while (xSemaphoreTake(s_done, pdMS_TO_TICKS(100)) != pdTRUE) {
        esp_task_wdt_reset();
        if (tick) tick(millis());
        if (millis() - start >= timeout_ms) {
            s_status.timed_out = true;
            s_status.duration_ms = millis() - start;
            snprintf(s_status.error, sizeof(s_status.error), "no answer within %u s",
                     (unsigned)(timeout_ms / 1000));
            return false;
        }
    }
    s_status.duration_ms = millis() - start;
    if (!s_ok) {
        snprintf(s_status.error, sizeof(s_status.error), "the coprocessor did not answer");
        return false;
    }
    s_status.answered = true;
    uint32_t a = 0, b = 0, d = 0;
    hostedGetHostVersion(&a, &b, &d);
    s_status.host[0] = clampVersion(a);
    s_status.host[1] = clampVersion(b);
    s_status.host[2] = clampVersion(d);
    a = b = d = 0;
    hostedGetSlaveVersion(&a, &b, &d);
    s_status.slave[0] = clampVersion(a);
    s_status.slave[1] = clampVersion(b);
    s_status.slave[2] = clampVersion(d);
    const char* target = hostedGetSlaveTargetName();
    snprintf(s_status.target, sizeof(s_status.target), "%s", target ? target : "");
    s_status.update_available = hostedHasUpdate();
    return true;
#else
    (void)timeout_ms;
    (void)tick;
    snprintf(s_status.error, sizeof(s_status.error), "no ESP-Hosted support in this image");
    return false;
#endif
}

}  // namespace WifiCoprocessorProbe

namespace WifiCoprocessorUpdate {

#if CONFIG_ESP_HOSTED_ENABLED
bool begin() {
    return s_status.answered && hostedBeginUpdate();
}

bool write(const uint8_t* data, size_t len) {
    // The core takes a non-const buffer; it only reads it
    return hostedWriteUpdate(const_cast<uint8_t*>(data), (uint32_t)len);
}

bool end() {
    return hostedEndUpdate();
}

bool activate() {
    return hostedActivateUpdate();
}
#else
bool begin() { return false; }
bool write(const uint8_t*, size_t) { return false; }
bool end() { return false; }
bool activate() { return false; }
#endif

}  // namespace WifiCoprocessorUpdate
