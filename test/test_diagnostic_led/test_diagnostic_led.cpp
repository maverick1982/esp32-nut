/**
 * test_diagnostic_led.cpp
 *
 * Test unitari per la macchina a stati DiagnosticLED.
 * Eseguiti su hardware ESP32-S3 — usano le funzioni reali di Arduino.
 */

#include <Arduino.h>
#include <unity.h>
#include "DiagnosticLED.h"

// Rimuoviamo le macro problematiche da DiagnosticLED.h
#undef COLOR_CONNECTING
#undef COLOR_OPERATIONAL
#undef COLOR_ERROR
#undef COLOR_OFF

// Colori attesi (devono combaciare con DiagnosticLED.cpp)
#define TEST_COLOR_CONNECTING Adafruit_NeoPixel::Color(255, 255, 0)
#define TEST_COLOR_OPERATIONAL Adafruit_NeoPixel::Color(0, 255, 0)
#define TEST_COLOR_ERROR Adafruit_NeoPixel::Color(255, 0, 0)
#define TEST_COLOR_AP_MODE_BLUE Adafruit_NeoPixel::Color(0, 0, 255)
#define TEST_COLOR_AP_MODE_RED Adafruit_NeoPixel::Color(255, 0, 0)
#define TEST_COLOR_OFF 0

// Istanza globale del LED diagnostico usata in tutti i test
DiagnosticLED led;

// Pin utilizzato per i test (LED integrato della ESP32-S3-DevKitC-1)
static const uint8_t TEST_PIN = LED_BUILTIN_PIN;

void setUp(void) {
    // Reinizializza il LED prima di ogni test per garantire isolamento
    led.begin(TEST_PIN);
}

void tearDown(void) {
    // Spegni il LED a fine test per lasciare lo stato pulito
    led.setState(CONNECTING); // Questo imposta COLOR_OFF
}

// ---------------------------------------------------------------------------
// 1. Stato iniziale: dopo begin(), lo stato deve essere CONNECTING
// ---------------------------------------------------------------------------
void test_initial_state_is_connecting(void) {
    // begin() viene già chiamato in setUp()
    TEST_ASSERT_EQUAL(CONNECTING, led.getState());
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());
}

// ---------------------------------------------------------------------------
// 2. Transizioni di stato: setState() cambia lo stato, getState() lo riflette
// ---------------------------------------------------------------------------
void test_setState_changes_state(void) {
    // Transizione da CONNECTING (default) a OPERATIONAL
    led.setState(OPERATIONAL);
    TEST_ASSERT_EQUAL(OPERATIONAL, led.getState());

    // Transizione da OPERATIONAL a ERROR
    led.setState(ERROR);
    TEST_ASSERT_EQUAL(ERROR, led.getState());

    // Transizione da ERROR a CONNECTING
    led.setState(CONNECTING);
    TEST_ASSERT_EQUAL(CONNECTING, led.getState());

    // Transizione a AP_MODE
    led.setState(AP_MODE);
    TEST_ASSERT_EQUAL(AP_MODE, led.getState());
}

// ---------------------------------------------------------------------------
// 3. Pattern OPERATIONAL: Flash verde 200ms, spento 4000ms
// ---------------------------------------------------------------------------
void test_operational_blink_asymmetric(void) {
    led.setState(OPERATIONAL);
    led.update();

    // Subito dopo update, il LED deve essere spento (5000ms off)
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());

    // Aspetta meno di 5000ms
    delay(4900);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());

    // Supera i 5000ms
    delay(150);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OPERATIONAL, led.getCurrentColor());

    // Aspetta meno di 100ms
    delay(50);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OPERATIONAL, led.getCurrentColor());

    // Supera i 100ms
    delay(100);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());
}

// ---------------------------------------------------------------------------
// 3b. Pattern AP_MODE: 4s animazione (blu/rosso), 4s spento
// ---------------------------------------------------------------------------
void test_ap_mode_animation(void) {
    led.setState(AP_MODE);
    led.update();

    // Subito all'inizio, phase è ~0, quindi colore blu
    TEST_ASSERT_EQUAL(TEST_COLOR_AP_MODE_BLUE, led.getCurrentColor());

    // Delay per cambiare fase a rosso (> 125ms)
    delay(150);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_AP_MODE_RED, led.getCurrentColor());

    // Delay per superare i 4000ms e verificare lo spegnimento
    delay(4000);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());
}

