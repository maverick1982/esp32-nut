✅ Migrazione Core RGB Completata!

**Spec:** US-006: LED Diagnostico di Sistema
**Status:** REVIEW
**Commit Type:** fix

**Implementation summary:**
- Fix task completed: Migrazione da Adafruit_NeoPixel a `neopixelWrite` (Core nativo).
- Abbiamo rimosso la dipendenza da `Adafruit_NeoPixel` per evitare conflitti con il driver RMT interno alle ESP32-S3.
- Utilizziamo `rgbLedWrite` (nuovo standard core Arduino dalla v3) assieme alla costante hardware nativa `RGB_BUILTIN` per inviare i colori.
- Abbiamo mantenuto `delay(10)` e l'abilitazione del pin di potenza (`NEOPIXEL_POWER_PIN`) nel caso serva.
- Compilazione eseguita con successo.

*Se sulla board è presente un NeoPixel indirizzato al pin nativo RGB_BUILTIN, ora si accenderà senza conflitti.*
