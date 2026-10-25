---
type: decision
title: "Supporto multi-scheda: ESP32-P4 ed Ethernet"
description: "Decisioni di prodotto per supportare più schede (ESP32-S3, ESP32-P4), la rete Ethernet e il recupero dell'accesso sulle schede senza Wi-Fi."
tags: [pdr, prodotto, hardware, esp32-p4, ethernet, rete, sicurezza]
---
# Supporto multi-scheda: ESP32-P4 ed Ethernet

* **PDR ID:** 0002
* **Stato:** Accettato
* **Data:** 2026-10-21
* **Autori:** @maverick1982, Claude
* **Decisioni architetturali collegate:** [ADR 0010](../adr/0010-modular-architecture-and-board-profiles.md)
* **Modifica:** [PDR 0001](0001-autenticazione-web.md), D6 e D7 (recupero dell'accesso sulle schede senza Wi-Fi)
* **Origine:** [#94](https://github.com/maverick1982/esp32-nut/issues/94) (fork di rtadams89 con il supporto ESP32-P4 ed Ethernet)

## Problema

Oggi esp32-nut gira su una sola scheda: ESP32-S3 DevKit, collegata alla rete via Wi-Fi.

Nella [#94](https://github.com/maverick1982/esp32-nut/issues/94) un utente chiede il supporto all'**ESP32-P4** con porta **Ethernet**, per due motivi: affidabilità del cavo e alimentazione **PoE** con un solo cavo. Ha già un fork funzionante sulla Waveshare ESP32-P4-ETH. Il fork però:
- sceglie la rete in compilazione (P4 = solo Ethernet), anche se alcune schede P4 hanno un coprocessore Wi-Fi ESP32-C6;
- sul P4 non ha Access Point, quindi nessun recupero dell'accesso come quello della modalità AP (PDR 0001, D6 e D7). Il doppio reset cancella le credenziali web e chiunque sulla rete locale può crearne di nuove, senza limite di tempo;
- ha tre immagini e tre pulsanti nel web installer.

Servono regole di prodotto valide per qualsiasi scheda futura, non solo per il P4.

## Obiettivi

- Chi usa l'ESP32-S3 non nota differenze: stessa immagine, stesse impostazioni, stessa procedura.
- Chi ha una scheda supportata la installa senza dover capire quale immagine gli serve.
- Chi preferisce il cavo può usare l'Ethernet (e il PoE, se la scheda lo prevede).
- Nessuna impostazione della Web UI può chiudere l'utente fuori dalla scheda.
- Su ogni scheda, chi ha accesso fisico può recuperare l'accesso alla Web UI, senza lasciarla esposta alla rete locale più del necessario.

## Fuori ambito

- Wi-Fi ed Ethernet attivi **contemporaneamente** (vedi D3).
- Moduli Ethernet SPI (W5500) sull'ESP32-S3: l'architettura lo permette (ADR 0010), ma non è deciso. Resta un lavoro futuro.
- Schede P4 con cablaggio Ethernet diverso dal design di riferimento Espressif: si possono aggiungere con un nuovo profilo, su richiesta.
- `nutdrv_qx` e MQTT: hanno analisi separate in `docs/plans`.

## Decisioni

### D1. Schede supportate e livello di supporto

| Scheda | Rete | Immagine | Livello di supporto |
|---|---|---|---|
| ESP32-S3 DevKit (e cloni) | Wi-Fi | ESP32-S3 | Supportata e testata, come oggi |
| ESP32-P4 rev. v3.1+ con design di riferimento Espressif (es. Waveshare ESP32-P4-ETH) | Ethernet; Wi-Fi se la scheda ha il coprocessore ESP32-C6 | ESP32-P4 | Pubblicata **senza test su hardware** finché la community non la prova: il README lo dice chiaramente |
| ESP32-P4 prima della v3 (v1.x, prime schede) | come sopra | ESP32-P4 pre-v3 | Supportata, provata da rtadams89 sulla sua scheda v1.3 (non abbiamo hardware P4) |

Le immagini sono separate solo dove l'hardware lo impone: architettura del chip diversa (S3 / P4) e revisioni di silicio del P4 non compatibili tra loro (ADR 0010).

### D2. Installazione e aggiornamento

- Il web installer **riconosce il chip** collegato e installa l'immagine della sua famiglia: chi ha un ESP32-S3 o un ESP32-P4 non sceglie l'immagine a mano.
- Se l'installer non riesce a distinguere la revisione del P4, ci sono due pulsanti P4 (v3.1+ e pre-v3), con l'indicazione di come leggere la revisione (`esptool.py chip_id` o il log di avvio).
- Un aggiornamento OTA con l'immagine sbagliata (altro chip o altra revisione) viene **rifiutato**: la scheda continua con il firmware attuale e l'utente vede un errore.
- Il pacchetto di release dell'ESP32-S3 mantiene il nome attuale, così i link esistenti continuano a funzionare.

### D3. Rete: un solo link attivo

- Ogni scheda usa **un solo link alla volta**: Wi-Fi oppure Ethernet. Il server NUT, la Web UI e il nome host corrispondono sempre a un solo indirizzo.
- Se la scheda ha entrambi, la Web UI offre la scelta:
  - **Wi-Fi**;
  - **Ethernet**;
  - **Automatico**: Ethernet quando il cavo ha il link, altrimenti Wi-Fi.
- Il valore iniziale dipende dalla scheda: Wi-Fi sull'ESP32-S3, Ethernet sulle schede P4.
- In Ethernet l'indirizzo arriva via DHCP e il nome host è `esp32-nut-xxxxxx` (ultimi 3 byte del MAC). La barra di stato della Web UI mostra il link attivo, ad esempio `Ethernet: 192.168.1.10 (100 Mbps)`.
- Sulle schede senza Wi-Fi la scheda Wi-Fi della Web UI non compare.

### D4. Cosa può configurare l'utente

Regola: **un parametro è un'impostazione della Web UI solo se, sbagliandolo, l'utente non perde l'accesso alla scheda.**

- **Fissi per scheda** (nel profilo compilato nell'immagine): i pin che, se sbagliati, tolgono la rete. Per esempio i pin Ethernet di una scheda senza Wi-Fi, o i pin SDIO del coprocessore Wi-Fi.
- **Impostazioni della Web UI**, con i valori iniziali dati dalla scheda:
  - modalità di rete (D3), quando ci sono entrambi i link;
  - credenziali Wi-Fi, nome dell'UPS e credenziali NUT, come oggi;
  - porta USB a cui è collegato l'UPS, sulle schede che ne hanno più di una (ESP32-P4: High-Speed o Full-Speed);
  - LED di stato acceso o spento. Le schede senza il LED dell'ESP32-S3 DevKit partono senza LED.
- Una scheda con un cablaggio diverso richiede un suo profilo e quindi una build dedicata.

### D5. Recupero dell'accesso sulle schede senza Wi-Fi

Sulle schede senza Wi-Fi non c'è la modalità AP del PDR 0001. Al suo posto il **doppio reset entro 3 secondi** apre una **finestra di recupero** di pochi minuti. Il valore esatto si fissa nella spec; il riferimento è il timeout di 10 minuti della modalità AP manuale.

- Durante la finestra, la Web UI mostra il form **"Crea credenziali"** senza chiedere la password attuale. Le nuove credenziali **sostituiscono** le vecchie e chiudono tutte le sessioni aperte.
- Il doppio reset **non cancella** le credenziali esistenti. Se nessuno ne crea di nuove prima della chiusura, restano valide e la scheda torna come prima. Un doppio reset accidentale, per esempio un calo di corrente, non lascia la scheda esposta.
- Durante la finestra la scheda resta sulla rete di casa e il server NUT continua a funzionare.
- Apertura e chiusura della finestra e sostituzione delle credenziali vengono scritte nel System Log.
- Le schede con Wi-Fi mantengono il recupero tramite AP del PDR 0001.

### D6. Integrazione del P4 nel progetto

- Il supporto P4 entra **dopo** l'aggiornamento della piattaforma a ESP-IDF 5.5 e il refactoring di schede e rete (ADR 0010, passi 1 e 2).
- Chiediamo a rtadams89 di adattare la sua PR alla nuova struttura. Il suo lavoro resta riconosciuto come contributo: è l'autore dei commit ed è citato nelle note di rilascio.
- La PR porta il firmware del P4 (profilo, link Ethernet, Web UI senza Wi-Fi, diagnostica dei crash su RISC-V, ambienti PlatformIO). CI, zip della release, web installer e README li facciamo noi, perché rtadams89 non può provarli e si intrecciano con la scelta della versione del firmware (US-099, US-100).
- Le release del P4 dipendono dai test della community: le note di rilascio indicano su quali schede e revisioni è stata provata ciascuna versione.

## Limiti accettati

- **Finestra di recupero sulla LAN.** Durante la finestra di D5, chiunque sulla rete locale può sostituire le credenziali web. Il rischio è limitato a pochi minuti e richiede che qualcuno abbia appena fatto il doppio reset sulla scheda. È lo stesso modello di fiducia del PDR 0001 (D3 e "Accesso fisico").
- **Immagine P4 v3.1+ non testata.** rtadams89 ha una scheda v1.3 (pre-v3), quindi l'immagine v3.1+ può non funzionare finché qualcuno non la prova. L'OTA rifiuta comunque un'immagine per la revisione sbagliata, quindi non può rendere inutilizzabile una scheda di un'altra revisione.
- **Correzione del 2026-10-25.** La prima versione di questo PDR invertiva le due immagini: diceva non testata la pre-v3. Da #94 risulta che rtadams89 ha una v1.3 e nessuna scheda v3+.
- **Nessun hardware P4 nel progetto.** I problemi specifici del P4 si analizzano con i log e le diagnostiche inviate dagli utenti.
- **Schede P4 con cablaggio diverso.** Non funzionano con l'immagine pubblicata finché non viene aggiunto il loro profilo.

## Suddivisione nel backlog

| Spec | Contenuto | Dipende da |
|---|---|---|
| A | Aggiornamento della piattaforma a ESP-IDF 5.5 (`docs/plans/platform-upgrade-idf55.md`), senza cambi di comportamento | - |
| B | Profili delle schede e link di rete: profilo ESP32-S3, link Wi-Fi ed Ethernet dietro un'interfaccia, modalità di rete nella Web UI (D3, D4) | A |
| C | Finestra di recupero dopo il doppio reset sulle schede senza Wi-Fi (D5) | B |
| D | Supporto ESP32-P4: profilo della scheda, link Ethernet, immagini v3.1+ e pre-v3 (D1, D2, D6). PR di rtadams89 adattata | B, C |
| E | Distribuzione: CI a tre immagini, zip della release, web installer con riconoscimento del chip, README (D1, D6). Fatta da noi | D |
