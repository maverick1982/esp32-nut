#ifndef DIAGNOSTIC_LED_H
#define DIAGNOSTIC_LED_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// Costanti per i periodi di lampeggio (millisecondi)
static const unsigned long LED_BLINK_SLOW_MS = 1000;  // Lampeggio lento (CONNECTING)
static const unsigned long LED_BLINK_FAST_MS = 125;   // Lampeggio veloce (ERROR)

#define COLOR_CONNECTING Adafruit_NeoPixel::Color(255, 255, 0)
#define COLOR_OPERATIONAL Adafruit_NeoPixel::Color(0, 255, 0)
#define COLOR_ERROR Adafruit_NeoPixel::Color(255, 0, 0)
#define COLOR_AP_MODE_BLUE Adafruit_NeoPixel::Color(0, 0, 255)
#define COLOR_AP_MODE_RED Adafruit_NeoPixel::Color(255, 0, 0)
#define COLOR_OFF 0

// Stati diagnostici del LED
enum LedState {
    CONNECTING,   // Lampeggio lento — connessione in corso
    OPERATIONAL,  // LED verde flash 200ms ogni 4s — funzionamento normale
    ERROR,        // Lampeggio veloce rosso — condizione di errore
    AP_MODE       // Animazione 8s (4s fast blue/red, 4s off) — modalità configurazione
};

class DiagnosticLED {
public:
    DiagnosticLED();

    // Pins from the board profile (US-090); a pin of -1 means no LED: every call is a no-op
    void begin(int8_t pin, int8_t power_pin);

    // Imposta lo stato corrente del LED
    void setState(LedState state);

    // User settings of the Board page (US-094): off keeps following the state but
    // shows nothing; brightness is the NeoPixel value (0-255)
    void setUserEnabled(bool on);
    void setBrightness(uint8_t value);

    // Aggiorna il pattern di lampeggio — chiamare nel loop principale
    void update();

    // Restituisce lo stato corrente
    LedState getState() const;

    // Restituisce l'ultimo colore impostato
    uint32_t getCurrentColor() const;

private:
    int8_t _pin;
    bool _enabled;      // the board has a LED and begin() ran
    bool _userOn;       // the user did not turn it off
    LedState _state;
    bool _ledOn;
    unsigned long _previousMillis;
    uint32_t _currentColor;
    Adafruit_NeoPixel _pixels;

    void setPixelColorAndShow(uint32_t color);
};

#endif // DIAGNOSTIC_LED_H
