---
type: plan
title: "Issue #47 (fix v3): Crash dopo Control Transfer Timeout — Analisi e Piano di Risoluzione"
description: "Analisi dei log seriali CP1300/CP1500 (fix v2) e piano per eliminare deadlock, corruzione HCD e recupero mancato."
tags: [plan, usb, hid, crash, watchdog, issue-47]
---
# Issue #47 (fix v3): Crash dopo Control Transfer Timeout

## 1. Contesto

Feedback di x-magic sul fix v2: stesso firmware su due UPS CyberPower (CP1300 e CP1500), entrambi con scheda VCC-GND YD-ESP32-S3 alimentata dalla porta USB dell'UPS.

- **CP1300**: crash con backtrace ogni 2-4 ore; all'ultimo crash non riparte più. Il watchdog non interviene.
- **CP1500**: si blocca ~2 ore dopo un errore USB Host e non riparte.

L'utente ipotizza brownout o reset dell'HID da parte dell'UPS durante i cambi di modalità.

Log analizzati:
- `cp1300-20260923_183101.log` (35 971 righe)
- `cp1500-20260923_183042.log` (5 252 righe)

## 2. In sintesi

Nei log c'è un'unica catena di cause, che si ripete in **6 eventi su 6**:

1. **Il timeout del control transfer lo causiamo noi.** Due task del firmware si bloccano a vicenda (deadlock tra loopTask e task HID).
2. **Il driver HID gestisce male il timeout.** Il transfer resta in volo e il semaforo di completamento si sfasa. Da lì in poi il codice scrive nel buffer di un URB ancora in mano all'HCD, e l'ISR USB va in crash.
3. **Il crash non si chiude bene.** Il core dump su flash è attivo ma la partizione non c'è, quindi il panic rientra in se stesso; a volte non ne esce più. Il "watchdog" attuale (ISR di un hw_timer) non può intervenire.

## 3. Analisi dettagliata

### 3.1 Il deadlock che genera il timeout

- Il loopTask entra in `_driver->loop()` tenendo `_mutex` (`lib/USBHostUPS/src/USBHostUPS.cpp:238`). Poi chiama `requestReport()` → `hid_class_request_get_report()` → `hid_control_transfer()`, che resta bloccato fino a 5 s (`DEFAULT_TIMEOUT_MS`) in attesa di `ctrl_xfer_done`.
- `ctrl_xfer_done` lo consegna il task HID in background (`lib/usb_host_hid/hid_host.c:180`, tramite `usb_host_client_handle_events`). Lo stesso task consegna anche gli INPUT report (`in_xfer_done`).
- Se arriva un INPUT report mentre il GET_REPORT è in corso, il callback `handle_interface_event` prova a prendere `_mutex` (`USBHostUPS.cpp:125`) e si blocca. Il completamento del control transfer resta in coda dietro di lui, quindi scatta il timeout di 5 s.

**Riscontro nei log.** Il CyberPower invia INPUT report (id=8, id=11) ogni 3 s. In tutti gli eventi ne manca almeno uno prima del timeout. Il report rimasto in coda viene consegnato subito dopo il rilascio del lock, ~10 ms dopo la riga `requestReport FAILED`:

| Log | Timeout (uptime ms) | Ultimo INPUT prima | Primo INPUT dopo |
|---|---|---|---|
| CP1500 | 3069803 | −4.7 s | +358 ms |
| CP1500 | 5860124 | −7.6 s | +358 ms |
| CP1300 | 14440671 | −7.6 s | +358 ms |
| CP1300 | 10690568 | −4.7 s | +364 ms |
| CP1300 | 6730397 | −7.6 s | +364 ms |
| CP1300 | 8440125 | −7.6 s | +364 ms |

La collisione dipende da una finestra di pochi ms (INPUT report che arriva durante un GET_REPORT), e per questo succede solo ogni 2-4 ore a istanti casuali.

