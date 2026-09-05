/**
 * test_system_state.cpp
 *
 * Test di integrazione per la logica di determinazione dello stato di sistema.
 * Verifica che computeSystemState() restituisca il LedState corretto
 * per ogni combinazione di stati Wi-Fi e UPS.
 *
 * Eseguiti su hardware ESP32-S3 — usano le funzioni reali di Arduino.
 */

// Definito PRIMA degli include per escludere setup/loop reali da main.cpp
#define UNIT_TEST

#include <Arduino.h>
#include <unity.h>
#include "core/main.h"

void setUp(void) {
    // Nessuna inizializzazione necessaria — funzione pura senza stato
}

void tearDown(void) {
    // Nessuna pulizia necessaria
}

// ---------------------------------------------------------------------------
// 1. Wi-Fi disconnesso + UPS disconnesso → CONNECTING
//    Quando il Wi-Fi non è connesso, il sistema è in fase di connessione
//    indipendentemente dallo stato dell'UPS.
// ---------------------------------------------------------------------------
void test_wifi_disconnesso_ups_disconnesso_ritorna_connecting(void) {
    TEST_ASSERT_EQUAL(LedState::CONNECTING, computeSystemState(false, false));
}

// ---------------------------------------------------------------------------
// 2. Wi-Fi disconnesso + UPS connesso → CONNECTING
//    Il Wi-Fi mancante ha priorità: il sistema resta in CONNECTING
//    anche se l'UPS è raggiungibile.
// ---------------------------------------------------------------------------
void test_wifi_disconnesso_ups_connesso_ritorna_connecting(void) {
    TEST_ASSERT_EQUAL(LedState::CONNECTING, computeSystemState(false, true));
}

// ---------------------------------------------------------------------------
// 3. Wi-Fi connesso + UPS disconnesso → ERROR
//    Il Wi-Fi è attivo ma l'UPS non è raggiungibile — condizione di errore.
// ---------------------------------------------------------------------------
void test_wifi_connesso_ups_disconnesso_ritorna_error(void) {
    TEST_ASSERT_EQUAL(LedState::ERROR, computeSystemState(true, false));
}

// ---------------------------------------------------------------------------
// 4. Wi-Fi connesso + UPS connesso → OPERATIONAL
//    Entrambi i sottosistemi sono operativi — funzionamento normale.
// ---------------------------------------------------------------------------
void test_wifi_connesso_ups_connesso_ritorna_operational(void) {
    TEST_ASSERT_EQUAL(LedState::OPERATIONAL, computeSystemState(true, true));
}

// ---------------------------------------------------------------------------
// 5. Modalità Access Point (AP Mode) attiva → AP_MODE
//    Indipendentemente dagli altri stati, se AP mode è attivo, ritorna AP_MODE.
// ---------------------------------------------------------------------------
extern AppNetworkManager network_mgr;
void test_ap_mode_attiva_ritorna_ap_mode(void) {
    network_mgr.beginAP("test_ap", "12345678");
    TEST_ASSERT_EQUAL(LedState::AP_MODE, computeSystemState(true, true));
    TEST_ASSERT_EQUAL(LedState::AP_MODE, computeSystemState(false, false));
}

void setup() {
    delay(2000);  // Stabilizzazione porta seriale

    UNITY_BEGIN();
    RUN_TEST(test_wifi_disconnesso_ups_disconnesso_ritorna_connecting);
    RUN_TEST(test_wifi_disconnesso_ups_connesso_ritorna_connecting);
    RUN_TEST(test_wifi_connesso_ups_disconnesso_ritorna_error);
    RUN_TEST(test_wifi_connesso_ups_connesso_ritorna_operational);
    RUN_TEST(test_ap_mode_attiva_ritorna_ap_mode);
    UNITY_END();
}

void loop() {
    // Loop vuoto — i test vengono eseguiti una sola volta in setup()
}

