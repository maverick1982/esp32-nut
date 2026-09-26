---
type: plan
title: "Review del layer USB ESP32 ↔ UPS — Analisi e Piano di Hardening"
description: "Bug, anti-pattern e smell del layer USB (hid_host.c, USBHostUPS, HIDParser, driver) e piano per una connessione stabile con tutti i modelli di UPS."
tags: [plan, review, usb, hid, stability, nut]
---
# Review del layer USB ESP32 ↔ UPS

## 1. Contesto

Review fatta dopo il fix v3 dell'issue #47 (ADR 0008). L'obiettivo è una connessione USB stabile e "a prova di bomba" con tutti i modelli di UPS supportati.

**Codice analizzato:**
- `lib/usb_host_hid/hid_host.c` (componente vendorizzato)
- `lib/USBHostUPS/src/USBHostUPS.{h,cpp}`, `LinkMonitor.h`
- `HIDParser`, `BeeperLogic`
- driver Generic, APC, CyberPower, Eaton, Powercom, OpenUPS

**Verifiche sulle fixture reali** (`test/fixtures`, 12 dispositivi):
- il report che contiene il beeper non contiene mai altre usage;
- l'unico INPUT report più lungo di 8 byte è il report 137 (64 byte) dell'APC Smart-UPS 750 pid 0003.

I riferimenti di riga si riferiscono al commit `60b6448`.

## 2. In sintesi

- **Critici:** 4 difetti possono spegnere il carico o far crashare la scheda. Sono fix piccoli e locali, da fare prima di distribuire la v3.
- **Alti:** 7 problemi toccano la stabilità del link e la semantica verso i client NUT, in particolare i dati serviti come validi a device scollegato.
- **Architettura:** polling e server NUT/web condividono il loopTask, quindi ogni transfer lento blocca tutto il firmware.

## 3. Analisi

### 3.1 Critici: possono spegnere il carico, crashare o bloccare la scheda

**C1. `setBeeper` può spegnere le prese dell'UPS** (`USBHostUPS.cpp:516-525`)
- Se il GET_REPORT fallisce (quasi sempre per i report OUTPUT), il fallback fa SET_REPORT dell'intero report con tutti gli altri campi a zero.
- In HID PDC `DelayBeforeShutdown = 0` equivale al comando NUT `load.off`: spegnimento immediato delle prese.
- Nelle fixture note il beeper ha un report dedicato, quindi oggi il rischio è latente. Su un modello sconosciuto che condivide il report con i campi di delay, il danno è enorme.

**C2. `hid_host_device_init_attempt` passa un handle non inizializzato** (`hid_host.c:509, 526`)
- Se `usb_host_device_open` fallisce, cosa probabile durante i reset USB dell'UPS nei cambi di modalità, viene chiamato `usb_host_device_close(dev_hdl)` con `dev_hdl` mai inizializzato.
- Due `ESP_ERROR_CHECK` (`:522, :524`) fanno `abort()` nel task HID se l'enumerazione fallisce (ENOMEM o errore sull'interfaccia).

**C3. Il report descriptor può contenere spazzatura** (`hid_host.c:1178, 1952`)
- `usb_class_request_get_descriptor` copia solo i byte ricevuti, ma `hid_host_get_report_descriptor` restituisce sempre `report_desc_size`, cioè la lunghezza dichiarata nell'HID descriptor.
- Se il device risponde con meno byte (quirk noto su alcuni UPS), il parser legge la coda non inizializzata del `malloc`. Il risultato sono usage fantasma e offset sbagliati, non deterministici.

**C4. Le stringhe del device possono causare un overread** (`USBHostUPS.cpp:658-664`)
- Su un carattere che non converte (per esempio ≥ 0x100, tipico delle stringhe "invertite" CyberPower), `wcstombs` restituisce -1 e non termina il buffer.
- `buf` è sullo stack e non inizializzato, quindi `String(buf)` legge oltre la fine.
- `QUIRK_INVERT_STRINGS` non viene mai applicato su questo percorso.

### 3.2 Alti: stabilità del link e correttezza verso NUT

**A1. Con il device disconnesso NUT serve dati "validi"** (`USBHostUPS.cpp:358, 425`)
- Al disconnect `_ups_data` viene azzerato, `isDataStale()` restituisce false e NUT risponde `ups.status "Unknown"`.
- Upstream (`usbhid-ups` con `dstate_datastale()`, poi `upsd`) risponde `ERR DATA-STALE`. `upsmon` lo interpreta come comunicazione persa e, se l'UPS era OB, la tratta come critica.
- Scenario pericoloso: blackout, l'UPS resetta l'USB passando a batteria, e durante la riconnessione il client non vede mai OB.