### 3.2 Il timeout non viene gestito, e il sistema si corrompe

In `hid_control_transfer` (`lib/usb_host_hid/hid_host.c:1033-1036`) il codice esce su timeout senza fare halt/flush/clear di EP0. Il commento dice che *"USBH will reset the endpoint"*, ma non è vero. Il callback arriva comunque in ritardo e lascia un token spurio nel semaforo binario `ctrl_xfer_done`. Da quel momento:

- ogni GET_REPORT successivo ritorna subito, prima che il transfer sia completato, e legge dati vecchi o sfasati. Si vede nel CP1300 alle 05:44:30: `Voltage = 0.0 V` per 25 s con UPS in OL;
- il codice riscrive setup packet e buffer (`hid_class_request_get`, `memcpy`) mentre l'URB è ancora in mano all'HCD.

I backtrace lo confermano:
- **crash 1** (22:31:57): il primo frame è nell'ISR USB (`0x4206061d → 0x4205e84b → 0x4205e7f1`), che ha interrotto il loopTask mentre era dentro una `memcpy` in ROM (`0x40056f6a`). La catena parte da `loop()` → `requestReport`;
- **crash 4** (05:45:20): `abort()` (`0x40380eb0 / 0x4037eee6 / 0x4037df78`) chiamato dall'ISR USB, cioè un assert fallito nell'HCD.

Tempo tra timeout e crash: ~1 s, ~0 s, 85 s, 55 s (CP1300). Sul CP1500 il log finisce 22 s dopo il timeout.

Altri difetti nello stesso file:
- lo `status` del transfer non viene mai controllato. C'è il commento *"Check transfer status"* ma nessuna verifica, quindi un transfer in STALL o in errore restituisce `ESP_OK`;
- se il re-submit dell'IN transfer fallisce, l'errore viene ignorato (`hid_host.c:950`). Probabilmente è per questo che nei crash 3 e 4 gli INPUT report spariscono del tutto per 55-85 s senza alcun log;
- `usb_class_request_get_descriptor` fa `usb_host_transfer_free()` su `ctrl_xfer` senza verificare che non sia in volo (`hid_host.c:1074`).

### 3.3 Perché la scheda non si riprende

**Core dump.** `sdkconfig.defaults` (`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`) **viene ignorato** con `framework = arduino`: le librerie precompilate del core Arduino hanno il core dump su flash attivo, ma `partitions.csv` non ha la partizione `coredump`. Il risultato si vede nei log:
- al boot compare `E (346) esp_core_dump_flash: No core dump partition found!`;
- al panic, backtrace annidati (`0x420280b0 … 0x4037750d 0x40378aa4`, lo SP scende di 0x210 a ogni rientro), poi `Re-entered core dump! Exception happened during core dump!`;
- la riga che dovrebbe contenere `Guru Meditation Error` esce corrotta (`xV���?0`, `�),d`);
- nell'ultimo crash del CP1300 (05:45:20) la ricorsione non termina: il log finisce con i backtrace, la scheda resta appesa e non riparte.

**Watchdog.** In `src/core/main.cpp:20-22` e `48-51` c'è un `hw_timer` la cui ISR chiama `esp_restart()`:
- chiamare `esp_restart()` da una ISR non è sicuro: esegue gli shutdown handler (WiFi incluso), che non vanno chiamati in contesto ISR;
- soprattutto, durante un panic gli interrupt sono disabilitati, quindi il timer non scatta mai. Questo spiega *"the watchdog didn't do anything"*.

### 3.4 Sull'ipotesi brownout / reset USB dell'UPS

**Questi log non supportano l'ipotesi:**
- tutti i reset dopo i crash sono `rst:0xc (RTC_SW_CPU_RST)`, cioè riavvii software dopo un panic, e nessuno è di tipo brownout;
- i `POWERON` a inizio log sono simultanei sulle due schede (18:31:15 e 18:31:18, uptime ~3070 s su entrambe), quindi li ha fatti l'utente all'avvio della cattura;
- nessun evento `HID Device Disconnected`, quindi l'UPS non ha re-enumerato il device.