// ---------------------------------------------------------------------------
// 4. Pattern CONNECTING: lampeggio lento giallo
// ---------------------------------------------------------------------------
void test_connecting_blink_slow(void) {
    led.setState(CONNECTING);

    // Subito dopo setState il LED è spento
    led.update();
    uint32_t initialState = led.getCurrentColor();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, initialState);

    // Attendi meno dell'intervallo di toggle (500ms) — il LED non deve cambiare
    delay(100);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());

    // Attendi fino a superare l'intervallo di toggle (~500ms totali)
    delay(450);  // 100 + 450 = 550ms > 500ms
    led.update();
    uint32_t afterFirstToggle = led.getCurrentColor();

    // Lo stato del LED deve essere cambiato (toggle avvenuto)
    TEST_ASSERT_EQUAL(TEST_COLOR_CONNECTING, afterFirstToggle);

    // Attendi un altro intervallo per verificare il secondo toggle
    delay(550);
    led.update();
    uint32_t afterSecondToggle = led.getCurrentColor();

    // Deve essere tornato allo stato iniziale (Nero)
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, afterSecondToggle);
}

// ---------------------------------------------------------------------------
// 5. Pattern ERROR: lampeggio veloce rosso
// ---------------------------------------------------------------------------
void test_error_blink_fast(void) {
    led.setState(ERROR);

    // Subito dopo setState il LED è spento
    led.update();
    uint32_t initialState = led.getCurrentColor();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, initialState);

    // Attendi meno dell'intervallo di toggle (62ms) — il LED non deve cambiare
    delay(30);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());

    // Attendi fino a superare l'intervallo di toggle (~62ms totali)
    delay(50);  // 30 + 50 = 80ms > 62ms
    led.update();
    uint32_t afterFirstToggle = led.getCurrentColor();

    // Lo stato del LED deve essere cambiato (toggle avvenuto)
    TEST_ASSERT_EQUAL(TEST_COLOR_ERROR, afterFirstToggle);

    // Attendi un altro intervallo per verificare il secondo toggle
    delay(80);
    led.update();
    uint32_t afterSecondToggle = led.getCurrentColor();

    // Deve essere tornato allo stato iniziale
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, afterSecondToggle);
}

// ---------------------------------------------------------------------------
// 6. Bonus: setState() con lo stesso stato non resetta il pattern
// ---------------------------------------------------------------------------
void test_setState_same_state_is_noop(void) {
    led.setState(OPERATIONAL);
    led.update();
    // Aspetta per superare i 5000ms ed accendere il LED
    delay(5050);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OPERATIONAL, led.getCurrentColor());

    // Richiamare setState con lo stesso valore non deve spegnere il LED o resettare il timer
    led.setState(OPERATIONAL);
    TEST_ASSERT_EQUAL(TEST_COLOR_OPERATIONAL, led.getCurrentColor());
    TEST_ASSERT_EQUAL(OPERATIONAL, led.getState());
}

// ---------------------------------------------------------------------------
// 7. Bonus: transizione da OPERATIONAL a CONNECTING spegne il LED
// ---------------------------------------------------------------------------
void test_transition_from_operational_resets_led(void) {
    led.setState(OPERATIONAL);
    led.update();
    // Accendi il LED
    delay(5050);
    led.update();
    TEST_ASSERT_EQUAL(TEST_COLOR_OPERATIONAL, led.getCurrentColor());

    // Passaggio a CONNECTING deve spegnere il LED
    led.setState(CONNECTING);
    TEST_ASSERT_EQUAL(TEST_COLOR_OFF, led.getCurrentColor());
    TEST_ASSERT_EQUAL(CONNECTING, led.getState());
}

void setup() {
    delay(2000);  // Stabilizzazione porta seriale

    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_connecting);
    RUN_TEST(test_setState_changes_state);
    RUN_TEST(test_operational_blink_asymmetric);
    RUN_TEST(test_ap_mode_animation);
    RUN_TEST(test_connecting_blink_slow);
    RUN_TEST(test_error_blink_fast);
    RUN_TEST(test_setState_same_state_is_noop);
    RUN_TEST(test_transition_from_operational_resets_led);
    UNITY_END();
}

void loop() {
    // Loop vuoto — i test vengono eseguiti una sola volta in setup()
}
