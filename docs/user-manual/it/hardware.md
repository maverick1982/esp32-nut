---
title: "Requisiti Hardware"
type: manual
tags: [user-manual]
---

# Requisiti Hardware

ESP32-NUT usa il controller USB host integrato in alcuni chip Espressif per parlare con l'UPS. La scelta della scheda è importante.

## Schede Supportate

| Scheda | Rete | LED di stato | Note |
| :--- | :--- | :--- | :--- |
| Scheda di sviluppo **ESP32-S3** (es. ESP32-S3 DevKitC-1 e compatibili con due porte USB-C) | Wi-Fi | Sì (LED RGB su GPIO 48, se presente) | La configurazione più comune. |
| **Waveshare ESP32-P4-ETH** (anche la versione PoE) | Ethernet | No | UPS sulla porta USB 2.0 High-Speed. |
| **Waveshare ESP32-P4-WIFI6-POE-ETH** | Ethernet | No | Il chip Wi-Fi ESP32-C6 non viene usato: la scheda lavora in Ethernet. |
| Altre schede ESP32-P4 che seguono il design di riferimento Espressif (PHY IP101) | Ethernet | No | Una scheda cablata diversamente richiede un profilo dedicato: apri una issue. |

> [!WARNING]
> Le schede ESP32 standard (es. ESP32-WROOM-32), ESP32-C3 ed ESP8266 **non funzionano**: non hanno un controller USB host.

## ESP32-S3: Collegamenti e Alimentazione

Le schede ESP32-S3 generiche con due porte USB-C (etichettate `COM`/`UART` e `USB`) richiedono una piccola preparazione, senza fili sui pin GPIO:

1. **Abilita la modalità host (piazzole "USB-OTG"):** sul retro della scheda, unisci con una goccia di stagno le due piazzole etichettate `USB-OTG`. Così la porta `USB` riceve i 5 V e può alimentare l'interfaccia USB dell'UPS.
2. **Abilita il LED di stato (piazzole "RGB", opzionale):** se la scheda ha un LED RGB, unisci le piazzole `RGB` per accenderlo.

Poi collega:

- **Alimentazione:** un alimentatore USB da muro alla porta `COM` (o `UART`).
- **UPS:** un adattatore **USB-C OTG** nella porta `USB`, e nell'adattatore il cavo USB dell'UPS.

## ESP32-P4: Collegamenti e Alimentazione

- **UPS:** sulla porta USB 2.0 High-Speed (sulla Waveshare ESP32-P4-ETH, il connettore a 4 pin J3).
- **Rete:** un cavo Ethernet. La scheda riceve l'indirizzo dal router (DHCP). Non c'è Wi-Fi né access point di configurazione.
- **Alimentazione:** dalla porta USB-C della scheda, oppure in PoE sulle versioni PoE.

### Revisione del chip: due immagini

L'ESP32-P4 esiste in diverse revisioni del chip, e ognuna richiede la propria immagine del firmware:

| Immagine | Revisione del chip | Prove su hardware |
| :--- | :--- | :--- |
| ESP32-P4 v3.1 o successiva | v3.1 e successive (produzione attuale) | Provata su una scheda v3.2, con un UPS |
| ESP32-P4 prima della v3 | v1.x (prime schede) | Provata su una scheda v1.3, con un UPS |

Per conoscere la revisione esegui `esptool chip-id` (`esptool.py chip_id` con esptool v4): l'output riporta per esempio `Chip is ESP32-P4 (revision v1.3)`. La mostra anche il log seriale di avvio, come `chip revision: v1.3`. Le revisioni v2.x e v3.0 non sono supportate da nessuna delle due immagini.

Un'immagine per la revisione sbagliata viene rifiutata, dal bootloader in un'installazione completa e dalla scheda in un aggiornamento OTA: basta installare l'altra.

## Hub USB e Prolunghe Attive

Un cavo USB lungo con amplificatore integrato contiene un hub USB.

- **ESP32-P4:** sulla porta High-Speed un hub high-speed nasconde l'UPS (un dispositivo full-speed): lo stack USB host non raggiunge un dispositivo full-speed dietro un hub high-speed. Usa un cavo passivo, oppure metti un isolatore USB solo full-speed (basato su ADuM3160) tra la scheda e il cavo.
- **ESP32-S3:** la sua porta USB è solo full-speed, quindi un cavo del genere dovrebbe funzionare anche senza isolatore (non provato).

## L'UPS

ESP32-NUT supporta gli UPS che usano la classe standard **USB HID Power Device**, quelli che NUT gestisce con il driver `usbhid-ups`. Sono provati molti modelli di Eaton/MGE, APC, CyberPower, Powercom, Legrand, Powerwalker, WalleCube e altri: l'elenco completo è nel [README](https://github.com/maverick1982/esp32-nut/blob/main/README.it.md). Un UPS non riconosciuto usa un driver generico che legge i valori standard.

Gli UPS che comunicano tramite un convertitore USB-seriale o un protocollo seriale (Megatec/Q1, USB CDC, RS-232 con cavo RJ45) non sono supportati.

L'UPS deve avere una vera porta USB (di solito di tipo B, quella quadrata delle stampanti).

## Case Stampato in 3D (Opzionale)

Un case compatto per la scheda ESP32-S3 è disponibile gratuitamente:

- [**Printables**](https://www.printables.com/model/1794471-case-esp32-nut-server-bridge)
- [**Thingiverse**](https://www.thingiverse.com/thing:7389257)

I modelli sorgente e i file CAD sono nella cartella [`hardware/case`](https://github.com/maverick1982/esp32-nut/tree/main/hardware/case) del repository.