Resta un punto aperto: il log del CP1500 finisce alle 20:09:21 senza nessun output. Non si può distinguere tra un blocco (probabile, stessa causa) e la perdita di alimentazione della scheda. Il logging di `esp_reset_reason()` (F4) servirà a chiarirlo in futuro.

## 4. Piano di implementazione (in ordine di priorità)

### F1: eliminare il deadlock (è la causa principale)
- Il callback HID non deve mai bloccarsi. In `USBHostUPS::handle_interface_event` basta copiare il report grezzo in una coda FreeRTOS (`xQueueSend` con timeout 0; se la coda è piena il report viene scartato e contato).
- Gestire allo stesso modo l'evento `DISCONNECTED`: accodarlo ed eseguire `hid_host_device_close` e il reset dello stato nel loopTask.
- Decodifica, cache dei report e modifiche a `UPSData` passano nel loopTask. `_mutex` resta solo a protezione dei lettori (web server, NUT server).
- Regola generale da documentare: nessun lock applicativo tenuto durante un control transfer bloccante.

### F2: rendere robusto `hid_control_transfer` (`lib/usb_host_hid/hid_host.c`)
- Prima del submit, svuotare il semaforo (`xSemaphoreTake(ctrl_xfer_done, 0)`).
- Su timeout, fare `usb_host_endpoint_halt` / `usb_host_endpoint_flush` / `usb_host_endpoint_clear` su EP0, come fa l'upstream `cdc_acm_host`, poi aspettare il callback (stato `CANCELED`) per un tempo limitato. Se il callback non arriva, marcare il pipe di controllo come inutilizzabile (`ESP_ERR_INVALID_STATE` alle richieste successive) fino alla riapertura del device.
- Tenere un flag `ctrl_inflight` (impostato al submit, azzerato in `ctrl_xfer_done`) e rifiutare submit o riallocazioni mentre è attivo, anche a `hid_host.c:1074`.
- Verificare `ctrl_xfer->status == USB_TRANSFER_STATUS_COMPLETED`, altrimenti restituire `ESP_ERR_INVALID_RESPONSE`.
- In `in_xfer_done`, controllare il ritorno di `usb_host_transfer_submit` e, in caso di errore, loggare e notificare `HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR`.
- **Da verificare:** che la versione di ESP-IDF inclusa in pioarduino `51.03.04` supporti halt/flush su EP0. Se non lo supporta, ripiegare sul marcare il pipe come inutilizzabile e riaprire il device (F3).
- Registrare la deviazione dalla libreria upstream vendorizzata in un nuovo **ADR 0008**.

### F3: recupero a livello applicativo
- Contatore di errori consecutivi e backoff del polling dei Feature Report dopo un timeout.
- Watchdog sul link: se per N secondi (es. 30-60; il CyberPower invia INPUT ogni 3 s) non arrivano né un GET_REPORT riuscito né INPUT report, fare un recupero controllato: chiudere e riaprire il device HID.
- Escalation: dopo M tentativi falliti, `esp_restart()` dal loopTask (mai da ISR), salvando il motivo in RTC memory o NVS e loggandolo al boot successivo.
- Nel frattempo marcare i dati come stale: il NUT server risponde `ERR DATA-STALE` invece di servire valori congelati (nel crash 3 lo stato restava `OL` con valori fermi).

