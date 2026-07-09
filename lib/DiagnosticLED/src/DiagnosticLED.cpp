/**
 * DiagnosticLED.cpp
 *
 * Implementazione della classe DiagnosticLED.
 * Gestisce il LED diagnostico con pattern di lampeggio non-bloccanti
 * basati su millis() usando Adafruit_NeoPixel.
 */

#include "DiagnosticLED.h"

// Costruttore — inizializza i membri a valori predefiniti
DiagnosticLED::DiagnosticLED()
    : _pin(LED_BUILTIN_PIN),
      _state(CONNECTING),
      _ledOn(false),
      _previousMillis(0),
      _currentColor(0),
      _pixels(1, LED_BUILTIN_PIN, NEO_GRB + NEO_KHZ800) {}

void DiagnosticLED::setPixelColorAndShow(uint32_t color) {
    _currentColor = color;
    _pixels.setPixelColor(0, color);
    _pixels.show();
}

void DiagnosticLED::begin(uint8_t pin) {
    _pin = pin;

    // Assicurati che l'alimentazione del NeoPixel sia attiva
    pinMode(NEOPIXEL_POWER_PIN, OUTPUT);
    digitalWrite(NEOPIXEL_POWER_PIN, HIGH);

    _pixels.begin();
    _pixels.setBrightness(10);
    _pixels.setPixelColor(0, 0, 0, 0); // Spento inizialmente
    _pixels.show();
    
    _ledOn = false;
    _state = CONNECTING;
    _previousMillis = millis();
}

// Cambia lo stato corrente e resetta il timer del pattern
void DiagnosticLED::setState(LedState state) {
    if (_state == state) return;  // Nessun cambiamento necessario

    _state = state;
    _previousMillis = millis();
    _ledOn = false;
    setPixelColorAndShow(COLOR_OFF);
}

// Aggiorna il pattern di lampeggio in base allo stato corrente
void DiagnosticLED::update() {
    unsigned long currentMillis = millis();

    switch (_state) {
        case CONNECTING:
            if (currentMillis - _previousMillis >= LED_BLINK_SLOW_MS / 2) {
                _previousMillis = currentMillis;
                _ledOn = !_ledOn;
                setPixelColorAndShow(_ledOn ? COLOR_CONNECTING : COLOR_OFF);
            }
            break;

        case OPERATIONAL:
            // Nello stato OPERATIONAL il LED rimane fisso, nessun toggle
            if (!_ledOn) {
                _ledOn = true;
                setPixelColorAndShow(COLOR_OPERATIONAL);
            }
            break;

        case ERROR:
            if (currentMillis - _previousMillis >= LED_BLINK_FAST_MS / 2) {
                _previousMillis = currentMillis;
                _ledOn = !_ledOn;
                setPixelColorAndShow(_ledOn ? COLOR_ERROR : COLOR_OFF);
            }
            break;
    }
}

// Restituisce lo stato corrente del LED
LedState DiagnosticLED::getState() const {
    return _state;
}

// Restituisce l'ultimo colore impostato
uint32_t DiagnosticLED::getCurrentColor() const {
    return _currentColor;
}
