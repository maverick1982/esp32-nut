---
type: plan
title: "Aggiornamento della piattaforma a pioarduino 55.03.x (ESP-IDF 5.5) — Analisi"
description: "Cosa guadagnerebbe il progetto passando da pioarduino 51.03.04 (IDF 5.1, Arduino 3.0.4) a 55.03.312-1 (IDF 5.5.5, Arduino 3.3.12), cosa no, rischi e piano per un'implementazione futura."
tags: [plan, platform, esp-idf, arduino, usb, upgrade, issue-36]
---
# Aggiornamento della piattaforma a pioarduino 55.03.x (ESP-IDF 5.5)

## 1. Contesto

Analisi del 2026-09-25, nata dall'issue #36 (Powercom SPD-750U). Nei log di @fox1047 (`esp-web-tools-logs` 11-14, firmware v12-v15) c'è un loop di riavvii ogni 7 s:

```
HUB: Incorrect number of bytes returned 8: CHECK_FULL_CONFIG_DESC
HUB: Stage failed: CHECK_FULL_CONFIG_DESC
assert failed: root_port_handle_events hub.c:837 (p_hub_driver_obj->single_thread.root_dev_uid != 0)
```

L'assert è nello stack USB host di ESP-IDF, precompilato nel core Arduino (`libusb.a`). Il nostro codice non può correggerlo né aggirarlo. L'unica strada è un IDF più recente, quindi una nuova piattaforma.

**Stato:** in corso (US-089, branch `feature/pdr-0002-multi-board`).

## 2. Versioni

Il numero di versione di pioarduino è `<IDF major><IDF minor>.<Arduino major>.<Arduino minor+patch>`.

| | pioarduino | Core Arduino | ESP-IDF | Data |
|---|---|---|---|---|
| **In uso** (`platformio.ini`) | `51.03.04` | 3.0.4 | 5.1 (`release/v5.1`, commit `b6b4727c58` del 2024-07-31) | — |
| **Ultima stabile** | `55.03.312-1` | 3.3.12 | 5.5.5 | 2026-09-22 |
| Release candidate | `61.04.00-RC1` | 4.0.0-RC1 | 6.1 | 2026-09-23 |

- In uso: verificato sui pacchetti installati (`framework-arduinoespressif32/package.json` → 3.0.4; `framework-arduinoespressif32-libs/versions.txt` → `esp-idf: release/v5.1 b6b4727c58`).
- `55.03.312-1` corregge solo la compilazione "verbose" rispetto a `55.03.312`. IDF 5.5.5 dalle note di rilascio di arduino-esp32 3.3.12.
- La `61.04` (IDF 6.1, Arduino 4.0) è esclusa per ora: è una RC con cambio di major sia per IDF sia per Arduino.

## 3. Guadagni concreti

### 3.1 L'assert del Powercom sparisce (motivo principale)

**Nella nostra base.** Nel sorgente IDF al commit `b6b4727c58`, `components/usb/hub.c`, `root_port_handle_events()`:
```c
case ROOT_PORT_STATE_ENABLED:
    // There is an enabled (active) device. We need to indicate to USBH that the device is gone
    pass_event_to_usbh = true;
...
if (pass_event_to_usbh) {
    // The port must have a device object
    assert(p_hub_driver_obj->single_thread.root_dev_uid != 0);
```
Dopo un'enumerazione fallita la porta resta `ENABLED` ma senza un oggetto device. Basta una disconnessione per far scattare l'assert.

**La sequenza nei log di fox1047:**
1. l'UPS si enumera;
2. senza una richiesta nei primi ~3 s si disconnette (`Device 1 gone` 3,3 s dopo il claim);
3. la nuova enumerazione fallisce (`CHECK_FULL_CONFIG_DESC`);
4. assert e reboot, in loop.