### F4: rete di sicurezza per i crash
- Sostituire il `hw_timer` in `src/core/main.cpp` con il Task WDT: `esp_task_wdt_reconfigure` (timeout 30 s, `trigger_panic = true`), `esp_task_wdt_add(NULL)` in `setup()`, `esp_task_wdt_reset()` in `loop()`.
- Aggiungere a `partitions.csv`:
  ```
  coredump, data, coredump, 0x7F0000, 0x10000,
  ```
  Entra esattamente negli 8 MB (app1 finisce a 0x7F0000). Con la partizione presente il panic si chiude e la scheda si riavvia.
  - Al boot, leggere `esp_core_dump_get_summary()` e scrivere PC e backtrace nel log web, così gli utenti possono mandarceli senza console seriale.
  - **Limite:** l'OTA non aggiorna la tabella delle partizioni. Serve un flash completo una tantum via web installer, che invece scrive `partitions.bin` all'offset 0x8000 (`web-installer/manifest.json`). Va detto nelle release notes.
- **Rete di sicurezza per chi aggiorna via OTA** (la tabella delle partizioni resta quella vecchia, senza `coredump`):
  - `build_flags` con `-Wl,--wrap=<funzione core dump chiamata dal panic handler>`. Il nome va verificato con `nm` su `libesp_system.a` della piattaforma: in IDF 5.1 dovrebbe essere `esp_core_dump_to_flash`, nelle versioni più recenti `esp_core_dump_write`;
  - la `__wrap_...` controlla una variabile impostata al boot (`esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL) != NULL`). Se la partizione esiste chiama la funzione originale (`__real_...`), altrimenti ritorna subito: il panic si chiude, i watchdog vengono riarmati e la scheda si riavvia;
  - nella UI web e nel log al boot, segnalare la partizione mancante e invitare a ripetere un flash completo con il web installer.
- Loggare `esp_reset_reason()` al boot ed esporlo nella diagnostica web, così un eventuale brownout si potrà provare o escludere.
- Rimuovere `sdkconfig.defaults` o documentare che con `framework = arduino` non ha effetto.

**Decisione:** partizione `coredump` più `--wrap` condizionale. Scartate:
- solo `--wrap` senza partizione: si perde la diagnosi post-mortem;
- `custom_sdkconfig` di pioarduino: probabilmente assente nella 51.03.04, richiede un aggiornamento della piattaforma e allunga molto la prima build;
- build ibrida `arduino, espidf`: invasiva e con tempi di build lunghi.

Nota: il panic handler chiama `disable_all_wdts()` prima di scrivere il core dump e riarma i watchdog solo alla fine. Per questo un'eccezione durante il core dump lascia la scheda appesa anche con il Task WDT attivo. Che l'eccezione avvenga proprio lì è dedotto da `Re-entered core dump!` e dalla struttura dello stack, non verificato con l'ELF: la verifica è che i panic annidati spariscano dopo il fix.

### F5: test e validazione
- **Riproduzione deterministica:** un flag di build di debug che fa `delay(6000)` nel callback INPUT. Prima del fix deve dare timeout (e poi crash); dopo il fix nessun errore e nessun timeout.
- Unit test nativi (`env:native`) per la macchina a stati di recupero (F3): backoff, watchdog sul link, escalation, dati stale.
- Soak test di più giorni con x-magic sui due UPS, chiedendo log seriali completi con reset reason ed eventuale summary del core dump.

## 5. Verifica su tutti i log della issue (2026-09-24)

Ricontrollati tutti i log allegati o incollati nella issue, compresi i quattro del 2026-09-24 (`cp1300-20260924_082854`, `cp1300-20260924_173251`, `cp1500-20260924_082848`, `cp1500-20260924_173243`). Questi girano ancora con il fix v2 (`hid_control_transfer(1034)`) e con le schede alimentate esternamente.

### 5.1 Un solo meccanismo, confermato su 22 timeout su 22

| Log | Firmware | Timeout | INPUT mancante prima | Esito |
|---|---|---|---|---|
| comment 2026-09-22 | fix v1 | 2 | 4,7 / 7,6 s | panic immediato (stack canary, poi panic annidati) |
| dev-debug | pre-v1 | 2 | ~7,5 s | panic |
| cp1300/cp1500 2026-09-23 | fix v2 | 6 | 4,7-7,6 s | panic (4), fine log (1), inizio cattura (1) |
| cp1300/cp1500 2026-09-24 | fix v2 | 11 | 4,7-13 s | panic (5), reset da watchdog HW TG0/TG1/RTC (3), blocco muto (3) |

