# esp32-nut-server - Documento dei Requisiti di Prodotto

**Autore:** ARchetipo
**Data:** 2026-07-08
**Versione:** 1.0

---

## Elevator Pitch

<!-- archetipo:prd section=elevator_pitch required=true -->

> Un server NUT ultra-efficiente basato su ESP32-S3 per monitorare gli UPS USB senza consumi eccessivi di energia.
>
> Per **hobbisti della smart home e amministratori di piccoli uffici (SOHO)**, che riscontrano il problema di **l'elevato consumo energetico e la complessità di mantenere acceso un PC o Raspberry Pi dedicato solo per monitorare un UPS via USB**, **ESP32 NUT Server** è un **firmware stand-alone per microcontrollore** che **permette di monitorare lo stato dell'UPS direttamente su Home Assistant tramite la rete Wi-Fi locale con consumi inferiori a 1W**. A differenza di **l'installazione di un client/server NUT completo su un server o un computer sempre attivo**, il nostro prodotto **offre una soluzione plug-and-play dedicata, a bassissimo consumo e collegabile direttamente via USB nativa all'UPS**.

---

## Visione

<!-- archetipo:prd section=vision required=true -->

Creare un ponte hardware minimalista ed economico tra gli UPS sprovvisti di scheda di rete e i sistemi di domotica (come Home Assistant), rendendo il monitoraggio energetico domestico accessibile, ecologico ed estremamente affidabile.

### Elementi di Differenziazione del Prodotto

A differenza dei server NUT tradizionali che richiedono un sistema operativo Linux completo e hardware general-purpose da 10-15W (o più), ESP32 NUT Server opera a meno di 1W, si avvia istantaneamente, ed è interamente contenuto in un singolo firmware per ESP32-S3 con porta USB nativa.

---

## Persona Utente

<!-- archetipo:prd section=user_personas required=true -->

### Persona 1: Marco

**Ruolo:** Appassionato di Smart Home DIY
**Età:** 34 | **Background:** Ingegnere informatico, ama personalizzare e ottimizzare ogni aspetto della sua domotica basata su Home Assistant.

**Obiettivi:**
- Ottenere dati storici precisi sul consumo e sullo stato dell'UPS in Home Assistant.
- Minimizzare i consumi elettrici di fondo della sua attrezzatura domotica.
- Avere una configurazione semplice e riproducibile.

**Punti di Dolore:**
- Dover dedicare una porta USB e risorse di un server principale per monitorare l'UPS.
- La complessità di configurare i servizi di rete su macchine virtuali o container solo per esporre la porta USB dell'UPS.

**Comportamenti e Strumenti:**
Utilizza Home Assistant OS su un mini PC, configura automazioni complesse in YAML e Node-RED, e preferisce dispositivi microcontroller indipendenti basati su ESPHome o firmware personalizzati.

**Motivazioni:** Efficienza energetica, stabilità del sistema domotico, controllo totale sui dati locali.
**Competenza Tecnica:** Alta

#### Customer Journey - Marco

| Fase | Azione | Pensiero | Emozione | Opportunità |
|---|---|---|---|---|
| Consapevolezza | Scopre il progetto su GitHub cercando soluzioni a basso consumo per server NUT. | "Posso davvero evitare di usare un intero server solo per leggere l'UPS?" | Curiosità | Fornire una documentazione iniziale chiara che spieghi il risparmio energetico e i requisiti hardware (ESP32-S3 USB Host). |
| Considerazione | Valuta la fattibilità del collegamento fisico (collegare le linee dati USB al chip). | "È difficile saldare i pin USB sull'ESP32-S3 o basta una scheda di sviluppo dotata di porta USB OTG?" | Valutazione tecnica | Consigliare schede di sviluppo commerciali con doppia porta USB (es. una per programmazione e una OTG). |
| Primo Utilizzo | Compila il firmware tramite PlatformIO, inserisce le credenziali Wi-Fi nel file `config.json` e lo carica sulla scheda. | "Il caricamento su LittleFS è andato a buon fine, proviamo a connettere l'UPS." | Soddisfazione | Usare il LED di stato per confermare immediatamente la connessione riuscita a Wi-Fi e UPS. |
| Utilizzo Regolare | Integra il server NUT in Home Assistant con un clic e configura le automazioni di shutdown. | "Funziona da settimane senza un solo crash. Home Assistant riceve i dati all'istante." | Tranquillità | Fornire stabilità a lungo termine grazie al Watchdog hardware. |
| Promozione | Consiglia la soluzione sul forum di Home Assistant e su gruppi di domotica. | "Questa è la soluzione definitiva per gli UPS in ambiente home-lab." | Orgoglio | Facilitare la condivisione di configurazioni e schemi di collegamento. |

---

### Persona 2: Sara