**A2. Nessun controllo di vita del pipe INPUT**
- Il `LinkMonitor` guarda solo EP0. Se gli INPUT report smettono di arrivare senza errore, i dati restano congelati senza mai diventare stale.
- Il caso peggiore sono i modelli con `QUIRK_NO_GET_REPORT` (APC 5G, Smart-UPS): per loro gli INPUT sono l'unica fonte di dati.
- CyberPower è colpito a metà: i flag di stato arrivano dagli INPUT, gli altri valori dal polling.

**A3. Il recovery dopo uno STALL sull'endpoint IN non sblocca il device** (`USBHostUPS.cpp:236`)
- `hid_host_device_stop/start` resetta solo lo stato lato host. Un endpoint in STALL lato device richiede una `CLEAR_FEATURE(ENDPOINT_HALT)` su EP0.
- Oggi si arriva al restart dopo 4 errori, e il device si sblocca solo perché la nuova enumerazione lo resetta.

**A4. INPUT report più lunghi di un pacchetto vengono spezzati** (`hid_host.c:808, 1908`)
- L'IN transfer è dimensionato su `ep_in_mps`. Un report più lungo di wMaxPacketSize viene diviso in più transfer, e i frammenti successivi vengono decodificati come report distinti (il primo byte scambiato per report ID).
- Sui device low-speed (MPS 8) basta un report di 9 byte.

**A5. Il polling blocca tutto il firmware**
- Driver, NUT server e web server girano tutti nel loopTask. Ogni GET_REPORT può bloccare fino a 5 s (`DEFAULT_TIMEOUT_MS`, `hid_host.c:75`), e intanto NUT e la web UI non rispondono.
- Un UPS sano risponde in pochi millisecondi.

**A6. `GenericDriver` fa un full poll ogni 2 s** (`GenericDriver.cpp:99`)
- Interroga tutti i report ID ogni 2 s. L'ADR 0006 e NUT prevedono un quick poll ogni 2 s e il full poll ogni 30 s, e CyberPower è già stato portato a 30 s per non soffocare il firmware dell'UPS.
- Il traffico continuo su EP0 è la causa principale di STALL e timeout sui firmware più fragili.

**A7. Restart senza protezione dai boot loop**
- Con un guasto persistente, la scheda si riavvia ogni ~3 minuti per sempre, senza backoff tra un restart e l'altro.

### 3.3 Medi

- **M1. Parser HID incompleto** (`HIDParser.cpp`):
  - `Logical Min/Max` sono ignorati (`logical_min` sempre 0, `:68`) e manca l'estensione del segno (`:199`), quindi i campi con segno escono sbagliati (per esempio le correnti in scarica);
  - `bit_size ≥ 64` è UB (`:197`), e `desc[i] << 24` con un byte ≥ 0x80 è overflow di un int con segno (`:40`);
  - mancano i long item e `Usage Minimum/Maximum`.
- **M2. Membri non inizializzati:** il costruttore di `GenericDriver` non inizializza `_batteryDateStringIndex` e `_rids_cached`. `PowercomDriver::setup()` (`:28`) non chiama `GenericDriver::setup()` e ridichiara `_poll_step`, `_last_fast_poll` ecc. nascondendo quelli della base.
- **M3. Leak in `hid_host.c`:** se l'allocazione del transfer fallisce dopo il claim (`:808`), l'interfaccia resta claimed. Dopo il free, `in_xfer` non viene messo a NULL (`:836`), lasciando un puntatore pendente.
- **M4. Politica di polling incoerente:** `GenericDriver` esclude i report con `report_id == 0` (`:79`), quindi gli UPS senza report ID non vengono mai interrogati via GET_REPORT, mentre Eaton lo fa. Anche la scelta "INPUT via GET_REPORT sì o no" cambia da driver a driver.
- **M5. Codice morto sulle stringhe:** `requestStringDescriptor()` restituisce sempre false (`:465`) e `_iManufacturer`/`_iProduct`/`_iSerialNumber` non vengono mai valorizzati. I passi "string descriptor" dei driver sono inutili, e `battery.type` (Eaton) e `battery.mfr.date` da stringa non arrivano mai.
- **M6. Chiave vuota pubblicata:** CyberPower scrive `input.voltage.nominal = ""` (`:127`) invece di rimuovere la chiave, e NUT la pubblica vuota.

### 3.4 Smell e manutenibilità

- **S1. Log flooding:** un log INFO per ogni INPUT report (`USBHostUPS.cpp:327`) e `Serial.printf` diretti nel decode di Powercom (`:119, :144`). Riempiono in pochi minuti il buffer da 50 righe della web UI e generano churn di `String` nell'heap.
- **S2. Decodifica costosa:** ogni report fa usage × mapping `strcmp`, ripetuto dal driver base e da quello derivato. Il match `path → mapping` andrebbe calcolato una volta al claim.
- **S3. Duplicazione:** la macchina a stati del polling e la costruzione della lista dei report ID sono copiate in Generic, CyberPower ed Eaton, contro l'ADR 0004. La scelta del driver in base al VID è un `if/else` cablato in `claimInterface`.
- **S4. Nomi e visibilità:** `_hid_parser` è un membro pubblico. `isControlPending()` ora significa "backoff attivo".
- **S5. Commento lasciato a metà:** `CyberPowerDriver.cpp:21` contiene ancora "Wait, CyberPowerDriver::setup had _slow_poll_counter = 14!".
- **S6. Diagnostica senza heap:** manca il monitoraggio dell'heap minimo (`esp_get_minimum_free_heap_size`), che serve a scoprire la frammentazione nei soak test di più giorni.

