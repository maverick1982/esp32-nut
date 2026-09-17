---
type: plan
title: "Piano di Migrazione USBHostUPS"
description: "Specifiche e Piano di Migrazione: Adozione di usb_host_hid per USBHostUPS."
tags: [piano, migrazione, usb, refactoring]
---
# Specifiche e Piano di Migrazione: Adozione di `usb_host_hid` per USBHostUPS

**Data:** 2026-09-04  
**Stato:** Approvato per implementazione  
**Obiettivo:** Risolvere definitivamente le instabilità hardware, le disconnessioni casuali e la fragilità dei trasferimenti USB del sottosistema `USBHostUPS`, sostituendo la gestione raw del controller DWC_OTG con il componente ufficiale Espressif `usb_host_hid`.

---

## 1. Motivazione e Analisi del Problema

### Cause Radice dell'attuale implementazione (`lib/USBHostUPS`)
1. **Accoppiamento con il Loop di Arduino:**
   * L'elaborazione degli eventi (`usb_host_lib_handle_events()`) e l'invio dei transfer girano nello stesso thread/loop del Web Server e del Server NUT. Latenze introdotte da Wi-Fi, pagine web o scritture NVS causano jitter nei token USB, timeout hardware e disconnessioni.
2. **Fragilità sui trasferimenti di Controllo (EP0):**
   * Mancanza di un layer di trasporto tollerante verso lievi disallineamenti di buffer (Babble errors, STALL, overrun di byte), frequenti nei firmware UPS non conformi.
3. **Allocazione Dinamica Continua:**
   * Creazione e distruzione continua di strutture `usb_transfer_t` in heap durante il polling, con rischio di frammentazione della memoria nel lungo periodo (24/7/365).
4. **Gestione Disconnessione (Plug/Unplug):**
   * La deregistrazione e chiusura asincrona del dispositivo non segue una macchina a stati solida, portando a volte a panic/core dump alla riconnessione fisica del cavo.

---

## 2. Architettura Target: La Strategia "70 / 30"

L'architettura separa nettamente il **Trasporto Hardware** dalla **Logica di Dominio UPS**:

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        ESP32-NUT ARCHITECTURE                          │
├────────────────────────────────────────────────────────────────────────┤
│ 1. LIVELLO TRASPORTO (100% delegato a usb_host_hid - ~70% complessità)  │
│    - Gestione Task FreeRTOS ad alta priorità (Core pinning dedicato)   │
│    - Enumerazione USB standard e navigazione descrittori HID           │
│    - Ricezione asincrona Interrupt IN endpoint                         │
│    - Ciclo di vita plug/unplug, deallocazione pulita e reset bus       │
│    - Invio e ricezione trasferimenti di controllo (GET_REPORT)         │
├────────────────────────────────────────────────────────────────────────┤
│ 2. LIVELLO LOGICA & MAPPING (Nostro dominio specialistico - ~30%)      │
│    - Politica di polling (intervalli veloci/lenti, inibizione comandi)  │
│    - Subdriver dedicati (APCDriver, Eaton, CyberPower, OpenUPS, ecc.)   │
│    - Quirk specifici (Powercom special-poll, WalleCube scaling 0.1)    │
│    - Estrazione ed elaborazione bit-level dei payload grezzi            │
│    - Popolamento sicuro (thread-safe con Mutex) di UPSData             │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Impatto sulle Risorse (RAM e Flash)

Sulla base delle verifiche di compilazione su `esp32-s3-standard`:
* **Memoria Flash:** Incremento stimato di ~25-35 KB (occupazione attuale: 28.4% su 4MB). **Impatto trascurabile**.
* **Memoria RAM:** Incremento stimato di ~8-12 KB (occupazione attuale: 14.8% su 327KB), dovuti al task stack FreeRTOS e ai buffer interni del componente Espressif. **Ampiamente sostenibile** (oltre 260 KB di RAM libera residua).

---

## 4. Requisiti e Decisioni di Design