**Ruolo:** Amministratrice SOHO (Small Office/Home Office)
**Età:** 42 | **Background:** Lavora da casa come consulente e possiede un piccolo server NAS per i backup dei clienti e la rete dell'ufficio.

**Obiettivi:**
- Proteggere il NAS e gli switch di rete da arresti anomali durante i blackout.
- Avere una soluzione autonoma che funzioni anche se il server principale è spento.
- Zero manutenzione post-installazione.

**Punti di Dolore:**
- Perdita di dati in passato dovuta a interruzioni di corrente improvvise.
- Difficoltà a configurare servizi di rete complessi.

**Comportamenti e Strumenti:**
Utilizza un NAS Synology, un UPS Eaton 3S 700, e gestisce la sua rete tramite un'interfaccia utente web semplice.

**Motivazioni:** Sicurezza dei dati dei clienti, affidabilità e semplicità d'uso del sistema di protezione energetica.
**Competenza Tecnica:** Media

#### Customer Journey - Sara

| Fase | Azione | Pensiero | Emozione | Opportunità |
|---|---|---|---|---|
| Consapevolezza | Cerca una soluzione hardware pronta per il protocollo NUT per collegare il NAS Synology e l'UPS. | "Mi serve un dispositivo compatto che renda di rete il mio UPS USB." | Speranza | Evidenziare la compatibilità con i client standard (inclusi NAS Synology e QNAP che supportano nativamente i server NUT). |
| Considerazione | Decide di acquistare i componenti (ESP32-S3 e cavetto) per assemblare il bridge. | "Spero che la configurazione sia semplice e non richieda righe di comando complesse." | Ansia moderata | Fornire un template JSON di esempio pre-compilato molto semplice da modificare. |
| Primo Utilizzo | Carica il file `config.json` via USB usando uno strumento grafico e collega il tutto nel rack. | "Il NAS rileva subito il server NUT sulla rete, fantastico!" | Rilassamento | Emettere log chiari sulla console seriale per facilitare la risoluzione di eventuali problemi di rete. |
| Utilizzo Regolare | Lascia il dispositivo nel rack di rete e se ne dimentica. | "Durante il blackout di ieri, il NAS si è spento in sicurezza 5 minuti dopo l'interruzione." | Sicurezza | Garantire che l'ESP32 rimanga attivo sulla batteria dell'UPS (collegando l'alimentatore dell'ESP32 a una delle prese protette dell'UPS). |
| Promozione | Recensisce positivamente l'idea su blog di tecnologia SOHO. | "Un piccolo dispositivo da 10€ mi ha salvato da una perdita di dati senza consumare corrente." | Entusiasmo | Promuovere l'ecosistema open-source. |

---

## Approfondimenti di Brainstorming

<!-- archetipo:prd section=brainstorming_insights required=true -->

> Scoperte chiave e direzioni alternative esplorate durante la sessione di inception.

### Assunzioni Sfidate

1. *Assunzione iniziale:* L'ESP32 richiede una complessa interfaccia web per configurare il Wi-Fi ed il server NUT.
   *Sfidato da Costanza/Livia:* Rimosso per l'MVP per risparmiare risorse hardware ed eliminare complessità di sviluppo. Sostituito con una configurazione basata su file JSON (`config.json`) caricato su LittleFS.
2. *Assunzione iniziale:* Connessione tramite protocollo seriale generico.
   *Sfidato da Leonardo:* L'Eaton 3S 700 utilizza lo standard USB HID PDC (Power Device Class). È necessario implementare un parser HID specifico anziché una semplice lettura seriale.

### Nuove Direzioni Scoperte

- Uso della modalità USB Host nativa dell'ESP32-S3 per evitare chip intermedi o shield USB esterni, riducendo i costi e lo spazio.
- Implementazione di un mini server TCP conforme al protocollo NUT per una compatibilità immediata con Home Assistant e sistemi NAS (Synology, QNAP) senza necessità di integrazioni personalizzate.

### Assunzioni da Validare

- Verifica della compatibilità del descrittore HID dell'Eaton 3S 700 con il parser USB Host HID sviluppato per ESP32-S3 (alcuni UPS inviano report HID leggermente diversi).
- Test della stabilità termica e del consumo del chip ESP32-S3 in modalità USB Host continua.

### Rischi Chiave

- **Rischio di implementazione hardware:** La necessità di collegare le linee dati USB (D+/D-) direttamente ai GPIO 19/20 dell'ESP32-S3 può intimidire gli utenti meno esperti di saldatura o hardware.
- **Rischio di stabilità USB Host:** Lo stack USB Host di ESP-IDF può essere soggetto a blocchi se si verificano disturbi elettrici sul cavo USB. È fondamentale l'uso di un watchdog hardware.

---

## Ambito del Prodotto

<!-- archetipo:prd section=product_scope required=true -->

### MVP - Minimum Viable Product

- Firmware per ESP32-S3 compilato con PlatformIO.
- Supporto USB Host HID PDC per leggere lo stato di alimentazione e la carica della batteria dell'UPS Eaton 3S 700.
- Server TCP NUT porta 3493 per rispondere alle query di Home Assistant (`LIST UPS`, `GET VAR`).
- Configurazione Wi-Fi e NUT statica tramite file `/config.json` su LittleFS.
- Gestione riconnessione automatica Wi-Fi e USB (hot-plug).

### Funzionalità di Crescita (Post-MVP)

- Portale Web Captive Portal per configurazione grafica del Wi-Fi e credenziali (senza caricamento manuale di LittleFS).
- Supporto per UPS di altri brand (es. APC, CyberPower) tramite configurazione del Vendor ID e mappatura dei report HID.

### Visione Futura

- Integrazione MQTT nativa con supporto Home Assistant Auto Discovery (alternativa al server NUT).
- Schermo OLED/e-Ink opzionale per mostrare in tempo reale la carica dell'UPS e lo stato di rete direttamente sul bridge hardware.

---

## Architettura Tecnica

<!-- archetipo:prd section=technical_architecture required=true -->

> **Proposta da:** Leonardo (Architect)

### Architettura di Sistema

Il sistema è basato su un singolo chip ESP32-S3. L'ESP32-S3 funge da controller USB Host collegato direttamente all'UPS Eaton tramite la porta USB nativa. In parallelo, si connette alla rete Wi-Fi locale ed esegue un server TCP NUT in ascolto sulla porta 3493. Home Assistant (o altri client NUT) interrogano periodicamente l'ESP32-S3 per raccogliere le metriche.

**Pattern Architetturale:** Event-driven non-blocking TCP server accoppiato a un task di polling USB Host.

**Componenti Principali:**
1. **Modulo USB Host Engine**: Gestisce l'inizializzazione del bus USB, l'identificazione dell'UPS e il polling dei report HID PDC.
2. **Modulo NUT Server**: Gestisce la connessione TCP, effettua il parsing dei comandi del protocollo NUT e restituisce le risposte formattate.
3. **Modulo Config Manager**: Legge e valida il file `/config.json` da LittleFS.
4. **Modulo Network Manager**: Gestisce la connessione Wi-Fi e monitora lo stato della rete per avviare riconnessioni automatiche.

### Stack Tecnologico

| Livello | Tecnologia | Versione | Motivazione |
|---|---|---|---|
| Linguaggio | C++ | C++17 | Linguaggio nativo supportato dal framework Arduino su ESP32, ideale per controllo a basso livello e performance. |
| Framework Backend | PlatformIO / Arduino ESP32 Core | v3.0.x | Fornisce le astrazioni necessarie per la gestione Wi-Fi e TCP, mantenendo l'accesso alle API USB Host native di ESP-IDF (v5.1). |
| Framework Frontend | N/A | N/A | Nessuna interfaccia frontend prevista nell'MVP |
| Database | N/A | N/A | N/A |
| ORM | N/A | N/A | |
| Autenticazione | N/A | | |
| Testing | Unity | | Integrato in PlatformIO per unit testing |

### Struttura del Progetto

**Pattern di organizzazione:** Struttura standard PlatformIO con cartelle `src/`, `lib/` e `include/`.

```text
esp32-nut-server/
├── .github/workflows/       # Pipeline CI/CD per compilazione automatica
├── data/                    # Risorse destinate a LittleFS (config.json)
│   └── config.json
├── include/
│   └── main.h
├── lib/                     # Librerie locali personalizzate
│   ├── NUTServer/           # Implementazione del protocollo NUT
│   └── USBHostUPS/          # Driver USB Host per HID Power Devices
├── src/
│   ├── main.cpp             # Ciclo principale ed inizializzazione
│   └── config_manager.cpp   # Gestione LittleFS e file JSON
└── platformio.ini           # Configurazione di build e dipendenze
```

### Ambiente di Sviluppo

Visual Studio Code con estensione PlatformIO IDE su sistema operativo Windows/macOS/Linux.

**Strumenti richiesti:** PlatformIO CLI, Git, client NUT (es. `upsc` per test di rete).

### CI/CD e Deployment

**Strumento di Build:** PlatformIO Core CLI

**Pipeline:** GitHub Actions per verificare la compilazione del codice su ogni commit/push.

**Deployment:** Flash del firmware e dell'immagine LittleFS tramite porta USB-to-UART dell'ESP32-S3 usando `esptool.py` (integrato in PlatformIO).

**Infrastruttura target:** Scheda di sviluppo ESP32-S3 (es. ESP32-S3-DevKitC-1 o clone equivalente dotato di due porte USB).

### Registri delle Decisioni Architetturali (ADR)

* **ADR-001: Scelta di LittleFS rispetto a SPIFFS** - SPIFFS è deprecato e ha prestazioni inferiori. LittleFS garantisce maggiore sicurezza nei file e velocità di scrittura/lettura.
* **ADR-002: Connessione USB Host Diretta** - Si è preferito non utilizzare shield USB Host esterni (es. MAX3421E) poiché aumenterebbero i costi, i consumi e lo spazio occupato, sfruttando invece l'USB OTG nativo dell'ESP32-S3.
* **ADR-003: Protocollo NUT semplificato** - Invece di portare l'intera suite NUT (molto pesante), viene implementato un server TCP leggero personalizzato che risponde alle interrogazioni standard di rete.

---

## Requisiti Funzionali

<!-- archetipo:prd section=functional_requirements required=true -->

### 1. Connettività di Rete (Wi-Fi)
* **FR-001**: Il firmware deve connettersi automaticamente all'avvio alla rete Wi-Fi specificata nel file di configurazione.
* **FR-002**: Il firmware deve gestire la riconnessione automatica in caso di disconnessione Wi-Fi senza richiedere il riavvio del dispositivo.

### 2. Comunicazione USB (UPS Host)
* **FR-003**: Il firmware deve configurare la porta USB dell'ESP32-S3 in modalità USB Host.
* **FR-004**: Il firmware deve identificare l'UPS Eaton 3S 700 tramite i descrittori USB HID (Vendor ID e Product ID) e la classe Power Device Class (PDC).
* **FR-005**: Il firmware deve eseguire il polling periodico dell'UPS via USB per leggere i dati essenziali: stato dell'alimentazione (rete/batteria), percentuale di carica della batteria, tensione di ingresso, e stato del carico.
* **FR-006**: Il firmware deve gestire la riconnessione dell'UPS "a caldo" (hot-plug) qualora il cavo USB venga scollegato e ricollegato, ripristinando la lettura dei dati.

### 3. Server NUT (Network UPS Tools)
* **FR-007**: Il firmware deve avviare un server TCP in ascolto sulla porta standard NUT `3493`.
* **FR-008**: Il server deve supportare i comandi di rete essenziali del protocollo NUT (come `LIST UPS`, `LIST VAR`, `GET VAR`) necessari all'integrazione di Home Assistant.
* **FR-009**: Il server NUT deve supportare l'autenticazione tramite username e password (configurati nel file `config.json`).
* **FR-010**: Il server deve gestire almeno due connessioni client TCP simultanee (es. Home Assistant e un client di debug da riga di comando).

### 4. Configurazione e Diagnostica
* **FR-011**: All'avvio, il firmware deve leggere il file `/config.json` memorizzato nel file system LittleFS per ricavare credenziali Wi-Fi, credenziali NUT ed eventuali parametri dell'UPS.
* **FR-012**: Il firmware deve utilizzare il LED di stato dell'ESP32-S3 per indicare visivamente lo stato corrente (es. lampeggio lento = connessione Wi-Fi in corso; fisso = operativo e connesso a UPS; lampeggio rapido = errore USB/UPS).

---

## Requisiti Non Funzionali

<!-- archetipo:prd section=non_functional_requirements required=true -->

### Sicurezza

* **SEC-001 (Autenticazione)**: Il server NUT deve validare le credenziali trasmesse dai client (comando `USERNAME` / `PASSWORD`) se abilitate nella configurazione, rifiutando le query in caso di credenziali errate.
* **SEC-002 (Isolamento Rete)**: Il dispositivo deve operare esclusivamente all'interno della rete locale (LAN). Non è prevista l'esposizione o l'apertura di porte verso l'esterno (WAN).

### Integrazioni

* **INT-001 (Compatibilità Home Assistant)**: Le risposte fornite dal server NUT ai comandi `LIST VAR` e `GET VAR` devono essere conformi alle aspettative del client NUT standard di Home Assistant (es. variabili come `ups.status`, `battery.charge`, `input.voltage`).
* **INT-002 (Compatibilità UPS standard)**: Il driver USB Host deve supportare i report HID standard descritti nella specifica USB PDC 1.11.

---

## Fasi Successive

<!-- archetipo:prd section=next_steps required=true -->

1. **Backlog** - Esegui `/archetipo-spec` per trasformare questo PRD in un backlog
2. **Design** - Esegui `/archetipo-design` per i mockup della UI (se applicabile)
3. **Validazione** - Rivedi con gli stakeholder e testa le assunzioni a maggior rischio

---

_PRD generato tramite ARchetipo Product Inception il 2026-07-08_
_Sessione condotta da: pasquale con il team ARchetipo_