## 4. Piano di implementazione

### Fase 1: fix critici (bloccanti per la distribuzione della v3)

| # | Intervento | File | Test | Stato |
|---|---|---|---|---|
| C1 | Fare il SET_REPORT del beeper solo se il GET è riuscito, oppure se il report (stesso id e tipo) non contiene altre usage oltre al beeper; altrimenti rifiutare e loggare. Estrarre il controllo in `BeeperLogic` per renderlo testabile. | `USBHostUPS.cpp`, `BeeperLogic.h` | `test_beeper_logic`: report condiviso con `DelayBeforeShutdown` → SET rifiutato; report dedicato → SET consentito | ✅ codice + test nativi |
| C2 | `dev_hdl = NULL`, close solo se l'open è riuscita, sostituire gli `ESP_ERROR_CHECK` con gestione dell'errore e cleanup | `hid_host.c` | build + prova di stacca/riattacca a raffica | ✅ codice + build; ⏳ prova su hardware |
| C3 | Salvare la lunghezza effettivamente ricevuta del report descriptor e restituirla | `hid_host.c` | build + confronto del descriptor dump prima/dopo sulle UPS disponibili | ✅ codice + build; ⏳ confronto dei dump |
| C4 | Conversione UTF-16 → ASCII fatta a mano, con terminazione garantita e applicazione di `QUIRK_INVERT_STRINGS` | `USBHostUPS.cpp` | test nativo della funzione di conversione (stringhe normali, invertite, non-ASCII) | ✅ codice + test nativi |
| A1 | `isDataStale()` true anche a device non connesso, con una grazia configurabile al boot (per esempio 15 s) | `USBHostUPS.cpp`, `NUTServer.cpp` | `test_nut_server`: host disconnesso → `ERR DATA-STALE` | ✅ codice + test nativi |

Deviazioni registrate nell'addendum dell'ADR 0008.

#### Stato di avanzamento della Fase 1 (2026-09-24)

Implementazione completata, non ancora committata. Verifiche eseguite:
- `pio test -e native`: 93/93 test superati (12 nuovi);
- `pio run -e esp32-s3-standard`: build OK (RAM 14,0%, flash 37,9%).

**Cosa è stato fatto:**
- **C1.** `BeeperLogic::reportHasOtherFields()` e `canWriteBack()`. `setBeeper()` rifiuta la scrittura, con un log WARN, se il report è condiviso e il GET è fallito o ha restituito meno byte di quelli che servono per arrivare al campo beeper (anche i byte mancanti andrebbero scritti a zero). Il parser registra le usage OUTPUT con tipo FEATURE, quindi i report OUTPUT e FEATURE con lo stesso id contano come un unico report. È la scelta prudente: nel dubbio il report è trattato come condiviso. Test: `test_beeper_shared_report_refused_without_read_back`, `test_beeper_dedicated_report_allowed_without_read_back`.
- **C2.** In `hid_host_device_init_attempt()`, se l'open fallisce viene solo loggato e la funzione ritorna; gli errori di `hid_host_install_device` e `hid_host_interface_list_create` vengono loggati e ripuliti invece di andare in `abort()`. Emerso durante il lavoro: anche il percorso `fail:` di `hid_host_install_device()` chiamava `hid_host_uninstall_device()` su un device non ancora in lista, quindi `STAILQ_REMOVE` su un elemento assente (crash) e chiusura di `dev_hdl`. Ora libera solo le proprie allocazioni.
- **C3.** Nuovo campo `report_desc_len` con i byte ricevuti; `usb_class_request_get_descriptor()` restituisce la lunghezza copiata. Una risposta vuota è un errore, una corta produce un log WARN.
- **C4.** Nuovo `DeviceStrings.h` (logica pura) che sostituisce `wcstombs`: terminazione sempre garantita, caratteri non ASCII → `?`, caratteri di controllo scartati, spazi finali rimossi, stringhe invertite rilevate dal primo carattere (stessa regola di `GenericDriver::parseStringDescriptor`) o forzate da `QUIRK_INVERT_STRINGS`. Aggiunta anche la protezione in `hid_host_string_descriptor_copy()` contro `bLength < 2`, che rendeva negativa la lunghezza. Test: `test_device_strings` (9 casi).
- **A1.** `LinkMonitor::isStaleWithoutDevice()`: senza device i dati sono stale, tranne nei primi `USBUPS_NO_DEVICE_BOOT_GRACE_MS` (15 s, sovrascrivibile da `build_flags`) dopo il boot se nessun UPS è ancora stato visto. Dopo un disconnect sono stale subito, anche dentro la grazia. `NUTServer.cpp` non ha richiesto modifiche, perché controllava già `isDataStale()` su `LIST VAR` e `GET VAR`. `/api/system-status` segnala `stale` solo con il device connesso, così la web UI mostra "Disconnected" senza il banner "UPS is not answering". Test: `test_stale_without_device`, `test_disconnected_is_stale`.