- In ogni caso manca almeno un INPUT report prima del timeout, e il report trattenuto arriva 348-364 ms dopo la riga `Control transfer timeout`. È il deadlock di §3.1: il callback INPUT del task HID aspetta `_mutex`, tenuto dal loopTask dentro il GET_REPORT.
- Dopo il timeout: `Voltage = 0.0 V` (fino a 30 righe) prima del crash, cioè le risposte sfasate di §3.2. Crash dall'ISR USB con lo stesso backtrace (`0x40380eb0 0x4037eee6 0x4037df78 ... 0x4206061d`), poi `Re-entered core dump!` e panic annidati. Tempo tra timeout e crash: da 0 a 504 s.
- Blocchi senza uscita: cp1300 09:11 (la scheda riparte solo al power-on manuale 7,7 ore dopo), cp1500 15:29 (ferma per 1,4 ore sui backtrace annidati), fine log a 19:41 e a 21:06. I reset `RTCWDT_RTC_RST` due secondi dopo un panic confermano il panic handler bloccato nel core dump senza partizione (§3.3).
- Nessun reset `BROWNOUT` in nessun log, e i crash continuano con alimentazione esterna: il brownout è escluso.
- Il reset USB dell'UPS esiste (`USBH: Device 1 gone`, 3 volte nel log cp1300 della mattina, riconnessione in circa 0,4 s), ma nessun crash lo segue: non è la causa. Il firmware attuale lo gestisce nel task di polling (disconnect differito, dati `ERR DATA-STALE` durante la riconnessione, enumerazione robusta C2).

### 5.2 Cause dei log più vecchi, già corrette prima della v3

- `Stack canary watchpoint triggered (IDLE0)`, PC `0x42028281` (log v1.5.1, dev, fix v1): stack del task HID da 4 KB. Nei log v2 (stack a 8 KB) non compare più.
- `Core 1 LoadProhibited` in `memcpy` della ROM con `EXCVADDR 0` (v1.5.1): compatibile con la `memcpy` di lunghezza negativa su un transfer fallito (ADR 0008), corretta nella v3. Senza l'ELF di quella versione non si può confermare il frame.

### 5.3 Copertura nel firmware attuale (branch `fix/issue-47`, fasi 1-3 della review)

| Sintomo nei log | Correzione |
|---|---|
| Timeout del control transfer (deadlock) | callback HID mai bloccanti, eventi in coda (`eaf98e3`); dal 2026-09-24 polling in un task dedicato (review A5b) |
| Dati sfasati / `0.0 V` / crash dall'ISR USB | URB di EP0 mai toccato mentre è in volo, semaforo svuotato, stato verificato (`968862b`) |
| Panic annidati, blocchi, watchdog inerte | Task WDT su loopTask e `ups_poll`, partizione `coredump` + `--wrap` condizionale (`a88b3fc`) |
| Scheda che non riparte da sola | scala di recupero `LinkMonitor` + restart controllati con backoff e modalità degradata (review A7) |
| Valori congelati serviti come validi | `ERR DATA-STALE` a pipe ferma, a device scollegato (A1) e con watchdog INPUT (A2) |
| Traffico EP0 del CyberPower | full poll ogni 30 s dei soli FEATURE, niente GET_REPORT degli INPUT (fase 3) |

