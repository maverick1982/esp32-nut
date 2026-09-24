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
- prova su hardware di stacca/riattacca a raffica (C2);
- confronto del descriptor dump prima e dopo sulle UPS disponibili (C3), controllando nel log l'eventuale `Short report descriptor`;
- verifica di `ups.mfr`, `ups.model` e `ups.serial` sul CyberPower con stringhe invertite (C4);
- commit, dopo la conferma.

### Fase 2: robustezza del link

| # | Intervento | Test |
|---|---|---|
| A2 | Watchdog sugli INPUT nel `LinkMonitor`: imparare l'intervallo tipico tra INPUT report (mediana degli ultimi N); se il device risulta periodico, dopo un silenzio pari a k volte l'intervallo segnare stale e poi fare recovery. Nessun effetto sui device che inviano solo sui cambi. | `test_link_monitor`: periodico → silenzio → stale/recover; aperiodico → nessuna azione |
| A3 | Nel recovery dopo un errore IN: `CLEAR_FEATURE(ENDPOINT_HALT)` sull'endpoint IN via EP0 (nuova API in `hid_host.c`), poi `stop/start` | prova su hardware; log dell'esito |
| A7 | Contatore dei restart in RTC con backoff crescente (1, 5, 15 min); oltre la soglia, modalità degradata: niente riavvii, dati stale, banner nella web UI | test nativo della policy (estratta in logica pura) |
| A5a | `DEFAULT_TIMEOUT_MS` dei control transfer da 5000 a 1500 ms (valore da confermare con un soak test su APC ed Eaton) | soak test |
| A4 | Dimensionare `in_xfer` sul massimo INPUT report del parser, arrotondato a un multiplo di MPS (limite 512 B) | test del calcolo; prova sull'APC Smart-UPS 750 pid 0003 |

### Fase 3: architettura e qualità dei dati

| # | Intervento |
|---|---|
| A5b | Task USB dedicato (priorità sopra il loopTask, sotto WiFi) che esegue `processEvents`, polling, decodifica e recovery. Il loopTask resta per NUT e web, ed entrambi leggono `UPSData` sotto `_mutex`. Aggiungere il nuovo task al Task WDT. Aggiornare l'ADR 0008 (modello di threading). |
| A6 | `GenericDriver`: quick poll ogni 2 s dei soli report con usage di stato (`PresentStatus`, `RemainingCapacity`, `RunTimeToEmpty`, `PercentLoad`), full poll ogni 30 s, come upstream `usbhid-ups` (ADR 0006) |
| S3 / M2 / M4 | Unificare la macchina a stati del polling in `GenericDriver`, con hook per le politiche dei driver (report ID esclusi, INPUT via GET_REPORT sì o no, intervalli). Powercom senza membri nascosti e con chiamata a `GenericDriver::setup()`. Registro dei driver per VID/PID. |
| M1 | Parser HID: Logical Min/Max con estensione del segno, `bit_size` limitato a 32, long item, Usage Min/Max. Test in `test_hid_parser` e `test_record_replay` su tutte le fixture. |
| S2 | Precalcolo della tabella `usage → mapping` al claim, con decodifica O(usage del report) |
| M5 | Implementare `requestStringDescriptor` (GET_DESCRIPTOR string su EP0) oppure rimuovere i passi morti dai driver |
| M3 / M6 / S4 / S5 | Pulizia: leak del claim, `in_xfer = NULL`, rimozione della chiave vuota, incapsulamento di `_hid_parser`, rinominare `isControlPending` → `isPollingPaused`, rimuovere il commento |
| S1 / S6 | INPUT report a livello DEBUG o riassunti ogni 60 s; niente `Serial.printf` nei driver; heap libero minimo nel log `[DIAG]` e in `/api/system-status` |

### Fase 4: validazione

- **Test nativi:** ogni fix con logica pura ha un test (beeper, conversione delle stringhe, stale al disconnect, watchdog INPUT, policy dei restart, parser).
- **Fault injection:** flag di build di debug per:
  - timeout del control transfer (già presente: `USBUPS_DEBUG_SLOW_INPUT_MS`);
  - STALL simulato sull'IN;
  - descriptor troncato;
  - disconnect durante un GET_REPORT.
- **Hardware:** stacca/riattacca a raffica (50 cicli), passaggio a batteria e ritorno, calibrazione dell'UPS, alimentazione della scheda da un'altra porta USB dell'UPS.
- **Soak test:** almeno 72 h sulle UPS dei tester (CyberPower CP1300/CP1500 di @x-magic, APC ed Eaton), con log seriali completi, reset reason e heap minimo.

## 5. Criteri di accettazione

- Nessun panic e nessun blocco in 72 h su tutti i modelli dei tester.
- Dopo un reset USB dell'UPS, i dati tornano validi entro 10 s. Nel frattempo i client NUT ricevono `ERR DATA-STALE`, mai valori congelati o `Unknown`.
- Nessun SET_REPORT del beeper che azzeri campi diversi dal beeper.
- La web UI e il server NUT rispondono entro 200 ms anche mentre l'UPS non risponde.