**Cosa resta prima di chiudere la Fase 1:**
- prova su hardware di stacca/riattacca a raffica (C2). In parte coperta dalle 11 disconnessioni reali dei due CyberPower di x-magic (2026-09-26), tutte riconnesse senza errori; manca la prova "a raffica" (Fase 4);
- confronto del descriptor dump prima e dopo sulle UPS disponibili (C3), controllando nel log l'eventuale `Short report descriptor`. Nessun `Short report descriptor` nei log di Eaton e CyberPower;
- ~~verifica di `ups.mfr`, `ups.model` e `ups.serial` sul CyberPower con stringhe invertite (C4)~~ fatto: `CPS`, `CP1300EPFCLCD` e `CP1500EPFCLCD` corretti nei dump del 2026-09-26;
- ~~commit~~ fatto (`da1f354`).

### Fase 2: robustezza del link

| # | Intervento | Test | Stato |
|---|---|---|---|
| A2 | Watchdog sugli INPUT nel `LinkMonitor`: imparare l'intervallo tipico tra INPUT report (mediana degli ultimi N); se il device risulta periodico, dopo un silenzio pari a k volte l'intervallo segnare stale e poi fare recovery. Nessun effetto sui device che inviano solo sui cambi. | `test_link_monitor`: periodico → silenzio → stale/recover; aperiodico → nessuna azione | ✅ codice + test nativi |
| A3 | Nel recovery dopo un errore IN: `CLEAR_FEATURE(ENDPOINT_HALT)` sull'endpoint IN via EP0 (nuova API in `hid_host.c`), poi `stop/start` | prova su hardware; log dell'esito | ✅ codice + build; ⏳ prova su hardware |
| A7 | Contatore dei restart in RTC con backoff crescente (1, 5, 15 min); oltre la soglia, modalità degradata: niente riavvii, dati stale, banner nella web UI | test nativo della policy (estratta in logica pura) | ✅ codice + test nativi |
| A5a | `DEFAULT_TIMEOUT_MS` dei control transfer da 5000 a 1500 ms (valore da confermare con un soak test su APC ed Eaton) | soak test | ✅ codice + build; ⏳ soak test |
| A4 | ~~Dimensionare `in_xfer` sul massimo INPUT report del parser, arrotondato a un multiplo di MPS (limite 512 B)~~ Riassemblaggio dei report multi-pacchetto lato applicazione (vedi sotto) | test del calcolo; prova sull'APC Smart-UPS 750 pid 0003 | ✅ codice + test nativi; ⏳ prova sull'APC |

#### Stato di avanzamento della Fase 2 (2026-09-24)

Implementazione completata, non ancora committata. Verifiche eseguite:
- `pio test -e native`: 116/116 test superati (23 nuovi);
- `pio run -e esp32-s3-standard`: build OK (RAM 14,2%, flash 38,0%);
- prova su hardware con un Eaton S3 (2026-09-24): funzionamento regolare. È l'unico UPS disponibile: le prove sugli altri marchi, elencate sotto, restano a carico dei tester.

**Cosa è stato fatto:**
- **A2.** Nuova classe `InputWatchdog` in `LinkMonitor.h`:
  - i report a meno di 500 ms l'uno dall'altro formano un burst (il CyberPower invia gli id 8 e 11 insieme), e si registrano gli intervalli tra un burst e l'altro;
  - il device è considerato periodico quando gli ultimi 8 intervalli stanno entro ±25% dalla mediana;
  - dopo un silenzio di 5 × periodo (minimo 30 s) i dati diventano stale e parte il recovery dell'interfaccia; dopo 60 s un secondo recovery, dopo altri 60 s il restart. È la stessa scala del `LinkMonitor`;
  - il silenzio rilevato non viene imparato come periodo, e un device nuovo riparte da zero;
  - il timestamp è quello di ricezione nel task HID (nuovo campo `HidEvent::ts`), così i GET_REPORT bloccanti del loopTask non falsano gli intervalli;
  - in `/api/usb/dump` sono stati aggiunti `input_period_ms` e `input_recoveries`.
