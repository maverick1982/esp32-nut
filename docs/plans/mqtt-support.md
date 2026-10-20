---
type: plan
title: "Supporto MQTT (stato UPS e log)"
description: "Analisi preliminare: pubblicazione via MQTT dello stato dell'UPS e dei log, con esp-mqtt del core Arduino."
tags: [piano, analisi, mqtt, home-assistant, log]
---
# Supporto MQTT (stato UPS e log)

**Data:** 2026-10-20  
**Stato:** Analisi preliminare, non ancora pianificata  
**Obiettivo:** Valutare l'aggiunta di un client MQTT che pubblichi lo stato dell'UPS e i log del firmware, per chi usa Home Assistant senza l'integrazione NUT o altre piattaforme (ioBroker, Node-RED, openHAB).

---

## 1. Fattibilità

- Il client MQTT ufficiale di ESP-IDF, **`esp-mqtt`**, è già incluso nel core Arduino precompilato che usiamo (pioarduino 51.03.04): `framework-arduinoespressif32-libs/esp32s3/lib/libmqtt.a` e `include/mqtt/esp-mqtt/include/mqtt_client.h`. Non serve nessuna libreria esterna.
- `esp-mqtt` gira in un task suo, si riconnette da solo e supporta QoS, Last Will e TLS. Non blocca il loopTask, quindi non interferisce con il watchdog (issue #47, ADR 0008).
- **Spazio:** la partizione app è di 4 MB (`partitions.csv`, `0x3F0000`) e il firmware occupa circa 2 MB.
- Non tocca il layer USB né il server NUT: è un'aggiunta isolata, a basso rischio.
- Al 2026-10-20 non risultano issue che chiedano MQTT.

---

## 2. Stato dell'UPS

- **Fonte:** `UPSData`, che contiene già tutte le variabili (le stesse che vede un client NUT, senza le chiavi interne `ups.status.*`, vedi US-088).
- **Quando:** a intervalli regolari (configurabile, per esempio 30 s) e **subito** quando cambia `ups.status` (OL → OB, comparsa di LB).
- **Topic proposti:**
  - `esp32-nut/<id>/status`: la stringa di stato (`OL`, `OB DISCHRG LB`, …);
  - `esp32-nut/<id>/state`: un JSON con tutte le variabili, oppure un topic per variabile (da decidere: il JSON è più leggero, i topic singoli sono più comodi per Node-RED);
  - `esp32-nut/<id>/availability`: Last Will con `online`/`offline`, per sapere se la scheda è viva.
- `<id>` può riusare l'ID dispositivo già calcolato dal modello della scheda e dal MAC (vedi #50).
- **Autodiscovery di Home Assistant** (fase successiva): topic `homeassistant/sensor/<id>/…/config` per creare i sensori in automatico. È il valore aggiunto principale per chi non usa l'integrazione NUT.

---

## 3. Log

- `AppLogger::log()` (`src/core/app_logger.cpp`) viene chiamato dal loopTask **e** dal task USB, sotto un `recursive_mutex`. Pubblicare lì dentro è rischioso:
  - si rischiano blocchi e attese dentro il task USB;
  - un errore MQTT che scrive a sua volta un log creerebbe un ciclo.
- **Soluzione:** un hook nel logger che mette il messaggio in una coda. La coda viene svuotata dal loopTask, oppure si usa `esp_mqtt_client_enqueue()`, che non blocca. Va escluso dalla pubblicazione tutto ciò che logga il modulo MQTT stesso.
- **Filtro** per livello (per esempio solo WARN/ERROR di default) e **limite di frequenza**, per non saturare il broker durante una raffica di errori USB.
- Topic proposto: `esp32-nut/<id>/log`, payload JSON coerente con l'export della Web UI (`{id, time, level, msg}`).

---

## 4. Configurazione e Web UI

- Nuova struttura in `ConfigManager` (Preferences, come Wi-Fi e NUT): abilitato sì/no, URI del broker, porta, utente, password, topic base, TLS sì/no, intervallo di pubblicazione, livello minimo dei log, autodiscovery sì/no.
- La password va gestita come le credenziali NUT: salvata, mai rimandata in chiaro dalla API.
- Nuova scheda nella Web UI e relativo endpoint API, dietro il login della Web UI (ADR 0009).
- Stato della connessione al broker visibile nella Web UI e nel System Log.

---

## 5. Risorse e rischi

- **Memoria:** il task di `esp-mqtt` usa circa 6 KB di stack, più piccoli buffer di ingresso e uscita. Con **TLS** servono circa 40 KB di heap in più per la connessione: da verificare con il log dell'heap che già scriviamo ogni 60 s (`[DIAG] Heap`).
- **Broker irraggiungibile:** la riconnessione è automatica, ma va limitata la crescita della coda dei messaggi in uscita.
- **Comandi via MQTT:** tecnicamente basta una subscribe che chiama `executeCommand()`, ma significa poter spegnere l'UPS da un broker spesso non protetto. Da tenere fuori dalle prime fasi; se si fa, disattivato di default e con le stesse protezioni dei comandi della Web UI.

---

## 6. Test

- **Nativi:** costruzione di topic e payload, filtro e limite di frequenza dei log, rilevamento del cambio di `ups.status`.
- **Web UI:** Playwright per la nuova scheda di configurazione.
- **Manuale:** prova con un broker Mosquitto (anche con TLS) e con l'autodiscovery di Home Assistant.

---

## 7. Fasi proposte

1. Pubblicazione dello stato (periodica e al cambio di `ups.status`), configurazione, Last Will.
2. Log via MQTT, con coda, filtro e limite di frequenza.
3. Autodiscovery di Home Assistant.
4. Comandi via MQTT (opzionale, da valutare per la sicurezza).

Lavoro medio-piccolo: ogni fase è una US indipendente.
