✅ Rework Hardware Completato!

**Spec:** US-006: LED Diagnostico di Sistema
**Status:** REVIEW
**Commit Type:** fix

**Implementation summary:**
- Fix tasks completed: 1/1 (Abilitazione alimentazione NeoPixel su ESP32-S3)
- È stato aggiunto `NEOPIXEL_POWER_PIN` (GPIO 38) nell'header.
- `DiagnosticLED::begin()` ora imposta esplicitamente il `NEOPIXEL_POWER_PIN` su `OUTPUT` e `HIGH` per alimentare fisicamente il circuito WS2812 prima che Adafruit_NeoPixel inizi a comunicare.
- Compilazione verificata su esp32-s3-devkitc-1 con successo.

*Il firmware ora fornisce fisicamente l'alimentazione al LED integrato della scheda, consentendone l'accensione. Il codice è pronto per essere collaudato.*