- **A3.** Nuova API `hid_host_device_clear_ep_in_halt()`. Il recovery dopo un errore IN, o dopo un silenzio rilevato da A2, fa `stop` (reset lato host), poi `CLEAR_FEATURE(ENDPOINT_HALT)` e infine `start`. L'ordine è quello del driver MSC di Espressif: prima si svuota il pipe e poi si sblocca il device. L'esito passa da `noteControlResult()`.
  - **Rischio da verificare su hardware:** nei sorgenti IDF (`_pipe_cmd_clear` in `hcd_dwc.c`) il clear lato host non azzera il data toggle, mentre il device lo riporta a DATA0. Il primo pacchetto dopo il clear potrebbe quindi essere scartato o dare un errore di transazione. Nei log va controllato che dopo `IN endpoint halt cleared` gli INPUT riprendano senza un nuovo `INPUT transfer error`.
- **A7.** Nuova classe `RestartPolicy` (logica pura) e contatore dei restart consecutivi in RTC (`CrashDiag`), azzerato al power-on, al brownout e dopo 10 minuti di dati sani.
  - Il primo restart è immediato, perché la scala di recovery ha già impiegato minuti; i successivi attendono 1, 5 e 15 minuti. Dal quinto in poi la scheda entra in modalità degradata: niente più riavvii, dati stale, banner nella web UI.
  - Una nuova enumerazione dell'UPS annulla la richiesta di restart e l'attesa in corso.
  - Durante l'attesa e in modalità degradata `isDataStale()` è true, perché nulla aggiorna più i dati (prima, per i device `QUIRK_NO_GET_REPORT`, il restart per errori IN non rendeva i dati stale).
  - In `/api/system-status` sono stati aggiunti `consecutive_restarts` e `degraded`.
- **A5a.** Nuovo `USBUPS_CTRL_TIMEOUT_MS` (1500 ms, sovrascrivibile da `build_flags`), applicato solo ai GET/SET class request del polling. La richiesta del report descriptor all'enumerazione e le attese sui lock restano a `DEFAULT_TIMEOUT_MS` (5 s).
- **A4, cambio di approccio rispetto al piano.** Nei sorgenti IDF (`_buffer_fill_intr`, `usbh.c`) un interrupt IN multi-pacchetto termina solo con un pacchetto corto o a buffer pieno. Con `in_xfer` dimensionato sul report più lungo, un report più corto con lunghezza multipla di MPS (per esempio 8 byte su MPS 8) resterebbe in attesa e verrebbe incollato al report successivo. Per questo `in_xfer` resta di un pacchetto e il report viene ricomposto nel loopTask:
  - nuova classe `InputReassembler` (logica pura): un pacchetto pieno che inizia un report dichiarato più lungo di MPS apre una ricomposizione, che si chiude alla lunghezza attesa o a un pacchetto corto, e scarta il frammento dopo 500 ms senza seguito;
  - nuove API di supporto `hid_host_device_get_ep_in_mps()`, `HIDParser::getInputLength()` e `HIDParser::usesReportIds()`;
  - in `/api/usb/dump` sono stati aggiunti `incomplete_input_reports` ed `ep_in_mps`.

**Cosa resta prima di chiudere la Fase 2:**
- prova su hardware di A3, in particolare del data toggle dopo il clear. Non ancora osservata: nessun errore IN nei log di Eaton e CyberPower, quindi il percorso non è mai scattato;
- soak test di A5a su APC ed Eaton, cercando `requestReport FAILED ... ESP_ERR_TIMEOUT` che con 5 s non comparivano. Sui CyberPower (2026-09-26) e sull'Eaton 3S: 0 richieste fallite; manca l'APC;
- prova di A4 sull'APC Smart-UPS 750 pid 0003 (report 137 da 64 byte), controllando `ep_in_mps` e il report ricomposto nel dump. I CyberPower hanno `ep_in_mps` 64 e 0 report incompleti, ma i loro report non superano un pacchetto;
- verifica di A2: ~~CyberPower (`input_period_ms` ≈ 3000)~~ fatto, 3000 ms su entrambi; manca un APC che invia solo sui cambi (`input_period_ms` = 0);
- ~~commit~~ fatto (`6de1b99`).

### Fase 3: architettura e qualità dei dati

