# Issue #13: Align NUT server variables with Web UI telemetry & USB stability

## Informazioni Generali
- **Branch:** `fix/issue-13-align-nut-web-telemetry`
- **Data creazione contesto:** 2026-08-26 21:49

## Cronologia Eventi & Log

### 2026-08-26 16:58 - Fix allineamento variabili NUT Server e Web UI
- **Problema:** Discrepanza tra le variabili e la telemetria esposta via NUT Server rispetto alla Web UI.
- **Modifiche effettuate:**
  - Aggiornato `NUTServer.cpp` e `app.js` per sincronizzare la nomenclatura e i formati dei dati.
  - Aggiornati gli asset web (`web_assets.h` e relativo hash).
  - Aggiornati i test unitari in `test/test_nut_server/test_main.cpp`.
- **Commit:** `4143db1`

### 2026-08-26 17:15 - Teardown USB Host pulito prima del reboot
- **Problema:** Possibile stallo/blocco della comunicazione USB con l'UPS al momento del riavvio dell'ESP32.
- **Modifiche effettuate:**
  - Implementata la procedura di teardown pulito in `USBHostUPS.cpp` / `USBHostUPS.h`.
  - Invocato il teardown prima del riavvio in `src/network/web_config_server.cpp`.
- **Commit:** `8fe3122`

### 2026-08-28 09:21 - Fix esposizione `battery.mfr.date` e `battery.date` (Feedback Hal9k0)
- **Problema:** L'utente segnala la mancanza del parametro `battery.mfr.date` sia via NUT Server sia su interfaccia Web.
- **Modifiche effettuate:**
  - Aggiunto `battery.mfr.date` e `battery.date` in `NUTServer.cpp` (`LIST VAR` e `GET VAR`).
  - Separati `batteryMfrDate` e `batteryDate` in `UPSData.h`.
  - Aggiornato `APCDriver.cpp` e `GenericDriver.cpp` con protezione contro sovrascritture di valori nulli (`v <= 0`) e conformità Y2K esatta di NUT.
  - Aggiunto `battery.date` a Web UI (`app.js` e `web_config_server.cpp`).
  - Generato firmware di test `test-pre-release_v5.zip` e pubblicato commento di risposta per l'utente nella Issue #13.

## Feedback Utenti
- **2026-08-28:** Inviato `test-pre-release_v5.zip` a `@Hal9k0` per verificare `battery.mfr.date` e `battery.date`.

## Prossimi Step & TODO
- [ ] Attendere riscontro dall'utente `@Hal9k0`.
