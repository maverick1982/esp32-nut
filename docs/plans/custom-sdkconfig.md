---
type: plan
title: "custom_sdkconfig: opzioni ESP-IDF del progetto (pronto, non in uso)"
description: "Come riattivare custom_sdkconfig di pioarduino per esp32-s3-standard: opzioni misurate nella US-095, procedura, verifiche, trappole, tempi di build e CI."
tags: [plan, platform, esp-idf, sdkconfig, memory, US-095]
---
# `custom_sdkconfig`: opzioni ESP-IDF del progetto (pronto, non in uso)

## 1. Stato e decisione

Nella US-095 (2026-10-22/23) `custom_sdkconfig` di pioarduino è stato provato e misurato sull'Eaton 3S con la piattaforma 55.03.312-1 (ESP-IDF 5.5.5). Funziona e recupera circa 17 KB di heap a riposo, ma costringe a ricompilare ESP-IDF ("compilazione ibrida") alla prima build, a ogni modifica delle opzioni e in ogni release.

**Decisione del 2026-10-23:** non adottarlo per ora e tenere le librerie ESP-IDF precompilate del core Arduino. Il punto debole sotto carico (l'export della diagnostica) si risolve con la US-096, senza costi di build. Questa pagina e `custom-sdkconfig/sdkconfig.esp32-nut` contengono tutto quello che serve per riattivarlo.

**Quando riattivarlo:**
- segnalazioni di memoria insufficiente o di frammentazione sul campo (richieste fallite, export troncati, riavvii dopo giorni di funzionamento);
- bisogno di un'opzione di ESP-IDF che il core precompilato non offre, per esempio la configurazione reale del core dump al posto di `-Wl,--wrap=esp_core_dump_write` (ADR 0008) o i log di debug dello stack USB host.

## 2. Guadagni misurati

Misure sulla scheda dell'utente con l'Eaton 3S, heap libero dopo 10 minuti a riposo (media dei minuti 3-10). Dettagli e metodo in `platform-upgrade-idf55.md` §11.

| Opzione | Valore | Risparmio | Note |
|---|---|---|---|
| PSRAM spenta | decisa dalla piattaforma | ~10 KB | la scheda N8 non ha PSRAM; non serve scriverla nel file |
| `CONFIG_ESP_TIMER_TASK_STACK_SIZE` | 8192 → 4096 | 4,2 KB | il task usa < 512 byte sotto carico; 4096 come nella v1.8.0 |
| Statistiche di FreeRTOS (`TRACE_FACILITY`, `STATS_FORMATTING_FUNCTIONS`, `GENERATE_RUN_TIME_STATS`) | spente | 1,3 KB | spente anche nella v1.8.0; il firmware usa solo `uxTaskGetStackHighWaterMark()` |
| `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE` | 3000 → 256 | 3 KB | 256 come nella v1.8.0; `hid_host.c` alloca i propri trasferimenti |
| `CONFIG_BT_ENABLED` | spento | ~0 | scartata: il core già rilascia la memoria del controller |
| `CONFIG_ESP_IPC_TASK_STACK_SIZE` | 2048 | — | non cambiata: usa ~770 byte, la regola (uso + 1024) lascia 2048 |

Risultato complessivo: heap libero a riposo da 150 516 a 167 484 byte (+11 %), blocco libero più grande da 102 388 a 122 868 (+20 %), `min free` sotto carico con l'export ogni 60 s da 74 688 a 80 660 (+8 %).

Escluse a priori, perché cambiano il comportamento della rete o dello stack USB: buffer Wi-Fi e lwIP, IPv6, `USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT`, `USB_HOST_HUBS_SUPPORTED`, `USB_HOST_ENABLE_ENUM_FILTER_CALLBACK` (con il callback in `USBHostUPS::begin()`).

## 3. Come riattivarlo

1. Copiare `docs/plans/custom-sdkconfig/sdkconfig.esp32-nut` nella radice del progetto.
2. In `platformio.ini`, sezione `[env:esp32-s3-standard]`, aggiungere:
   ```ini
   ; Opzioni ESP-IDF del progetto: pioarduino ricompila le librerie ESP-IDF, quindi
   ; la prima build è molto più lunga. Vedi docs/plans/custom-sdkconfig.md
   custom_sdkconfig = file://sdkconfig.esp32-nut
   ```
3. Aggiungere al `.gitignore` i file che la piattaforma genera nella radice:
   ```
   # Generati da pioarduino con custom_sdkconfig (docs/plans/custom-sdkconfig.md)
   sdkconfig.defaults
   sdkconfig.esp32-s3-standard
   .dummy/
   managed_components/
   ```
4. Su Windows compilare da **PowerShell** (non da Git Bash) con `$env:PLATFORMIO_CACHE_DIR = 'D:\pc'` (o un altro percorso corto) e `pio run -e esp32-s3-standard`. La prima build impiega 16-21 minuti.
5. Fare le verifiche del §4 e aggiornare questa pagina se la piattaforma è cambiata.

Il nome del file non può essere `sdkconfig.defaults` né `sdkconfig.<env>`: la piattaforma genera e sovrascrive quei file nella radice (`builder/frameworks/espidf.py`, `arduino.py`).

## 4. Verifiche dopo l'attivazione

- **Opzioni effettive.** Confrontare il `sdkconfig.esp32-s3-standard` generato con la copia del precompilato in `idf55-baseline/sdkconfig-precompiled-55.03.312-1.txt`: devono cambiare solo le opzioni del file, PSRAM e flash (DIO, 8 MB). Restano invariati: le tre opzioni USB del §2, core dump in flash in formato ELF, `ESP_TASK_WDT_PANIC=y`, buffer Wi-Fi. Con una piattaforma diversa dalla 55.03.312-1, rifare prima la copia del precompilato.
- **Core dump.** `__wrap_esp_core_dump_write` presente in `.pio/build/esp32-s3-standard/firmware.map`.
- **Rete di sicurezza.** Due build temporanee con `abort()` nel loopTask e con il loopTask bloccato senza `esp_task_wdt_reset()`: dopo il riavvio `Reset reason: PANIC` / `TASK_WDT` e il riepilogo del crash (task, PC, backtrace).
- **USB.** Enumerazione all'avvio e dopo uno stacca e riattacca dell'UPS (con i trasferimenti di controllo a 256).
- **Stack del loopTask.** La riga `[DIAG] Loop Task Stack High Water Mark` sopra 8 KB: `-DARDUINO_LOOP_STACK_SIZE=16384` vale anche con la compilazione ibrida.
- **Regressione.** Suite nativa, e2e, `scripts/nut_snapshot.py` confrontato con `idf55-baseline/`, test di carico (§7).

## 5. Trappole note

- **Togliere un'opzione** dal file non la riporta al valore precompilato: il `sdkconfig.esp32-s3-standard` generato la conserva. Dopo averne tolta una, cancellarlo.
- **La ricompilazione parte solo se cambiano contenuto o data di modifica** del file. Copiando un file preparato prima, la data resta quella vecchia e la build usa le librerie già compilate: aggiornare la data (`(Get-Item sdkconfig.esp32-nut).LastWriteTime = Get-Date`).
- **Cambio di branch.** Passare a un branch senza `custom_sdkconfig` reinstalla le librerie precompilate; tornare indietro ricompila ESP-IDF da capo.
- **Windows.** `idf_tools.py` rifiuta di girare da Git Bash/MSYS ("MSys/Mingw is not supported"), quindi esptool non si installa e la build fallisce sul bootloader. Con `LongPathsEnabled=0` lo scompattamento delle librerie supera i 260 caratteri nella cache: serve `PLATFORMIO_CACHE_DIR` corto. La piattaforma avvisa che senza percorsi lunghi la build è più lenta, ma funziona.
- **esptool 5.4** (installato dalla 55.03.x) stampa la barra di avanzamento in Unicode: con l'uscita rediretta su file serve `PYTHONIOENCODING=utf-8`.
- **Altri processi PlatformIO** (PIO Home, `project init` di VS Code) durante la prima installazione fanno fallire l'installazione delle dipendenze Python della piattaforma.

## 6. Tempi di build e CI

- **Locale** (portatile dell'utente, Windows): prima build 16-21 minuti; build incrementale 2,5 minuti; ogni modifica del file 16-19 minuti.
- **CI (`release.yml`, Ubuntu):** la compilazione ibrida avviene a ogni release, perché il runner parte senza cache: stima di 10-15 minuti in più. Per toglierla, aggiungere uno step di cache di `~/.platformio` con una chiave che includa l'hash di `platformio.ini` e di `sdkconfig.esp32-nut`.
- **Non** provare `release.yml` con `workflow_dispatch`: pubblica una release e aggiorna il web installer su GitHub Pages.

## 7. Come misurare

Strumenti nel repository:
- `scripts/serial_log.py`: log della seriale con l'ora del PC e reset all'avvio (porta UART della DevKitC; l'UPS resta sulla USB nativa).
- `scripts/mem_probe_patch.py`: patch temporanea di `main.cpp` con l'heap per fase dell'avvio e lo stack minimo libero dei task ogni 5 minuti. Da non committare.
- `scripts/load_test.py`: test di carico (8 client NUT, Web UI ogni 2 s, export ogni 60 s).
- `scripts/nut_snapshot.py`: `upsc`, `upscmd -l` e `upsrw` senza gli strumenti NUT.

Flash via seriale senza cancellare la flash (le impostazioni restano); `boot_app0.bin` riporta l'avvio sulla prima partizione app anche se l'ultimo OTA era sull'altra:

```
python -m esptool --chip esp32s3 --port COM5 --baud 460800 --after no_reset write_flash 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

Metodo:
- confrontare due configurazioni **nella stessa sessione**: tra sessioni diverse il `min free` sotto carico varia di circa 20 KB;
- heap a riposo: media delle righe `[DIAG] Heap` dei minuti 3-10 dopo il reset;
- il `min free` è il minimo dall'avvio: prima di ogni test di carico, riavviare la scheda;
- per isolare il costo dell'export, ripetere il test senza export (copia dello script con `last_dump` iniziale uguale all'ora corrente).

## 8. Riferimenti

- `docs/plans/platform-upgrade-idf55.md` §11 (misure complete della US-095).
- `docs/plans/idf55-baseline/`: `sdkconfig` precompilati della 51.03.04 e della 55.03.312-1, report dei test di carico, baseline NUT della v1.8.0.
- `.archetipo/plans/US-095-plan.yaml` (piano e task), US-096 (export senza `JsonDocument`).
