---
title: "Introduzione"
type: manual
tags: [user-manual]
---

# Introduzione

Benvenuto nel Manuale Utente di ESP32-NUT. Questo documento fornisce linee guida e istruzioni complete per l'installazione, la configurazione e l'integrazione del sistema ESP32-NUT.

## Che cos'è ESP32-NUT?
ESP32-NUT è un dispositivo bridge open-source progettato per trasformare un normale Gruppo di Continuità (UPS) USB in un dispositivo di rete intelligente. Sfrutta il microcontrollore ESP32-S3 per agire da USB Host, comunicare con l'UPS ed esporre i suoi dati via Wi-Fi utilizzando il protocollo standard Network UPS Tools (NUT). Questo elimina la necessità di avere un PC dedicato o un Raspberry Pi sempre acceso vicino all'UPS.

## Funzionalità Principali
- 🔌 **Plug & Play USB Host:** Supporto diretto per gli UPS USB standard (es. CyberPower, APC) tramite la porta USB nativa dell'ESP32-S3.
- 🌐 **Connettività Wi-Fi & Captive Portal:** Configurazione iniziale facilissima tramite smartphone o computer, senza bisogno di scrivere codice.
- 🔗 **Server NUT Integrato:** Compatibilità nativa al 100% con client esterni come Home Assistant, pfSense, TrueNAS e Synology.
- 💡 **Diagnostica LED:** Feedback visivo immediato sullo stato della connessione di rete e dell'UPS.

## A Chi è Rivolto?
Questo progetto è pensato per:
- **Utenti Smart Home:** Ideale per integrare un UPS remoto in Home Assistant quando l'UPS è fisicamente lontano dal server principale.
- **Appassionati di Homelab:** Perfetto per monitorare i consumi, il carico e lo stato della batteria tramite la rete locale con un ingombro hardware minimo.

## Come Leggere Questo Manuale
Per iniziare, ti consigliamo di seguire i capitoli nell'ordine seguente:

1. **Requisiti Hardware:** Assicurati di avere la scheda e i cavi corretti.
2. **Installazione:** Carica il firmware sull'ESP32-S3.
3. **Configurazione:** Collega il dispositivo al Wi-Fi e configura il server NUT.
4. **Integrazione Client:** Aggiungi il tuo UPS al tuo software di monitoraggio preferito.

## Link Utili
- [Repository GitHub](https://github.com/maverick1982/esp32-nut): Codice sorgente e versioni (release).
- [Issue Tracker](https://github.com/maverick1982/esp32-nut/issues): Segnala bug o richiedi nuove funzionalità.
