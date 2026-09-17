---
title: "Risoluzione dei Problemi"
type: manual
tags: [user-manual]
---

# Risoluzione dei Problemi

Questa sezione ti aiuta a diagnosticare e risolvere i problemi più comuni che potresti incontrare nell'utilizzo di ESP32-NUT.

## Diagnostica Integrata (Log di Sistema)

Il primo passo per risolvere qualsiasi anomalia è consultare i log di sistema.
Accedi all'interfaccia web del dispositivo digitando il suo indirizzo IP nel browser e naviga nella scheda **System Logs**. Qui potrai leggere in tempo reale cosa sta facendo il firmware (es. errori di connessione Wi-Fi, tentativi di handshake USB).

## Problema: "UPS Non Rilevato" (LED Rosso o Errore UI)

Se la Web UI mostra che nessun UPS è connesso, o il LED di stato lampeggia rapidamente in rosso, verifica i seguenti punti:

1. **Pad USB-OTG non saldati:** Se usi una scheda ESP32-S3 generica con due porte USB-C, assicurati di aver unito con lo stagno i due piccoli pad posteriori etichettati `USB-OTG`. Senza questo ponte, la scheda non invia l'alimentazione a 5V verso la porta USB e l'UPS non si accenderà (lato logico).
2. **Cavo / Adattatore Errato:** Assicurati di utilizzare un vero adattatore **USB OTG** (On-The-Go). I normali adattatori fisici da pochi centesimi spesso mancano della resistenza interna necessaria (sui pin CC) per segnalare all'ESP32 che deve comportarsi da "Host" e non da periferica. Senza un cavo/adattatore esplicitamente certificato OTG, la comunicazione dati non si avvierà.
3. **Porte invertite:** Controlla che l'alimentazione a muro sia collegata alla porta `COM`/`UART`, e il cavo dell'UPS sia collegato alla porta etichettata `USB`.

## Il mio UPS usa una connessione Seriale (RJ45 a USB, Megatec, ecc.)

Allo stato attuale, ESP32-NUT supporta **solo e unicamente** gli UPS che si presentano come dispositivi USB HID (Human Interface Device) conformi alla specifica *USB HID Power Device Class* (driver `usbhid-ups`).

Gli UPS che comunicano tramite interfacce Seriali emulate via USB (es. convertitori ch340, ft232) o protocolli seriali proprietari non sono supportati nativamente da questo ramo del progetto.

## UPS riconosciuto, ma mancano dei dati (Es. Livello Batteria al 0%)

I vari produttori (CyberPower, Eaton, APC) implementano l'albero HID in modo leggermente diverso. Se il tuo UPS viene riconosciuto ma usa il **Driver Generico** e mancano dati vitali, possiamo aggiungere il supporto specifico!

**Come richiedere supporto per il tuo UPS:**
1. Vai sulla Web UI dell'ESP32, nella scheda **System Logs**.
2. Clicca sul pulsante **Export USB Diagnostics**. Verrà scaricato un file chiamato `usb_diagnostics.json` contenente l'albero dei descrittori HID del tuo UPS.
3. Apri una nuova *Issue* sulla pagina GitHub del progetto e allega questo file, indicando la marca e il modello esatto del tuo UPS. Verrà utilizzato per scrivere un driver su misura!