Riferimento upstream: [espressif/esp-idf#13364](https://github.com/espressif/esp-idf/issues/13364) ("[v5.1.2] USB crashes in hub.c when unplugging device", `hub.c:838`).

**L'IDF 5.1.7 non basta.** Il fix portato sul ramo 5.1 ([`6df6ab7d`](https://github.com/espressif/esp-idf/commit/6df6ab7d), 2025-02-26, "Fix disconnection error handling") aggiunge solo il caso `ROOT_PORT_STATE_RECOVERY`, cioè due disconnessioni ravvicinate ([#15290](https://github.com/espressif/esp-idf/issues/15290)). Non tocca l'assert su `root_dev_uid`.

**Nell'IDF 5.5 il caso è gestito** (sorgenti in `~/.platformio/packages/framework-espidf`, 5.5.2):
- un'enumerazione annullata genera `ENUM_EVENT_CANCELED` → `hub_port_disable()` (`usb_host.c:401`) → la root port passa a `DISABLED`;
- la disconnessione in stato `DISABLED` va direttamente in recupero: "no device object to clean up" (`hub.c:418-423`), senza assert;
- `root_dev_uid` non esiste più: il device tree è gestito da `dev_tree_node_*`.

Verificato leggendo il codice, non ancora sul Powercom.

### 3.2 Reset della porta USB senza riavviare la scheda

L'IDF 5.5 ha `usb_host_lib_set_root_port_power(bool enable)` e `usb_host_config_t.root_port_unpowered` (in IDF dalla 5.2.4 / 5.3.2 / 5.4: "Added option to initialize USB Host stack with unpowered port"). L'IDF 5.1 non li ha: confronto degli header `usb/usb_host.h` fra le due versioni.

Oggi, quando un URB di EP0 non torna (ADR 0008) o il device non risponde più, l'ultimo gradino della scala di recupero è `esp_restart()`. Spegnendo e riaccendendo la porta si ottiene un vero reset USB del device e una nuova enumerazione, senza perdere WiFi, client NUT e log. È un gradino da inserire prima del restart controllato (review A7, `RestartPolicy`).

Altre API nuove nella 5.5, oggi non necessarie: `usb_host_get_config_desc()` / `usb_host_free_config_desc()` (multi-configurazione).

### 3.3 `custom_sdkconfig` di pioarduino (da verificare)

Le release recenti di pioarduino permettono di impostare opzioni sdkconfig anche con `framework = arduino`, ricompilando le librerie. Va verificato sulla versione scelta. Potrebbe:
- sostituire il trucco `-Wl,--wrap=esp_core_dump_write` (ADR 0008) con una configurazione reale del core dump;
- abilitare le opzioni di debug USB host e di log che oggi non possiamo toccare;
- rendere di nuovo utile `sdkconfig.defaults`, rimosso perché ignorato.

Costo: la prima build è molto più lunga, e va verificato l'impatto sulla CI (`release.yml`).

### 3.4 Guadagni minori dal core Arduino (3.0.4 → 3.3.12)

Voci delle note di rilascio che toccano componenti che usiamo; nessuna è legata a problemi osservati nei nostri log:
- **WebServer:** "Harden security and fix hangs" (3.3.12, arduino-esp32#12794). Il WebServer gira nel loopTask sotto Task WDT, e un blocco lì causa un panic.
- **WebServer:** risposte chunked (3.3.2, #11894). `/api/usb/dump` e `/api/logs` si potrebbero mandare a pezzi invece di costruire un'unica `String`, con meno picchi di heap (utile per S6 e i soak test).
- **WebServer:** reset di `_contentLength` dopo lo streaming (3.3.8, #12385).
- **Update / OTA:** controllo della partizione OTA libera (3.3.6, #12264); checksum SHA-256 opzionale del firmware (3.3.12, #12824).
- **WiFi:** correzione dei crash nei cicli init/deinit (3.3.9, #12592). Rilevante solo se passiamo tra AP e STA a runtime.

## 4. Cosa non guadagneremmo

- **Timeout degli URB.** `usb_transfer_t::timeout_ms` resta "currently not supported yet" anche nella 5.5 (`usb_types_stack.h`). Le protezioni di `hid_host.c` (`ctrl_inflight`, ADR 0008) restano necessarie.
- **Halt/flush di EP0 da un client.** Restano rifiutati (già verificato sulla 5.5 per l'ADR 0008).
- **Heap corruption [#15815](https://github.com/espressif/esp-idf/issues/15815)** ("data_buffer_size ... is not correct", corretta in 5.3.4 / 5.4.2 / 5.5). Nella nostra base `urb_alloc()` imposta `data_buffer_size` alla dimensione richiesta, quindi il difetto non ci riguarda.
- **Le altre voci USB host tra 5.2 e 5.5** (hub esterni, ESP32-P4, ISOC, low-speed dietro un hub full-speed) non riguardano il nostro hardware.
- **Stringhe dei descriptor.** Il fix per i device non conformi (5.1.3) è già nella nostra base. Un fallimento della stringa seriale (`CHECK_SHORT_SER_STR_DESC`) già oggi non blocca l'enumerazione, e nella 5.5 resta tollerato.
- **`CHECK_FULL_CONFIG_DESC`.** Nella 5.5 un descriptor di configurazione corto annulla ancora l'enumerazione. La differenza è che la scheda non va più in assert: la porta viene ri-enumerata.

## 5. Rischi e lavoro da fare

| Area | Lavoro |
|---|---|
| `lib/usb_host_hid/hid_host.c` (vendorizzato) | Riallineare all'API USB host 5.5 conservando le deviazioni marcate `[esp32-nut, ...]` (ADR 0007, ADR 0008, review C2/C3/C4/A3/A4/A5a/M3/M5). Valutare il confronto con l'ultima versione upstream di `usb_host_hid`. |
| Vincoli dell'ADR 0008 | Riverificare sulla 5.5: EP0 non annullabile, `timeout_ms` non supportato, `usb_host_transfer_free()` senza controllo di volo, comportamento di `_pipe_cmd_clear` (data toggle, review A3). |
| Interrupt IN multi-pacchetto (review A4) | Riverificare che il transfer termini solo con un pacchetto corto o a buffer pieno (`_buffer_fill_intr` / `usbh.c`). |
| Core dump | Il nome della funzione chiamata dal panic handler (`esp_core_dump_write`) e il `--wrap` in `platformio.ini`; oppure passare a `custom_sdkconfig`. |
| Task WDT | Firma di `esp_task_wdt_config_t` / `esp_task_wdt_reconfigure()` in `main.cpp`. |
| Core Arduino 3.3 | API di WiFi, WebServer, Preferences, Update; avvisi di deprecazione. |
| Flash e partizioni | Dimensione del binario (oggi 38,4% di 4 MB per slot); `partitions.csv` invariata. |
| CI / release | `release.yml` (percorso di `boot_app0.bin` cercato con `find`), web installer. |
| Test nativi | `env:native` non dipende dalla piattaforma: devono restare verdi senza modifiche. |

## 6. Piano proposto

1. **Prerequisito:** validare sulla piattaforma attuale i fix dell'issue #47 (x-magic, CyberPower) e dell'issue #36 (fox1047, Powercom, con le migliorie di `feature/issue-36` riportate). In questo modo un problema nuovo si potrà attribuire alla piattaforma.
2. **Branch dedicato** (es. `feature/platform-idf55`) e **ADR** per la decisione (template in `docs/adr/`).
3. **Prova di compilazione** con `platform = https://github.com/pioarduino/platform-espressif32.git#55.03.312-1` (o la stabile più recente in quel momento): elenco degli errori e stima del lavoro.
4. **Riallineamento** di `hid_host.c` e di `USBHostUPS` (§5), senza nuove funzionalità.
5. **Recupero con reset della porta:** nuovo gradino in `LinkMonitor` / `USBHostUPS`, `usb_host_lib_set_root_port_power(false/true)` prima di `requestRestart()`, con test nativi della scala di recupero.
6. **Validazione:** test nativi, build, prova su Eaton 3S; poi firmware di test per i tester (Powercom in primis: il loop di assert deve sparire; CyberPower, APC); soak test di 24-72 h.
7. **Rilascio** con note che spiegano il cambio di piattaforma (flash completo non necessario se `partitions.csv` resta invariata).

## 7. Criteri di accettazione

- Powercom SPD-750U: dopo una disconnessione dell'UPS con enumerazione fallita, nessun `assert failed: root_port_handle_events`. La scheda ri-enumera senza riavviarsi.
- Nessuna regressione su Eaton, CyberPower e APC rispetto alla piattaforma 51.03.04 (stessi valori NUT, nessun nuovo timeout o crash in 24 h).
- Recupero di un link bloccato tramite reset della porta, senza `esp_restart()`, nella maggior parte dei casi (visibile nei log).
- Test nativi verdi; dimensione del firmware entro lo slot OTA.

## 8. Riferimenti

- Issue: #36 (Powercom), #47 (CyberPower); piani `docs/plans/issue-47-usb-ctrl-timeout-crash.md` e `docs/plans/usb-layer-review.md`; ADR 0007 e ADR 0008.
- ESP-IDF: [#13364](https://github.com/espressif/esp-idf/issues/13364), [#15290](https://github.com/espressif/esp-idf/issues/15290), [#15815](https://github.com/espressif/esp-idf/issues/15815), commit [`6df6ab7d`](https://github.com/espressif/esp-idf/commit/6df6ab7d); note di rilascio v5.1.3 → v5.5.5 (sezioni "USB Host").
- Core Arduino: note di rilascio 3.0.5 → 3.3.12 ([arduino-esp32 releases](https://github.com/espressif/arduino-esp32/releases)).
- Piattaforma: [pioarduino/platform-espressif32 releases](https://github.com/pioarduino/platform-espressif32/releases).

## 9. Verifica sulla 5.5.5 (US-089)

Verifica del 2026-10-21 sui sorgenti ESP-IDF al tag `v5.5.5` (`tools/cmake/version.cmake` → 5.5.5), scaricati da GitHub. Il pacchetto `framework-arduinoespressif32-libs` della 55.03.312-1 non era ancora installato: la versione viene dalle note di rilascio di Arduino 3.3.12 (§2). Righe riferite al tag `v5.5.5`, percorsi sotto `components/`.

| Vincolo | Esito sulla 5.5.5 | Riferimento (file:riga, tag v5.5.5) | Conseguenza per `hid_host.c` |
|---|---|---|---|
| EP0 non annullabile da un client | Invariato. `usb_host_endpoint_halt/flush/clear` passano da `usbh_ep_get_handle()`, che rifiuta EP0 con `check_ep_addr()` (*"EP0 is the owned/managed by USBH"*) | `usb/usb_host.c:1520-1576`; `usb/usbh.c:197-207`, `1486` | `ctrl_inflight` e `hid_device_lock_ctrl()` (ADR 0008) restano necessari |
| `timeout_ms` non supportato | Invariato: *"currently not supported yet"*. Nessun uso in `usb_host.c`, `usbh.c`, `hcd_dwc.c`, `enum.c`, `hub.c` | `usb/include/usb/usb_types_stack.h:175` | Timeout software in `hid_control_transfer()` e URB lasciato allo stack: restano necessari |
| `usb_host_transfer_free()` senza controllo di volo | Invariato: libera sempre (`urb_free()`), il vincolo *"must not be in-flight"* è solo nel commento. Il submit rifiuta un URB già in volo (`ESP_ERR_NOT_FINISHED`), ma il setup packet è già stato riscritto prima | `usb/usb_host.c:1591-1599`; `usb/usb_private.c:55-62`; `usb/include/usb/usb_host.h:603-615`; submit: `usb/usb_host.c:1621`, `1654` | Leak del contesto in `hid_host_uninstall_device()` e rifiuto del realloc in `usb_class_request_get_descriptor()` restano necessari |
| Data toggle dopo `_pipe_cmd_clear` (review A3) | Invariato: il clear rimette il pipe `ACTIVE` e riesegue i buffer, senza toccare il PID. DATA0 solo all'allocazione del canale (claim); `set_pid` solo nelle fasi di controllo | `usb/hcd_dwc.c:1846-1866`, `2313`, `2371`; `hal/usb_dwc_hal.c:318`; `hal/esp32s3/include/hal/usb_dwc_ll.h:770` | Recovery A3 (`stop` → CLEAR_FEATURE → `start`) invariato: il primo pacchetto dopo il clear può ancora andare perso |
| Interrupt IN multi-pacchetto (review A4) | Invariato: un transfer IN deve essere multiplo di MPS, il flag HOC è solo sull'ultimo QTD, e il transfer termina prima solo con un pacchetto corto (`stop_idx`) | `usb/hcd_dwc.c:2152-2192` (`_buffer_fill_intr`), `2413-2461` (`_buffer_parse_intr`) | `in_xfer` resta di un pacchetto; `InputReassembler` resta necessario |
| Assert del Powercom (§3.1) | Sparito: `root_dev_uid` non esiste più in `hub.c`. Un'enumerazione annullata chiude il device (`stage_cancel`) e genera `ENUM_EVENT_CANCELED` → `hub_port_disable()`. Alla disconnessione: `dev_tree_node_dev_gone()` → `usbh_devs_remove()` → `USBH_EVENT_DEV_FREE` → `hub_port_recycle()` → recupero della porta, senza assert | `usb/usb_host.c:401-402`, `339-345`; `usb/hub.c:378-442`, `504-516`, `670-698`; `usb/enum.c:860-889`; `usb/usbh.c:894-930` | Nessuna (fuori da `hid_host.c`) |

Note:
- **Precisazione su §3.1.** Lo stato `ROOT_PORT_STATE_DISABLED` (`hub.c:60`) non viene mai assegnato, né nella 5.5.2 né nella 5.5.5: dopo un'enumerazione annullata lo stato del hub resta `ENABLED`. Il recupero passa quindi dalla catena `dev_gone` → `DEV_FREE` → `recycle` descritta in tabella, non dal ramo `DISABLED` di `hub.c:418-423`. L'esito non cambia: niente assert, la porta si ri-enumera. Finché il device resta collegato, però, la porta rimane disabilitata: la nuova enumerazione parte solo dopo una disconnessione (il Powercom si disconnette da solo) o con `usb_host_lib_set_root_port_power()` (§3.2).
- **Differenze 5.5.2 → 5.5.5 nello stack USB host.** `hub.c`, `usbh.c`, `hcd_dwc.c` e `usb_types_stack.h` sono identici. `enum.c` controlla la lunghezza ricevuta dei descriptor corti e della tabella LANGID (`enum.c:544-547`, `584-587`, `650-652`). `usb_host_interface_release()` aspetta 10 tick se un endpoint del client ha un URB non ancora processato (`usb_host.c:1483-1494`): se la release gira nel task che chiama `usb_host_client_handle_events()` l'attesa non serve, e l'errore resta gestito dalla deviazione M3.
- **`usb_host_config_t`.** Ha `root_port_unpowered` (`usb_host.h:108`), `enum_filter_cb` (`usb_host.h:112`, tipo in `usb_types_stack.h:71`), `fifo_settings_custom` (`usb_host.h:114-122`) e `peripheral_map` (`usb_host.h:124`, 0 = periferica di default). Il campo `enum_filter_cb` c'è sempre, ma viene usato solo con `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK` (`Kconfig:167`, default `n`; `private_include/enum.h:21-23`; `usb_host.c:550-553`). Nelle librerie Arduino in uso è disattivato (`sdkconfig`: *"is not set"*). Il `configs/defconfig.esp32s3` di `esp32-arduino-lib-builder` (`master`, IDF `release/v5.5`) lo attiva, insieme a `USB_HOST_HUBS_SUPPORTED`, `USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT` (FIFO RX di 128 byte invece di 408) e `USB_HOST_CONTROL_TRANSFER_MAX_SIZE=3000` (oggi 256). **Confermato** sul `sdkconfig` di `framework-arduinoespressif32-libs/esp32s3` della 55.03.312-1 (`esp-idf: v5.5.5 b774170ff46`): `ENABLE_ENUM_FILTER_CALLBACK=y` (il callback in `USBHostUPS::begin()` è necessario), `HUBS_SUPPORTED=y`, `HW_BUFFER_BIAS_PERIODIC_OUT=y`, `CONTROL_TRANSFER_MAX_SIZE=3000`. La FIFO RX più piccola è un cambio di comportamento dello stack, non modificabile senza ricompilare le librerie: da tenere d'occhio nelle prove con gli UPS (gli UPS sono full-speed, MPS fino a 64 byte).
- **Build della US-089 (2026-10-21).** `esp32-s3-standard` compila senza avvisi nei nostri sorgenti: `firmware.bin` 1 987 680 byte (v1.8.0: 1 961 072), RAM statica 16,6% (prima 15,2%). Note pratiche su Windows: la piattaforma installa le sue dipendenze Python nel `penv` di PlatformIO dall'interno del processo `pio`, e fallisce se un altro processo PlatformIO (PIO Home, `project init` di VS Code) è attivo; `idf_tools.py` rifiuta di girare da Git Bash/MSYS ("MSys/Mingw is not supported"), quindi la prima installazione va fatta da PowerShell o cmd; con `LongPathsEnabled=0` un header Matter dell'ESP32-C5 supera i 260 caratteri nella cartella temporanea, e serve `PLATFORMIO_CACHE_DIR` su un percorso corto (per esempio `D:\pioc`) solo per la prima installazione.
- **Esito.** Nessun vincolo è cambiato: tutte le deviazioni `[esp32-nut, ADR 0008]` e `[esp32-nut, review A3/A4]` di `hid_host.c` restano necessarie e corrette sulla 5.5.5.

## 10. Validazione sull'Eaton 3S (US-089)

Prove del 2026-10-21 sulla scheda dell'utente con un Eaton 3S 700, firmware `idf55-v1`. Riferimento della v1.8.0 in `docs/plans/idf55-baseline/`.

- **Funzionale:** aggiornamento via OTA dalla v1.8.0 (bootloader 5.1 con app 5.5) riuscito, con Wi-Fi, credenziali NUT, login web e variabili salvate conservati; flash completo riuscito; `upsc`, `upscmd -l` e `upsrw` identici alla v1.8.0; Web UI, AP con doppio reset e UPS staccato e riattaccato senza riavvio funzionanti.
- **USB:** `[USB] Last 60 s: 0 INPUT reports (0 packets), 244 GET_REPORT ok, 0 failed`, identico alla misura del 2026-09-24 sull'IDF 5.1 (`usb-layer-review.md`): l'Eaton 3S non invia INPUT report periodici in OL stabile, e il polling fa 244 richieste al minuto.
- **Heap a riposo:**

| | v1.8.0 | idf55-v1 | idf55-v1 dopo 1 h |
|---|---|---|---|
| Free | 184 540 | 147 620 | 147 984 |
| Min free | 169 300 | 122 536 | 118 920 |
| Blocco più grande | 172 020 | 102 388 | 102 388 |

  Nessuna perdita: free e blocco più grande stabili dopo un'ora.

- **Heap sotto carico** (`scripts/load_test.py`, 900 s: 7 client NUT dello script più un client esterno sull'ottavo slot, Web UI ogni 2 s, export della diagnostica USB ogni 60 s; stesse condizioni per le due versioni; report in `idf55-baseline/stresstest-*.txt`):

| | v1.8.0 | idf55-v1 | Differenza |
|---|---|---|---|
| Free più basso | 151 028 | 110 324 | −40 KB |
| Min free | 99 188 | 53 252 | −46 KB |
| Blocco più grande, minimo | 122 868 | 54 260 | −67 KB |

  Nessun errore NUT o web su entrambe; il nono client è rifiutato su entrambe (US-084).

- **Conclusione.** Nessuna regressione funzionale. Il carico costa lo stesso sulle due piattaforme (circa 65-70 KB dal riposo al picco); la differenza è un costo fisso della 5.5 di circa 40-46 KB, più una frammentazione maggiore. Il margine nel caso peggiore scende da circa 100 KB a circa 50 KB. Il recupero della memoria (`custom_sdkconfig`, export della diagnostica a pezzi) è una spec separata, da completare prima del rilascio con IDF 5.5. La soglia di 100 KB dello script è troppo severa per questo test: anche la v1.8.0 la manca di poco.
- **Rete di sicurezza (TASK-08).** Due build temporanee, non committate, sulla stessa scheda:
  - `abort()` nel loopTask dopo 90 s: riavvio con `Reset reason: PANIC`, riepilogo del core dump nel log e nella diagnostica (task `loopTask`, PC, backtrace di 6 indirizzi);
  - loopTask occupato per 60 s senza `esp_task_wdt_reset()`: il Task WDT va in panic dopo 30 s, riavvio con `Reset reason: TASK_WDT` e riepilogo del core dump del `loopTask`.

  L'intercettazione `-Wl,--wrap=esp_core_dump_write` (ADR 0008) funziona anche sulla 5.5. In entrambi i casi la scheda riparte e serve i dati in meno di 2 s.
