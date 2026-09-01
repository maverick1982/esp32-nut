# Issue #21: Add support for Powercom SPD-750U

## Info Generali
- **Issue:** [#21](https://github.com/maverick1982/esp32-nut/issues/21)
- **Titolo:** Add support for Powercom SPD-750U
- **Branch:** `feature/issue-21-powercom`
- **Spec Archetipo:** `US-052`
- **Utente segnalatore:** `@fox1047`
- **Hardware identificato:** VID `0x0D9F`, PID `0x0004` (Manufacturer: `POWERCOM Co.,LTD`, Product: `HID UPS Battery`)

---

## Cronologia Eventi & Log

### 2026-08-21
- Creazione issue da parte dell'utente `@fox1047`.
- Errore iniziale riportato dall'utente su ESP32-S3:
  - `E (9708) HCD DWC: bInterval value (100) of Interrupt pipe exceeds max supported limit`
  - `[USBHostUPS] Error claiming interface 0: 262`
  - `[USBHostUPS] Device Info: Address 1, VID 0D9F, PID 0004`

### 2026-08-22 / 2026-08-23
- Implementato `PowercomDriver` (`lib/USBHostUPS/src/PowercomDriver.cpp`, `PowercomDriver.h`).
- Supporto al fallback su `GenericDriver` per estrazione standard HID usages.
- Implementato l'hack del Feature Report `0xA4` con payload a 8 byte (come NUT ufficiale) per la lettura del voltaggio della batteria.
- Gestione beeper e omessi valori a 0.0V per conformità NUT.
- Generato firmware di test e rilasciato all'utente.

### 2026-08-27 14:38
- Ripresa dei lavori sulla issue #21.
- Creato file di contesto `docs/issues/issue-21.md`.
- Eseguito push di `fix/issue-13-align-nut-web-telemetry` e rebase di `feature/issue-21-powercom`.
- Risolto conflitto tra polling continuo e comandi sincroni in `PowercomDriver::loop` aggiungendo `isControlPending()`.
- Corretta l'inversione del beeper in lettura e in scrittura (`setBeeper` usa `1 = enable`, `2 = disable` per Powercom).
- Aggiunti flag di presenza `data.has.*` su tutte le metriche Powercom.
- Generato firmware di test `test_powercom_driver_issue_21_v5.zip`.

### 2026-08-28 12:35
- Eseguito rebase del branch `feature/issue-21-powercom` su `main` (che include il nuovo Native Unit Testing & Record/Replay framework).
- Risolti i conflitti di disaccoppiamento polimorfico dell'interfaccia `IUSBHostUPS` in `PowercomDriver` e `IUPSDriver`.
- Integrata e validata la suite nativa `test_powercom_driver` (5 unit test dedicati al driver Powercom, decoding voltaggio report 0xA4, mapping beeper, encodeBeeperValue).
- Risolto bug di inizializzazione default su `HIDUsageDef` (`exponent` e `unit`).
- Acquisito e integrato il log diagnostico reale `usb_diagnostics.8.json` (Powercom SPD-750U, VID `0x0D9F`, PID `0x0004`) come fixture in `test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json`.
- Aggiunto il test di parsing completo per il Report Descriptor reale di 996 byte e il replay scenario in `FixtureReplayRunner`.
### 2026-08-28 12:55
- Compilato `firmware.bin` per l'ambiente `esp32-s3-standard` su `feature/issue-21-powercom`.
- Generato l'archivio `test_powercom_driver_issue_21_v6.zip` nella directory root del progetto da inviare all'utente `@fox1047` per il test su hardware reale.