### R1. Thread Safety (Protezione Dati e Ciclo di Vita Driver)
* **Protezione Dati (`UPSData`):** Poiché `usb_host_hid` invoca i callback nel contesto del proprio task FreeRTOS, gli accessi a `UPSData` (da driver USB, `NUTServer` e `WebConfigServer`) devono essere protetti. Invece di trasformare l'intera struct in Getter/Setter (che richiederebbe un refactoring massivo di centinaia di riferimenti nel codice sorgente e nei test), verrà implementato un meccanismo di Lock a livello di `GenericDriver` o gestore globale (es. `acquireData()` / `releaseData()`) per accedere alla struct in sicurezza.
* **Ciclo di Vita del Driver:** Un semplice azzeramento dell'istanza `IUPSDriver` in `usb_host_hid_CLOSE_EVENT` non è sufficiente e causa *Use-After-Free* se `NUTServer` o altri componenti la stanno interrogando contemporaneamente all'unplug. Verrà introdotto un meccanismo di sincronizzazione (es. Reader-Writer Lock o Reference Counting) per garantire che l'istanza del driver non venga distrutta finché ci sono transazioni pendenti attive.

### R2. Controllo Esplicito del Polling (No Auto-Polling Cieco)
* Disabilitare o non affidarsi al recupero automatico indiscriminato di tutti i report censiti. Il nostro loop di polling (`GenericDriver`) continuerà a governare:
  * Quali Report ID interrogare e con quale periodicità.
  * L'esclusione di dispositivi fragili (es. Powercom, dove interrogazioni errate causano il freeze del microcontrollore dell'UPS).

### R3. Preservazione dei Quirk e dei Subdriver
* Mantenere intatta la gerarchia `IUPSDriver`, `GenericDriver` e i subdriver specializzati (`APCDriver`, `PowercomDriver`, `OpenUPSDriver`, ecc.).
* Ricevendo da `usb_host_hid` i byte grezzi del report (`const uint8_t *data, size_t length`), i subdriver continueranno ad applicare i consueti fattori di moltiplicazione, correzioni esponenti e inversione di bit (`QUIRK_INVERT_STRINGS`).

### R4. Buffer Descrittori Estesi
* Configurare tramite macro o build flags una dimensione adeguata per il Report Descriptor (almeno 2048 o 4096 byte) per garantire la piena compatibilità con UPS complessi (es. APC Smart-UPS, Eaton).

---

## 5. Piano Operativo di Implementazione (Fasi)

### Fase 1: Predisposizione Ambiente e Spike di Compilazione
- [x] 1. Creare un branch dedicato `feature/refactor-esp-hid-host`.
- [x] 2. Verificare l'inclusione di `esp_hid` in `platformio.ini` (o componenti ESP-IDF) e accertarsi che compili senza warning o conflitti di simboli con il framework Arduino-ESP32.
- [x] 3. Implementare la primitiva di sincronizzazione (Lock/Mutex) per la protezione di `UPSData` (senza alterare la struct) e strutturare la protezione (RWLock/RefCounter) per il ciclo di vita dell'istanza `IUPSDriver`.

### Fase 2: Riprogettazione di `USBHostUPS`
1. Sostituire le chiamate di basso livello (`usb_host_transfer_alloc`, `usb_host_device_open`, ecc.) registrando i callback di `usb_host_hid`:
   * `usb_host_hid_OPEN_EVENT`: Riconoscimento VID/PID, lettura stringhe descrittive (Manufacturer, Product, Serial), istanziazione dinamica del subdriver (`IUPSDriver`).
   * `usb_host_hid_REPORT_EVENT`: Inoltro immediato dei dati asincroni (Interrupt IN) al metodo `decodeReport()` del driver attivo.
   * `usb_host_hid_CLOSE_EVENT`: Pulizia sicura del driver e azzeramento stato connessione.
- [x] 2. Adattare il metodo di polling ciclico (`loop`) affinché utilizzi `usb_host_hid_dev_get_report(dev, type, id, req_len)`.

### Fase 3: Riadattamento dei Subdriver e Quirk
- [x] 1. Verificare che `GenericDriver`, `APCDriver`, `PowercomDriver` e `OpenUPSDriver` si interfaccino con la nuova firma del wrapper.
- [x] 2. Mantenere la compatibilità al 100% con `dumpUSBDiagnostics()` per l'estrazione dei report JSON di debug sulla Web UI.

### Fase 4: Validazione e Test di Regressione
- [x] 1. Esecuzione dei test unitari e replay fixture (`test/test_desktop` o test nativi).
- [x] 2. Test di stabilità a lungo termine: stress test di connessione/disconnessione continua del cavo USB.
- [x] 3. Test di carico concorrente: interrogazione massiva della Web UI e polling NUT continuo a 10 Hz per verificare l'assenza totale di jitter o frame persi sull'USB.