| # | Intervento | Stato |
|---|---|---|
| A5b | Task USB dedicato (priorità sopra il loopTask, sotto WiFi) che esegue `processEvents`, polling, decodifica e recovery. Il loopTask resta per NUT e web, ed entrambi leggono `UPSData` sotto `_mutex`. Aggiungere il nuovo task al Task WDT. Aggiornare l'ADR 0008 (modello di threading). | ✅ codice + build; ⏳ prova su hardware |
| A6 | `GenericDriver`: quick poll ogni 2 s dei soli report con usage di stato (`PresentStatus`, `RemainingCapacity`, `RunTimeToEmpty`, `PercentLoad`), full poll ogni 30 s, come upstream `usbhid-ups` (ADR 0006) | ✅ codice + test nativi |
| S3 / M2 / M4 | Unificare la macchina a stati del polling in `GenericDriver`, con hook per le politiche dei driver (report ID esclusi, INPUT via GET_REPORT sì o no, intervalli). Powercom senza membri nascosti e con chiamata a `GenericDriver::setup()`. Registro dei driver per VID/PID. | ✅ codice + test nativi |
| M1 | Parser HID: Logical Min/Max con estensione del segno, `bit_size` limitato a 32, long item, Usage Min/Max. Test in `test_hid_parser` e `test_record_replay` su tutte le fixture. | ✅ codice + test nativi |
| S2 | Precalcolo della tabella `usage → mapping` al claim, con decodifica O(usage del report) | ✅ codice + test nativi (Powercom escluso, vedi sotto) |
| M5 | Implementare `requestStringDescriptor` (GET_DESCRIPTOR string su EP0) oppure rimuovere i passi morti dai driver | ✅ implementato; ⏳ prova su hardware |
| M3 / M6 / S4 / S5 | Pulizia: leak del claim, `in_xfer = NULL`, rimozione della chiave vuota, incapsulamento di `_hid_parser`, rinominare `isControlPending` → `isPollingPaused`, rimuovere il commento | ✅ |
| S1 / S6 | INPUT report a livello DEBUG o riassunti ogni 60 s; niente `Serial.printf` nei driver; heap libero minimo nel log `[DIAG]` e in `/api/system-status` | ✅ |

#### Stato di avanzamento della Fase 3 (2026-09-24)

Implementazione completata, non ancora committata. Verifiche eseguite:
- `pio test -e native`: 130/130 test superati (14 nuovi); tutte le fixture di `test_record_replay` restano invariate;
- `pio run -e esp32-s3-standard`: build OK (RAM 14,2%, flash 38,4%);
- prova su hardware con un Eaton 3S (2026-09-24):
  - `battery.type` compare (M5);
  - 244 GET_REPORT al minuto, 0 falliti. Una simulazione con il descriptor della fixture `eaton_3s` e lo stesso `EatonDriver` produce esattamente 244 richieste al minuto: 20 report nel full poll, 11 nel quick poll;
  - stack libero del task `ups_poll`: 5384 byte su 8192;
  - heap stabile: libero circa 201,9 KB, minimo 181,5 KB, blocco più grande 188,4 KB;
  - 0 INPUT report con UPS in OL stabile: l'Eaton 3S non invia report periodici sull'interrupt, quindi il watchdog di A2 resta inattivo;
  - toggle del beeper corretto;
  - togliendo la rete lo stato diventava `OL OB`. **Difetto preesistente** (da `b3cb3c1`, non introdotto da questa review): `computeUPSStatusString()` aggiungeva `OL` quando `ups.status.good = 1`, ma l'Eaton tiene `PresentStatus.Good` a 1 anche in batteria (NUT `mge-hid.c` lo usa solo per `OFF`). Corretto come in `usbhid-ups`: `OL` viene da ACPresent e `OB` significa "non online" (anche con ACPresent a 0 e Discharging non ancora aggiornato); `Good` sostituisce ACPresent solo nei device che non ce l'hanno, e mai durante la scarica. Nuovi test in `test_ups_status` (135/135 in totale). Da riverificare sull'Eaton: in batteria deve comparire `OB`;
  - 0 INPUT report anche togliendo e ridando la rete. Non è la ricomposizione di A4: gli INPUT report dell'Eaton 3S sono di 3-6 byte, sotto un pacchetto, e nel log non compaiono né report scartati né errori di transfer. Non sembra neppure una regressione: il dump live della fixture `eaton_3s` (2026-08-28, firmware precedente alla review) ha 28 report in cache, tutti FEATURE, nessun INPUT. Ipotesi più probabile: il 3S non invia sull'interrupt, o lo fa solo in condizioni particolari; i cambi di stato arrivano comunque entro circa 2 s dal quick poll. Due aggiunte per chiarirlo: l'esito di `hid_host_device_start()` al claim ora viene controllato (prima un avvio fallito della pipe INPUT passava senza log, ora c'è un WARN e un nuovo tentativo), e il riepilogo `[USB] Last 60 s` conta anche i pacchetti grezzi (`N INPUT reports (M packets)`), mentre il log di claim riporta la MPS dell'endpoint IN;
  - seconda prova (correzioni applicate): `OL` → `OB` → `OL CHRG` corretti; pipe INPUT avviata (MPS 8); al ritorno della rete è arrivato 1 INPUT report (1 pacchetto). Quindi l'interrupt funziona: il 3S lo usa solo raramente e non nel passaggio a batteria, e i cambi di stato arrivano dal quick poll;
  - `battery.type` compariva con circa 30 s di ritardo: l'indice della stringa (`iDeviceChemistry`) si scopre solo decodificando i report del primo full poll, mentre le stringhe venivano raccolte all'inizio del ciclo. Ora a fine full poll le stringhe vengono ricontrollate e quelle appena note si accodano allo stesso ciclo (vale anche per `battery.mfr.date`). Nuovo test `test_string_index_found_in_reports_requested_same_cycle` (136/136). Verificato sull'Eaton 3S: `battery.type` arriva pochi secondi dopo gli altri valori;
  - heap: `largest block` da 192,5 KB a 184,3 KB in 4 minuti di avvio; da seguire nel soak test.

