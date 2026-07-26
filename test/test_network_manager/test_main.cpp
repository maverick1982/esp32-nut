#include <Arduino.h>
#include <unity.h>
#include "network/network_manager.h"

AppNetworkManager net_manager;

void setUp(void) {
    // Inizializzazione prima di ciascun test
}

void tearDown(void) {
    // Pulizia dopo ciascun test
}

void test_initial_state(void) {
    // Verifica che isConnected() restituisca false prima di avviare la connessione
    TEST_ASSERT_FALSE(net_manager.isConnected());
}

void test_initialization_and_states(void) {
    // Avvia la connessione con credenziali fittizie
    net_manager.begin("TEST_SSID", "TEST_PASS");
    // All'avvio immediato (essendo asincrono e non bloccante) isConnected() deve essere ancora false
    TEST_ASSERT_FALSE(net_manager.isConnected());
}

void setup() {
    delay(2000); // Stabilizzazione porta seriale

    UNITY_BEGIN();
    RUN_TEST(test_initial_state);
    RUN_TEST(test_initialization_and_states);
    UNITY_END();
}

void loop() {
    // Loop vuoto
}
