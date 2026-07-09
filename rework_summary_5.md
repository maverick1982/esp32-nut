✅ Configurazione Definitiva NeoPixel su GPIO 47 Completata!

**Spec:** US-006: LED Diagnostico di Sistema
**Status:** REVIEW
**Commit Type:** fix

**Implementation summary:**
- Fix task completed: Rimosso il Pin Sweeper e ripristinato l'uso della libreria `Adafruit_NeoPixel`.
- Il LED RGB target è ora permanentemente configurato sul **GPIO 47**, come indicato dal produttore.
- Rimosso qualsiasi forzatura del GPIO 38 (`NEOPIXEL_POWER_PIN`) che poteva interferire con il funzionamento della scheda.
- Ripristinata l'interfaccia visiva: CONNECTING (Giallo lampeggiante), OPERATIONAL (Verde fisso) e ERROR (Rosso lampeggiante).
- I test unitari sono stati adattati per asserire i valori RGB generati dalla libreria Adafruit.
- Il firmware compila con successo per l'ambiente `esp32-s3-devkitc-1`.

Pronto per l'accettazione finale da parte dell'utente!
