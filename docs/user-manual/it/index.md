---
title: "Introduzione"
type: manual
tags: [user-manual]
---

# Introduzione

Benvenuto nel Manuale Utente di ESP32-NUT. Spiega come scegliere l'hardware, installare il firmware, configurare la scheda e collegarle i client NUT. Descrive il firmware v1.14.0.

## Cos'è ESP32-NUT?

ESP32-NUT è un firmware open source che trasforma un UPS USB in un UPS di rete. La scheda fa da host USB per l'UPS, ne legge i dati e li pubblica sulla rete locale con il protocollo standard Network UPS Tools (NUT), sulla porta 3493. Home Assistant, Synology, TrueNAS, PeaNUT, `upsmon` e qualsiasi altro client NUT la usano come un server NUT su un PC, senza un Raspberry Pi o un computer sempre acceso vicino all'UPS.

Funziona su due famiglie di schede:

- **ESP32-S3**, collegata in **Wi-Fi**;
- **ESP32-P4** con porta **Ethernet** (e PoE sulle schede che lo hanno).

## Funzionalità Principali

- 🔌 **Host USB per l'UPS:** legge i dati standard USB HID Power Device, con driver specifici per Eaton/MGE, APC, CyberPower, Powercom e altri, e un driver generico per gli altri modelli.
- 🔗 **Server NUT integrato:** protocollo NUT standard sulla porta 3493, fino a 8 client contemporanei, con gli stessi nomi di variabile del driver `usbhid-ups` di NUT.
- ⚡ **Comandi e impostazioni dell'UPS:** test della batteria, cicalino, comandi di carico e spegnimento e parametri scrivibili dell'UPS, dall'interfaccia web o con `upscmd` e `upsrw`.
- 🏷️ **Variabili NUT personalizzate:** sostituisci un valore sbagliato o aggiungine uno mancante per i client NUT, come `override.*` nel file `ups.conf` di NUT.
- 🔋 **Spegnimento automatico:** la scheda può spegnere da sola l'UPS a batteria scarica, anche senza client NUT collegati.
- 🖥️ **Interfaccia web con login:** dati in tempo reale, comandi, impostazioni, rete, log e aggiornamenti del firmware, protetti da nome utente e password.
- 🌐 **Rete:** Wi-Fi con access point di configurazione (ESP32-S3) o Ethernet (ESP32-P4), con il nome host che preferisci.
- 📜 **Log e diagnostica:** System Log nel browser, syslog remoto, ora di rete ed export diagnostico USB per le segnalazioni.
- 🔄 **Aggiornamenti OTA:** installa le nuove versioni dal browser.
- 💡 **LED di stato** sulle schede ESP32-S3.

## A Chi è Rivolto

- **Utenti smart home** che vogliono un UPS remoto in Home Assistant quando l'UPS è lontano dal server.
- **Appassionati di homelab** che vogliono monitorare carico, batteria ed eventi di alimentazione di uno o più UPS in rete, con poco hardware.

## Come Usare Questo Manuale

La prima volta segui i capitoli in ordine:

1. **[Requisiti Hardware](hardware.md):** la scheda, i cavi e l'UPS.
2. **[Installazione](installation.md):** installa il firmware e tienilo aggiornato.
3. **[Configurazione Iniziale](configuration.md):** collega la scheda alla rete e proteggi l'interfaccia web.
4. **[Interfaccia Web](web-interface.md):** a cosa serve ogni pagina.
5. **[Integrazione Client](integration.md):** aggiungi l'UPS a Home Assistant, a un NAS o a un client Linux.

I capitoli **[LED di Stato](status-led.md)**, **[Risoluzione Problemi](troubleshooting.md)** e **[FAQ](faq.md)** aiutano quando qualcosa non va come previsto.

## Link Utili

- [Repository GitHub](https://github.com/maverick1982/esp32-nut): codice sorgente, release e l'elenco completo degli UPS provati.
- [Web Installer](https://maverick1982.github.io/esp32-nut/): installa il firmware dal browser.
- [Issue Tracker](https://github.com/maverick1982/esp32-nut/issues): segnala un bug, chiedi una funzionalità o invia un report di compatibilità del tuo UPS.
