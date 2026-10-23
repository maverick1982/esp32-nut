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

**Stato:** in corso (US-089 e US-095, branch `feature/pdr-0002-multi-board`).

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

### 3.3 `custom_sdkconfig` di pioarduino (verificato nella US-095, §11)

Le release recenti di pioarduino permettono di impostare opzioni sdkconfig anche con `framework = arduino`, ricompilando le librerie. Va verificato sulla versione scelta. Potrebbe:
- sostituire il trucco `-Wl,--wrap=esp_core_dump_write` (ADR 0008) con una configurazione reale del core dump;
- abilitare le opzioni di debug USB host e di log che oggi non possiamo toccare;
- rendere di nuovo utile `sdkconfig.defaults`, rimosso perché ignorato.

Costo: la prima build è molto più lunga, e va verificato l'impatto sulla CI (`release.yml`).

Verificato nella US-095: funziona con la 55.03.312-1 e recupera memoria (§11), ma per ora non è adottato (§11.8). Procedura per riattivarlo in `custom-sdkconfig.md`. L'intercettazione del core dump resta com'è.

### 3.4 Guadagni minori dal core Arduino (3.0.4 → 3.3.12)

Voci delle note di rilascio che toccano componenti che usiamo; nessuna è legata a problemi osservati nei nostri log:
- **WebServer:** "Harden security and fix hangs" (3.3.12, arduino-esp32#12794). Il WebServer gira nel loopTask sotto Task WDT, e un blocco lì causa un panic.
- **WebServer:** risposte chunked (3.3.2, #11894). `/api/usb/dump` e `/api/logs` si potrebbero mandare a pezzi invece di costruire un'unica `String`, con meno picchi di heap (utile per S6 e i soak test). Fatto per `/api/usb/dump` nella US-095 (§11.4); `/api/logs` resta com'è.
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

## 11. Recupero della memoria (US-095)

Misure del 2026-10-22 e 2026-10-23 sulla scheda dell'utente con l'Eaton 3S (flash via seriale, log dalla UART, stessa sessione di prova per i confronti).

### 11.0 Opzioni della v1.8.0 e della 5.5 a confronto

Dai `sdkconfig` precompilati delle due piattaforme (copie in `idf55-baseline/sdkconfig-precompiled-51.03.04.txt` e `sdkconfig-precompiled-55.03.312-1.txt`), sulle opzioni valutate dalla US-095:

| Opzione | v1.8.0 (51.03.04, IDF 5.1) | 55.03.312-1 (IDF 5.5.5) |
|---|---|---|
| `ESP_TIMER_TASK_STACK_SIZE` | 4096 | 8192 |
| `ESP_IPC_TASK_STACK_SIZE` (per core) | 1024 | 2048 |
| `FREERTOS_USE_TRACE_FACILITY` | n | y |
| `FREERTOS_GENERATE_RUN_TIME_STATS` | n | y |
| `FREERTOS_USE_STATS_FORMATTING_FUNCTIONS` | n | y |
| `USB_HOST_CONTROL_TRANSFER_MAX_SIZE` | 256 | 3000 |
| `BT_ENABLED` | y | y |
| `SPIRAM` | y | y |

Buffer Wi-Fi, lwIP (socket, finestre TCP, stack di `tiT`), stack di `sys_evt`, del timer di FreeRTOS e del main task sono uguali. Le opzioni che la US-095 riporta ai valori della v1.8.0 sono quindi già state usate in produzione su questa scheda. Bluetooth e PSRAM erano attivi anche nella v1.8.0.

### 11.1 Bilancio sulla scheda (TASK-01, 2026-10-23)

Due build temporanee con la stessa patch di misura (righe `[MEM]` all'avvio e stack minimo libero dei task ogni 5 minuti), sulla scheda dell'utente con l'Eaton 3S, flash via seriale e log dalla UART: `mem-idf55` (codice del branch, librerie 5.5 precompilate, come idf55-v1) e `mem-v180` (tag v1.8.0). Stesse condizioni per le due: 10 minuti a riposo, poi `load_test.py` per 900 s con 7 client dello script e il client esterno sull'ottavo slot. Report in `idf55-baseline/stresstest-mem-idf55.txt` e `stresstest-mem-v180.txt`.

**Heap durante l'avvio** (byte):

| | v1.8.0 | 5.5 | Differenza |
|---|---|---|---|
| Heap interno totale | 356 852 | 346 344 | −10 508 |
| Libero a inizio `setup()` | 314 240 | 294 848 | −19 392 |
| Costo di `network_mgr.begin()` (Wi-Fi) | 50 168 | 62 032 | +11 864 |
| Costo di server web, auth e attesa di 3 s | 8 348 | 8 436 | +88 |
| Costo di `usb_ups.begin()` e server NUT | 28 132 | 32 320 | +4 188 |
| Libero a fine `setup()` | 227 592 | 192 060 | −35 532 |

**A riposo e sotto carico** (byte):

| | v1.8.0 | 5.5 | Differenza |
|---|---|---|---|
| Libero dopo 10 minuti | 188 736 | 150 516 | −38 220 |
| Blocco più grande dopo 10 minuti | 176 116 | 102 388 | −73 728 |
| Libero più basso sotto carico | 154 224 | 113 464 | −40 760 |
| `min free` sotto carico | 112 884 | 74 688 | −38 196 |
| Blocco più grande, minimo sotto carico | 114 676 | 61 428 | −53 248 |

Il carico costa lo stesso sulle due piattaforme (circa 76 KB dal riposo al `min free`). Il costo fisso della 5.5 si divide così: circa 10,5 KB di heap totale in meno (memoria statica e riservata delle librerie), circa 9 KB allocati prima di `setup()` (stack più grandi dei task di sistema, vedi sotto), circa 12 KB in più all'avvio del Wi-Fi e circa 4 KB in più per lo stack USB e il server NUT. Questo `min free` della 5.5 (74 688) è più alto di quello della US-089 (53 252, §10) nelle stesse condizioni: fra due sessioni di prova la variabilità è di circa 20 KB, quindi i confronti valgono solo nella stessa sessione.

Durante il test della v1.8.0 il Wi-Fi è caduto una volta per 15 s (`[NETWORK] Wi-Fi connection lost!`, riconnessione automatica, tutti gli 8 client ricollegati subito): un errore NUT per client e un errore web nel report, non legati alla memoria.

**Stack dei task di sistema** (minimo libero dall'avvio, dopo il carico; byte):

| Task | Stack 5.5 | Usato 5.5 | Stack v1.8.0 | Usato v1.8.0 |
|---|---|---|---|---|
| `esp_timer` | 8192 | 488 | 4096 | 504 |
| `ipc0` | 2048 | 768 | 1024 | 836 |
| `ipc1` | 2048 | 760 | 1024 | 776 |

Gli altri task (`tiT`, `sys_evt`, `wifi`, `Tmr Svc`, `usb_host_events`, `USB HID Host`, `ups_poll`, `loopTask`) hanno stack uguali sulle due piattaforme e margini ampi. Applicando la regola del piano (massimo tra il minimo e uso + 1024, arrotondato a 512):
- `esp_timer`: max(4096, 1512) = **4096**, il valore della v1.8.0 (risparmio atteso 4 KB);
- IPC: arrotondato(768 + 1024) = **2048**, cioè nessuna modifica. La v1.8.0 usava 1024 con meno di 250 byte di margine: la regola lo esclude.

### 11.2 `custom_sdkconfig` (TASK-05, 2026-10-22)

- **Meccanismo.** `custom_sdkconfig = file://sdkconfig.esp32-nut` in `[env:esp32-s3-standard]`. Il file contiene solo le opzioni cambiate, con un commento per ciascuna. La piattaforma ricompila le librerie ESP-IDF del core Arduino (HybridCompile) e genera nella radice del progetto `sdkconfig.defaults`, `sdkconfig.esp32-s3-standard`, `.dummy/` e `managed_components/`, da mettere nel `.gitignore` quando lo si attiva (`custom-sdkconfig.md` §3). Copia del `sdkconfig` precompilato della 55.03.312-1 in `idf55-baseline/sdkconfig-precompiled-55.03.312-1.txt`.
- **Tempi su Windows** (portatile dell'utente, PowerShell, `PLATFORMIO_CACHE_DIR` corto): prima build dopo `clean` 16,2 minuti (ricompilazione di ESP-IDF, circa 13 minuti, più la build Arduino); build incrementale senza modifiche 152 s. Ogni modifica di `sdkconfig.esp32-nut` ripete la ricompilazione di ESP-IDF. La piattaforma avvisa che senza `LongPathsEnabled` la build è più lenta, ma funziona.
- **`sdkconfig` generato contro precompilato.** Oltre all'opzione del file (`CONFIG_ESP_TIMER_TASK_STACK_SIZE` 8192 → 4096, confermato dal bilancio del §11.1), la piattaforma cambia da sola:
  - PSRAM disattivata (`CONFIG_SPIRAM`, `CONFIG_ESP32S3_SPIRAM_SUPPORT`): la scheda `esp32-s3-devkitc-1` N8 non ha PSRAM;
  - flash allineata alla scheda: DIO e 8 MB invece di QIO e 16 MB del precompilato generico.

  Le librerie non usate (camera, Matter, TinyUSB in modalità device, riconoscimento vocale) non sono più compilate. Invariati: `USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT=y`, `USB_HOST_HUBS_SUPPORTED=y`, `USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y`, `USB_HOST_CONTROL_TRANSFER_MAX_SIZE=3000`, core dump in flash in formato ELF (stack del core dump 1792), `ESP_TASK_WDT_PANIC=y`, buffer Wi-Fi (8 RX statici, 32 RX dinamici, 8 TX statici). `__wrap_esp_core_dump_write` resta collegato (`firmware.map`).
- **Dimensioni.** RAM statica 54 304 byte (16,6%, prima 54 384), flash 1 963 465 byte (47,6%, prima 1 967 621).

### 11.3 Opzioni una per volta (TASK-06, 2026-10-23)

Una build per variante, flash via seriale sulla scheda dell'utente con l'Eaton 3S, heap dalle righe `[DIAG] Heap` nei primi 10 minuti dopo il reset (media dei minuti 3-10, più stabile dell'ultimo valore). Ogni variante aggiunge un'opzione alla precedente. Riferimento: `mem-idf55` del §11.1 (librerie precompilate).

| Variante | Opzioni rispetto al precompilato | Libero a riposo (media) | `min free` | Blocco più grande | Differenza | Esito |
|---|---|---|---|---|---|---|
| precompilato | nessuna | 149 175 | 132 912 | 102 388 | — | — |
| b0 | PSRAM spenta (decisa dalla piattaforma) | 159 426 | 140 792 | 116 724 | +10 251 | inevitabile con `custom_sdkconfig` |
| b1 | + `ESP_TIMER_TASK_STACK_SIZE=4096` | 163 600 | 145 064 | 118 772 | +4 174 | tenuta |
| b2 | + statistiche di FreeRTOS spente | 164 864 | 147 488 | 122 868 | +1 264 | tenuta |
| b3 | + `BT_ENABLED` spento | 164 415 | 144 148 | 118 772 | −449 (rumore) | scartata |
| b4 | + `USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` | 167 475 | 151 388 | 124 916 | +3 060 | tenuta |

- **IPC:** non provata: la regola del §11.1 lascia lo stack a 2048.
- **Bluetooth:** spegnerlo toglie 428 byte di RAM statica e 8 KB di flash, ma non cambia l'heap: il core Arduino rilascia già la memoria del controller quando il Bluetooth non è usato. Scartato per non allontanarsi dalla configurazione del core senza un guadagno.
- **Statistiche di FreeRTOS:** la build collega senza errori, quindi né il core né le librerie le usano. Erano spente anche nella v1.8.0.
- **Trasferimenti di controllo USB a 256:** l'Eaton 3S si enumera all'avvio e serve i dati come prima (prima lettura completa in 1,7 s).
- **PSRAM:** non è un'opzione del file. La piattaforma la spegne da sola quando ricompila per una scheda senza PSRAM, ed è il guadagno più grande.
- **Totale a riposo:** circa 18 KB rispetto alle librerie precompilate (da 149 a 167 KB circa), metà della differenza con la v1.8.0 (188 736, §11.1).

Note pratiche sulla compilazione ibrida:
- togliere un'opzione dal file non la riporta al valore precompilato: il `sdkconfig.esp32-s3-standard` generato la conserva, va cancellato;
- la piattaforma decide se ricompilare ESP-IDF da contenuto e data di modifica del file: copiando un file preparato prima, la data resta quella vecchia e la ricompilazione non parte;
- dopo la compilazione ibrida `tool-esptoolpy` è la 5.4, che stampa la barra di avanzamento in Unicode: con l'uscita rediretta su file serve `PYTHONIOENCODING=utf-8`.

### 11.4 Export a pezzi e test di carico (TASK-04, TASK-07)

- **Export a pezzi.** `USBHostUPS::buildUSBDiagnostics()` riempie il `JsonDocument` sotto `_mutex`; il web server lo serializza con `ChunkWriter` (`lib/WebApi/src/ChunkWriter.h`, blocchi da 1024 byte) e `sendContent()`, dopo `setContentLength(CONTENT_LENGTH_UNKNOWN)`. Non si usa `chunkWrite()` del core 3.3 perché scrive il framing chunked anche ai client HTTP/1.0. La suite `test/test_chunk_writer` verifica che l'uscita sia identica byte per byte a quella di `serializeJsonPretty` in un'unica stringa. Sulla scheda: HTTP/1.1 con `Transfer-Encoding: chunked`, HTTP/1.0 grezzo, stesso file di 12 180 byte.
- **Build finale `idf55-v2`:** export a pezzi più `sdkconfig.esp32-nut` (stack di `esp_timer` a 4096, statistiche di FreeRTOS spente, trasferimenti di controllo USB a 256; PSRAM spenta dalla piattaforma).

| | v1.8.0 (`mem-v180`) | 5.5 precompilata (`mem-idf55`) | `idf55-v2` | `idf55-v2` senza export |
|---|---|---|---|---|
| Libero a riposo dopo 10 minuti | 188 736 | 150 516 | 167 484 | — |
| Blocco più grande a riposo | 176 116 | 102 388 | 122 868 | — |
| Libero più basso sotto carico | 154 224 | 113 464 | 124 040 | 122 460 |
| `min free` sotto carico | 112 884 | 74 688 | **80 660** | **105 000** |
| Blocco più grande, minimo sotto carico | 114 676 | 61 428 | 71 668 | 71 668 |
| Errori NUT / web | 7 / 1 (Wi-Fi caduto) | 0 / 0 | 0 / 0 | 7 / 2 (Wi-Fi caduto) |

Report in `idf55-baseline/stresstest-idf55-v2.txt` e `stresstest-idf55-v2-nodump.txt`. In entrambi i test il nono client è rifiutato.

- **L'export costa circa 25 KB di picco** anche a pezzi: il `JsonDocument` resta intero in memoria durante la serializzazione. Senza export, 8 client NUT e la Web UI lasciano più di 100 KB liberi.
- **Criterio rivisto (2026-10-23, decisione dell'utente):** al posto dei 90 KB, almeno 75 KB con l'export ogni 60 s e almeno 100 KB senza export. Togliere il `JsonDocument` dall'export è la US-096, a bassa priorità finché non arrivano segnalazioni di export troncati o riavvii durante l'export.
- **Dopo 1 h** (con in mezzo il test di carico senza export): libero 164 976, blocco più grande 100 340, stabile dal minuto 30 (165 016). Nessuna perdita.
- **Cadute del Wi-Fi.** In due prove su quattro (`mem-v180` e `idf55-v2` senza export) il Wi-Fi è caduto una volta, circa 110 s dopo l'inizio del test, con riconnessione automatica in 15 s e un errore per client. Succede su entrambe le piattaforme, non è legato alla memoria; la coincidenza dei tempi fa pensare al router.

### 11.5 Regressione (TASK-08)

- **NUT:** `upsc`, `upscmd -l` e `upsrw` letti con un client Python equivalente e confrontati con `idf55-baseline/*-v1.8.0.txt`: `upscmd -l` e `upsrw` identici; in `upsc` cambia solo `ups.type: EatonDriver` in `driver.name: EatonDriver` (merge di #43, main `906acd7`).
- **Automatici:** suite nativa 585/585; e2e 101/101 (un test di `ups-settings.spec.ts` fallito una volta in parallelo e passato rilanciato da solo, insieme a tutto il file).
- **USB:** con i trasferimenti di controllo a 256 l'Eaton 3S si enumera all'avvio e dopo uno stacca e riattacca del cavo (dati di nuovo serviti in 1,7 s).
- **Rete di sicurezza:** due build temporanee, non committate. `abort()` nel loopTask dopo 90 s: `Reset reason: PANIC`, riepilogo del core dump (task `loopTask`, PC, backtrace di 6 indirizzi). LoopTask occupato senza `esp_task_wdt_reset()`: panic del Task WDT dopo 43 s (il controllo avviene una volta per periodo di 30 s), `Reset reason: TASK_WDT` e riepilogo. L'intercettazione `-Wl,--wrap=esp_core_dump_write` funziona anche con le librerie ricompilate.
- **Stack del loopTask:** 13 192 byte liberi dopo 1 h: `-DARDUINO_LOOP_STACK_SIZE=16384` vale anche con la compilazione ibrida.

### 11.6 Tempi di build e CI

- **Sul portatile dell'utente** (Windows, PowerShell): prima build con ricompilazione di ESP-IDF 16-21 minuti (21 quando la piattaforma reinstalla anche il framework); build incrementale 2,5 minuti; ogni modifica di `sdkconfig.esp32-nut` 16-19 minuti. Il passaggio a un branch senza `custom_sdkconfig` (per esempio `main` prima del merge) reinstalla il framework precompilato, e il ritorno ricompila tutto.
- **CI (`release.yml`, Ubuntu):** la compilazione ibrida avviene a ogni esecuzione, perché il runner parte senza cache. Stima: almeno 10-15 minuti in più per release, più il download di `framework-espidf`. Non misurata: **non** si lancia `release.yml` con `workflow_dispatch` per provarla, perché pubblica una release e aggiorna il web installer su GitHub Pages. La prima release con la 5.5 darà il tempo reale; una cache di `~/.platformio` è un possibile seguito.

### 11.7 Conclusione

Il costo fisso della 5.5 a riposo scende da circa 38 KB a circa 21 KB rispetto alla v1.8.0 (167 484 contro 188 736 libero dopo 10 minuti), con un blocco libero più grande di 20 KB. Sotto carico il margine sale da 74 688 a 80 660 con l'export ogni 60 s e arriva a 105 000 senza export. Il resto della differenza viene dal Wi-Fi della 5.5 (circa 12 KB all'avvio) e dalla memoria statica delle librerie (circa 10 KB), che non dipendono dalle opzioni valutate.

### 11.8 Decisione: `custom_sdkconfig` non adottato (2026-10-23)

Dopo le misure, l'utente ha scelto di non adottare `custom_sdkconfig` per ora: il guadagno sotto carico (+6 KB) non giustifica la ricompilazione di ESP-IDF alla prima build, a ogni modifica delle opzioni e in ogni release. Il guadagno a riposo (+17 KB) e sul blocco libero più grande (+20 KB) resta disponibile se servirà.

- `platformio.ini` torna alle librerie ESP-IDF precompilate; l'export della diagnostica a pezzi (§11.4) resta.
- Il file delle opzioni è archiviato in `docs/plans/custom-sdkconfig/sdkconfig.esp32-nut`; procedura, verifiche, trappole e tempi in `docs/plans/custom-sdkconfig.md`. Gli strumenti di misura sono in `scripts/` (`serial_log.py`, `mem_probe_patch.py`, `nut_snapshot.py`, oltre a `load_test.py`).
- Il punto debole sotto carico, l'export della diagnostica (circa 25 KB di picco), passa alla US-096 (priorità MEDIUM), che non ha costi di build.
- Le misure con le librerie precompilate restano quelle di `mem-idf55` (§11.1): a riposo 150 516 libero e 102 388 di blocco più grande; sotto carico `min free` 74 688 con il vecchio export in un'unica `String`.
