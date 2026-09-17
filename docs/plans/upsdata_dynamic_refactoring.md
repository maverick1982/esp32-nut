---
type: plan
title: "Piano di Refactoring: `UPSData` Dinamico (Dizionario Key-Value)"
description: "Documento: Piano di Refactoring: `UPSData` Dinamico (Dizionario Key-Value)"
tags: [plan]
---
# Piano di Refactoring: `UPSData` Dinamico (Dizionario Key-Value)

## 1. Obiettivo e Contesto Architetturale
Il refactoring ha lo scopo di migrare la gestione dei parametri dell'UPS da una `struct` statica a un dizionario dinamico (Key-Value) basato su stringhe, utilizzando le chiavi gerarchiche standard di NUT (es. `battery.voltage`, `ups.status`).

Questo intervento **rispetta rigorosamente l'ADR 0003** (*Faithfully Mirror Official NUT Drivers Behavior*), allineando in modo definitivo l'architettura di ESP32-NUT a quella del server `upsd` ufficiale, che si basa su una raccolta dinamica di dati (dstate) piuttosto che su proprietà C++ compilate staticamente.

## 2. Nuova Struttura Dati (`UPSData`)
Il file `UPSData.h` e `UPSData.cpp` verranno completamente riscritti.

### Design Pattern
- Utilizzo di un `std::vector<UPSParameter>` (dove `UPSParameter` è una struct `{ String key; String value; }`) pre-allocato tramite `reserve(60)` per annullare la frammentazione dell'heap causata dalle operazioni di rilocazione.
- Inserimento/Aggiornamento *in-place*: se la chiave esiste già, la stringa del valore viene sostituita, riducendo l'impatto sulla memoria nel ciclo continuo di polling USB.

### API della nuova classe
- `void set(const String& key, const String& value);`
- `String get(const String& key, const String& defaultValue = "") const;`
- Helper tipizzati: `float getFloat(const String& key, float defaultVal = 0.0f) const;`
- `bool has(const String& key) const;`
- `const std::vector<UPSParameter>& getAll() const;`

### Gestione Stato (Status String)
La logica per generare `OL`, `OB`, `LB`, `CHRG` (attualmente in `computeUPSStatusString`) verrà mantenuta, ma leggerà i trigger da variabili boolean astratte (es. `ups.status.ac_present`) popolate dal driver.

## 3. Refactoring dei Driver USB (Livello di astrazione)
I driver correnti (`GenericDriver.cpp`, `APCDriver.cpp`, `CyberPowerDriver.cpp`) usano già tabelle di mapping (`mappings[]`). L'impatto sarà solo sintattico ma esteso.

**Prima:**
```cpp
{ "UPS.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
    d.has.inputVoltage = true; d.inputVoltage = v; 
} }
```

**Dopo:**
```cpp
{ "UPS.Input.Voltage", [](GenericDriver*, UPSData& d, double v, const HIDUsageDef*) { 
    d.set("input.voltage", String(v, 1)); 
} }
```

## 4. Refactoring Interfacce e Consumer (WebApi, Main, MQTT)
I componenti che "leggono" i dati non dovranno più sapere a priori quali campi la struct contiene.

### `WebApiJson.cpp`
Il metodo `generateUpsVars` verrà snellito drasticamente: eliminerà le ~40 righe di `if (data->has.xxx) doc["xxx"] = data->xxx;` per diventare un semplice iteratore che esporta l'intero dizionario nel JSON.

### `main.cpp`
I riferimenti diretti ai campi (es. `usb_ups.getUPSData()->outputVoltage`) saranno tradotti nelle relative chiamate `getFloat("output.voltage")` e similari. Se un parametro non esiste, la funzione tornerà il default in sicurezza.

## 5. Fasi di Implementazione e TDD

L'implementazione avverrà per step incrementali, garantendo che i test unitari continuino a validare il codice in ogni fase (in linea con le regole TDD del progetto).

- **Fase 1 (Core):** Riscrivere `UPSData` (Header e CPP) e aggiornare/creare i relativi Unit Test per testare inserimento, lettura, preallocazione vettori e generazione dello status.
- **Fase 2 (Parser USB):** Modificare `GenericDriver` e i sub-driver (`APC`, `CyberPower`, ecc.) affinché scrivano sulla nuova API. Lanciare e correggere gli Unit Test dei driver (`test_usb_host`, `test_apc_driver`, ecc.).
- **Fase 3 (Consumer):** Refactoring di `WebApiJson.cpp` e `main.cpp`. Compilazione finale e test in hardware (ambiente PlatformIO).

## 6. Rischi e Mitigazioni
- **Rischio Frammentazione Heap:** Mitigato con l'uso di vector pre-allocato e modifica delle `String` in-place.
- **Overhead CPU (String Matching):** Il numero massimo di chiavi in un report UPS è tipicamente ~30-50. Una ricerca lineare `O(N)` su array così piccoli su ESP32 a 240MHz costa una manciata di microsecondi, tempo non misurabile su chiamate che avvengono una volta al secondo.