**Cosa è stato fatto:**
- **A5b.**Il servizio USB (eventi, polling, decodifica, recovery) gira nel task `ups_poll`: priorità 3 (sopra il loopTask, sotto il task HID a 5 e il WiFi), stack da 8 KB, periodo di 10 ms, sotto il Task WDT. `main.cpp` non chiama più `usb_ups.loop()`.
  - `setBeeper()`, chiamato da NUT o dalla web UI, attende il passo di polling in corso tramite `_op_mutex` (al massimo 3 s) invece di sovrapporsi. L'ordine dei lock è `_op_mutex` → `_mutex`.
  - Emerso durante il lavoro: `beeper.toggle` in `NUTServer.cpp` chiamava `setBeeper()` con il lock dei dati ancora preso (temporaneo di `getUPSData()` nella stessa espressione). Con il nuovo task sarebbe stato uno stallo di 3 s; ora legge lo stato prima.
  - `AppLogger` è protetto da un mutex, perché ora scrivono due task.
  - Le scritture dei driver su `UPSData` fuori dalla decodifica (`ups.type`, i default Powercom) avvengono sotto il lock dell'host.
  - Nuovo log `[DIAG] UPS Poll Task Stack High Water Mark`.
- **A6 / S3 / M2 / M4.** Una sola macchina a stati in `GenericDriver`:
  - full poll ogni 30 s (string descriptor mancanti, poi tutti i report) e quick poll ogni 2 s dei soli report con usage di stato;
  - una richiesta per chiamata, a 50 ms l'una dall'altra, e nessuna mentre `isPollingPaused()`; il ciclo interrotto riprende da dove si era fermato;
  - le politiche dei driver sono hook: `quickPollMs()`, `fullPollMs()`, `acceptPollReport()`, `pollInputReports()`, `buildPollLists()`, `collectStringRequests()`, `onLoop()`;
  - CyberPower: nessun quick poll, full poll ogni 30 s dei soli FEATURE, esclusi gli id 4, 6 e ≥ 130 (come prima). Eaton: esclusi i report 254/255, più il string descriptor della chimica batteria. Powercom: liste fisse 0x0A ogni 2 s e 0x1D/0x21/0x1F ogni 30 s, nessuna stringa, niente più membri nascosti, chiama `GenericDriver::setup()`;
  - il report ID 0 non è più escluso: i device senza report ID ora vengono interrogati;
  - nuovo `DriverRegistry` (tabella VID/PID), usato anche da `test_record_replay`.
  - **Cambio di comportamento da verificare sull'Eaton:** prima tutti i report venivano letti ogni 2 s; ora ogni 2 s solo quelli di stato, gli altri (per esempio le tensioni) ogni 30 s, come `usbhid-ups`.
- **M1.** Parser: Logical Min/Max (salvati nel global stack), estensione del segno se Logical Minimum < 0, Logical Maximum senza segno se inviato come byte "negativo" (0xFF per 255), `bit_size` limitato a 32, long item saltati, Usage Minimum/Maximum espansi (stessa pagina, massimo 256 usage), niente più overflow di `desc[i] << 24`.
- **S2.** Nuovo `UsageMapIndex`: la corrispondenza usage → mappatura viene calcolata al primo report e poi riusata, con decodifica proporzionale alle usage del report. Stesso risultato dei vecchi cicli (ordine del descriptor, prima mappatura che corrisponde). Powercom mantiene il suo ciclo, perché riconosce le usage anche per codice e non solo per path.
- **M5.** Nuove API `hid_host_device_get_string_descriptor()` e `hid_host_device_get_string_indices()`. `requestStringDescriptor()` legge prima il language ID (con fallback 0x0409), passa il descriptor al driver e non richiede più un indice fallito una volta, così un firmware che va in timeout non avvia la scala di recovery a ogni full poll. Gli indici di produttore, modello e seriale vengono ora dal device descriptor. Ora arrivano davvero `battery.type` (Eaton) e `battery.mfr.date`.
- **Pulizie.** M3: rilascio dell'interfaccia se l'allocazione dell'IN transfer fallisce, niente `ESP_ERROR_CHECK` sul free, `in_xfer = NULL`. M6: CyberPower rimuove `input.voltage.nominal` invece di pubblicarlo vuoto. S4: `_hid_parser` privato, `isControlPending()` → `isPollingPaused()`. S5: commento rimosso.
- **S1 / S6.** Niente più log per ogni INPUT report: un riassunto ogni 60 s (`[USB] Last 60 s: N INPUT reports, N GET_REPORT ok, N failed`). `logDebug()` è attivo solo con `-DUSBUPS_DEBUG_LOG`, e Powercom non usa più `Serial.printf`. Heap libero, minimo e blocco più grande nel log `[DIAG]` e in `/api/system-status` (`heap_free`, `heap_min_free`, `heap_largest_block`).

