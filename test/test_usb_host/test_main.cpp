#include <Arduino.h>
#include <unity.h>
#include "USBHostUPS.h"

USBHostUPS test_usb_ups;

void setUp(void) {
    // Predisposizione per ciascun test
}

void tearDown(void) {
    // Pulizia dopo ciascun test
}

void test_usb_host_initialization(void) {
    // Verifica che l'inizializzazione dello stack USB Host vada a buon fine
    TEST_ASSERT_TRUE(test_usb_ups.begin());
}

void setup() {
    // Delay per stabilizzare la connessione seriale e permettere al monitor di agganciarsi
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_usb_host_initialization);
    UNITY_END();
}

void loop() {
    // Loop vuoto
}