**Cosa non è dimostrato:**
- nessuna prova su un CyberPower: il firmware attuale non è ancora stato dato a x-magic;
- i blocchi muti non hanno output e la loro causa è dedotta (lo stato corrotto dopo il timeout), non osservata;
- se una scheda si bloccasse con gli interrupt disabilitati su entrambi i core, resta solo l'interrupt watchdog (il reset `TG1WDT_SYS_RST` nel log cp1500 mostra che funziona);
- senza un flash completo con il web installer non c'è partizione `coredump`: il panic si chiude lo stesso (wrap), ma senza dettagli post-mortem.

### 5.4 Prossimo passo con x-magic

Firmware di test `test_fix_issue_47_v3.zip` (commit `dd2a740` su `main`, versione `fix-issue-47-v3`; nei commenti della issue è citato come `6a8e871`, lo stesso commit sul branch di sviluppo prima del rebase-merge, con contenuto identico), da installare con un **flash completo** dal web flasher di Espressif, perché serve ad aggiungere la partizione `coredump`:
- opzione A: `bootloader.bin` a `0x0`, `partitions.bin` a `0x8000`, `boot_app0.bin` a `0xe000`, `firmware.bin` a `0x10000`, senza "Erase Flash", così le impostazioni restano;
- opzione B: immagine unica a `0x0`, che però cancella le impostazioni.

`boot_app0.bin` riscrive `otadata`. Senza, una scheda che dopo un numero dispari di OTA gira da `app1` continuerebbe ad avviare il vecchio firmware anche dopo il flash. Per lo stesso motivo il file è stato aggiunto anche al manifest del web installer e a `release.yml`.

Opzione A provata il 2026-09-25 su ESP32-S3 con Eaton 3S: flash riuscito e funzionamento regolare.

Da verificare nei log:
- nessun `Control transfer timeout` preceduto da un INPUT mancante; un eventuale timeout isolato deve dare solo backoff (`link failures`, `polling paused`), mai dati `0.0 V` o crash;
- riepilogo `[USB] Last 60 s`: circa 40 INPUT report al minuto (id 8 e 11 ogni 3 s) e un GET_REPORT ogni 30 s per report FEATURE;
- al boot `[DIAG] Reset reason`, `Last controlled restart` e `Last crash` (con backtrace dal core dump);
- ~~build di debug con `-DUSBUPS_DEBUG_SLOW_INPUT_MS=6000` per la riproduzione deterministica (F5)~~: il flag è stato rimosso prima del merge. Con il servizio USB nel task `ups_poll` rallentava il task stesso fino al Task WDT invece di provocare un timeout (vedi la Fase 4 di `usb-layer-review.md`).

### 5.5 Esito del firmware v3 (2026-09-26)

Feedback di x-magic sulla issue (`cp1300-20260925_230317.log`, `cp1500-20260925_231222.log`, dump `usb_diagnostics.json` e `/api/system-status` di entrambi):
- **nessun reboot, crash o timeout** in 13,9 h (CP1300) e 19,6 h (CP1500) di uptime continuo, anche nei passaggi tra rete e batteria;
- ~96.600 GET_REPORT riusciti, 0 falliti; 40 INPUT al minuto senza buchi; nessuna risposta sfasata e nessun `0.0 V`;
- 11 reset USB dell'UPS, tutti gestiti: riconnessione in ~0,4 s, dati validi entro 1-6 s, nel frattempo `ERR DATA-STALE` ai client NUT;
- flash completo con esptool riuscito: NVS conservata, partizione `coredump` presente, `coredump_partition: true`, errore `esp_core_dump_flash` al boot sparito;
- heap stabile a ~215,4 KB dalla prima ora.

Con il fix v2 in un periodo simile si erano osservati una quindicina di timeout con deadlock (§5.1), tutti seguiti da crash o blocchi: la causa radice risulta eliminata. Il dettaglio è nella Fase 4 di `docs/plans/usb-layer-review.md`.

Risposta pubblicata sulla issue il 2026-09-26: i fix v3 verranno uniti su `main` e usciranno con la prossima release ufficiale. A x-magic sono stati chiesti qualche giorno in più di test e una prova del beeper.
