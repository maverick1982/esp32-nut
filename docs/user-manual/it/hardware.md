---
title: "Requisiti Hardware"
type: manual
tags: [user-manual]
---

# Requisiti Hardware

Questo progetto è progettato specificamente per sfruttare le capacità USB host native dei microcontrollori Espressif. Pertanto, la scelta dell'hardware è cruciale.

## Scheda Supportata: ESP32-S3

Per utilizzare ESP32-NUT **è strettamente necessaria una scheda di sviluppo ESP32-S3**. 

> [!WARNING]
> Le normali schede ESP32 (es. ESP32-WROOM-32), ESP8266 o ESP32-C3 **non funzioneranno**, in quanto non dispongono del controller USB OTG (On-The-Go) nativo necessario per comunicare direttamente con l'UPS.

## Cablaggio e Alimentazione

La configurazione più comune e consigliata prevede l'utilizzo di una generica scheda di sviluppo ESP32-S3 dotata di due porte USB-C (tipicamente etichettate come `COM`/`UART` e `USB`).

Per ottenere una configurazione pulita senza dover saldare cavi esterni ai pin GPIO, è sufficiente ponticellare alcuni pad presenti sul retro della scheda:

1. **Abilitare la modalità Host (Pad "USB-OTG")**: Individua i due piccoli pad di saldatura sul retro della scheda etichettati come `USB-OTG`. Uniscili con una goccia di stagno. Questo passaggio instrada l'alimentazione a 5V verso la porta `USB`, permettendole di agire come "Host" e di alimentare l'interfaccia USB dell'UPS.
2. **Abilitare il LED di Stato (Pad "RGB" - Opzionale)**: Se la tua scheda ha un LED RGB integrato (es. WS2812), individua i pad etichettati `RGB` sul retro e ponticellali. Questo abiliterà il feedback visivo dello stato del sistema.

### Collegamenti Finali

Una volta preparata la scheda, effettua i collegamenti in questo modo:

- **Alimentazione:** Collega un normale caricabatterie da muro USB alla porta etichettata `COM` (o `UART`). Questa porta fornirà l'alimentazione principale all'ESP32.
- **Dati (UPS):** Collega un adattatore **USB-C OTG** alla porta etichettata `USB`, e inserisci in questo adattatore il cavo USB proveniente dal tuo UPS.

## Case Stampato in 3D (Opzionale)

Se hai a disposizione una stampante 3D, puoi trasformare la tua nuda scheda ESP32-S3 in un prodotto rifinito. Abbiamo progettato un case compatto e su misura per questo progetto.

Puoi scaricare gratuitamente i file STL/3MF pronti per la stampa dai seguenti link:
- [**Printables**](https://www.printables.com/model/1794471-case-esp32-nut-server-bridge)
- [**Thingiverse**](https://www.thingiverse.com/thing:7389257)