**Cosa resta prima di chiudere la Fase 3:**
- ~~prova sull'Eaton: dati regolari, `battery.type`, margine di stack~~ fatto;
- ~~beeper da web UI e da NUT~~ fatto (Eaton);
- ~~passaggio a batteria dopo la correzione dello stato~~ fatto: `OB` corretto su Eaton 3S e sui due CyberPower;
- stacca/riattacca a raffica (Fase 4);
- ~~commit~~ fatto (`6a8e871`).

### Fase 4: validazione

- **Test nativi:** ogni fix con logica pura ha un test (beeper, conversione delle stringhe, stale al disconnect, watchdog INPUT, policy dei restart, parser).
- **Fault injection:** flag di build di debug per:
  - timeout del control transfer. `USBUPS_DEBUG_SLOW_INPUT_MS` non serviva più allo scopo ed è stato rimosso prima del merge:dalla Fase 3 il ritardo gira nel task `ups_poll` e, con INPUT ogni 3 s, blocca il task oltre i 30 s del Task WDT invece di provocare un timeout. Serve un flag nuovo che ritardi il callback di completamento del control transfer in `hid_host.c`;
  - STALL simulato sull'IN;
  - descriptor troncato;
  - disconnect durante un GET_REPORT.
- **Hardware:** stacca/riattacca a raffica (50 cicli), passaggio a batteria e ritorno, calibrazione dell'UPS, alimentazione della scheda da un'altra porta USB dell'UPS.
- **Soak test:** almeno 72 h sulle UPS dei tester (CyberPower CP1300/CP1500 di @x-magic, APC ed Eaton), con log seriali completi, reset reason e heap minimo.

#### Stato di avanzamento della Fase 4 (2026-09-26)

**Prima validazione sui CyberPower di x-magic** (firmware `fix-issue-47-v3`, commit `6a8e871`, flash completo con esptool, schede alimentate esternamente):

| | CP1300 | CP1500 |
|---|---|---|
| Uptime continuo nel log | 13,9 h | 19,6 h |
| Reset, panic, blocchi | nessuno (solo il power-on iniziale) | nessuno |
| Righe `ERROR` / `WARN` | 0 | 0 |
| GET_REPORT riusciti / falliti | ~40.100 / 0 | ~56.500 / 0 |
| INPUT al minuto | 40 costanti | 40 costanti |
| Disconnessioni USB dell'UPS | 5, riconnesse in ~0,4 s | 6, riconnesse in ~0,4 s |
| Dati validi dopo la riconnessione | 2,0-5,5 s | 1,1-3,9 s |
| Risposte sfasate in cache | 0 su 24 | 0 su 24 |
| `link` nel dump | failures 0, recoveries 0, dropped 0, `input_period_ms` 3000 | uguale |
| Stack libero minimo del task `ups_poll` | 5388 B | 5200 B |
| Heap libero (dalla 1ª ora) | stabile ~215,4 KB | stabile ~215,4 KB |

- Passaggi `OL` → `OB` → `OL CHRG` corretti su entrambi, compresa una scarica di 44 minuti. Le disconnessioni USB coincidono con i cambi di modalità e alcune avvengono nello stesso secondo sui due UPS: è il reset USB dell'UPS ipotizzato da x-magic, ora gestito senza conseguenze.
- Con il fix v2 in un periodo simile si erano visti una quindicina di timeout con deadlock, tutti seguiti da crash o blocchi.
- Heap: il minimo storico (`min free`) scende a gradini fino a ~184 KB in corrispondenza di eventi; il blocco libero più grande è fermo a 192,5 KB. Nessuna tendenza alla perdita.

**Rispetto ai criteri di §5:**
- nessun panic o blocco: ✅ nelle ~33 ore-scheda disponibili, ⏳ 72 h e altri marchi;
- dati validi entro 10 s dopo un reset USB: ✅ (massimo 5,5 s);
- beeper senza azzerare altri campi: ✅ su Eaton (C1), ⏳ CyberPower;
- web UI e NUT entro 200 ms con l'UPS muto: ⏳ da misurare (l'architettura lo garantisce dalla Fase 3, ma la misura non è stata fatta).

**Resta da fare:** soak test di 72 h, prove su APC (A2 aperiodico, A4, A5a) e Powercom, stacca/riattacca a raffica, fault injection, misura dei tempi di risposta con l'UPS muto.

## 5. Criteri di accettazione

- Nessun panic e nessun blocco in 72 h su tutti i modelli dei tester.
- Dopo un reset USB dell'UPS, i dati tornano validi entro 10 s. Nel frattempo i client NUT ricevono `ERR DATA-STALE`, mai valori congelati o `Unknown`.
- Nessun SET_REPORT del beeper che azzeri campi diversi dal beeper.
- La web UI e il server NUT rispondono entro 200 ms anche mentre l'UPS non risponde.
