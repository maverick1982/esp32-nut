---
title: "Installazione"
type: manual
tags: [user-manual]
---

# Installazione del Firmware

Ci sono tre modi per installare ESP32-NUT: il web installer (consigliato), lo zip della release con `esptool`, oppure la compilazione dai sorgenti con PlatformIO. Gli aggiornamenti successivi si installano in rete dall'interfaccia web (OTA).

## Metodo A: Web Installer (Consigliato)

Nessun programma da installare: è il browser a programmare la scheda.

> [!IMPORTANT]
> Il web installer richiede un browser basato su Chromium (**Google Chrome, Microsoft Edge, Brave**). Firefox e Safari non supportano l'API Web Serial che usa.

1. Collega la scheda al computer con un cavo dati: la porta `COM` (o `UART`) su una ESP32-S3, la porta USB-C su una ESP32-P4.
2. Apri il [**Web Installer di ESP32-NUT**](https://maverick1982.github.io/esp32-nut/).
3. Premi il pulsante della tua scheda:
   - il pulsante principale per una **ESP32-S3**;
   - nella sezione **ESP32-P4**, il pulsante della revisione del tuo chip, **v3.1 or later** o **before v3** (vedi [Revisione del chip](hardware.md#revisione-del-chip-due-immagini)).
4. Scegli la porta seriale della scheda (spesso si chiama "USB to UART Bridge", "USB Serial" o "USB JTAG/serial debug unit").
5. Quando l'installer chiede **"Do you want to erase the device before installing?"**:
   - lascia **"Erase device"** deselezionato per conservare le impostazioni Wi-Fi, NUT e del login web di una scheda che usa già ESP32-NUT;
   - selezionalo per un'installazione pulita. Una ESP32-S3 riparte allora come access point di configurazione `NUT_ESP32_Config`.
6. Attendi la fine dell'installazione: la scheda si riavvia da sola.

L'installer installa sempre l'ultima release.

## Metodo B: Zip della Release ed esptool

Ogni [release](https://github.com/maverick1982/esp32-nut/releases) ha uno zip per scheda:

| Zip | Scheda |
| :--- | :--- |
| `esp32-nut-vX.Y.Z.zip` | ESP32-S3 |
| `esp32-nut-vX.Y.Z-esp32-p4-eth.zip` | ESP32-P4 v3.1 o successiva |
| `esp32-nut-vX.Y.Z-esp32-p4-eth-v1.zip` | ESP32-P4 prima della v3 |

Dentro, `firmware.factory.bin` è l'immagine completa da scrivere all'indirizzo `0x0`:

```bash
esptool write-flash 0x0 firmware.factory.bin
```

Con esptool v4 il comando è `esptool.py write_flash 0x0 firmware.factory.bin`. Il file `README.txt` nello zip spiega gli altri file.

## Metodo C: Compilazione con PlatformIO (Avanzato)

Per modificare il codice o provare un branch di sviluppo:

1. Installa [Visual Studio Code](https://code.visualstudio.com/) con l'estensione **PlatformIO IDE**, e Git.
2. Clona il repository:
   ```bash
   git clone https://github.com/maverick1982/esp32-nut.git
   ```
3. Apri la cartella `esp32-nut` in Visual Studio Code. PlatformIO scarica toolchain e librerie.
4. Scegli l'ambiente della tua scheda:
   - `esp32-s3-standard` (ESP32-S3);
   - `esp32-p4-eth` (ESP32-P4 v3.1 o successiva);
   - `esp32-p4-eth-v1` (ESP32-P4 prima della v3).
5. Collega la scheda e premi **Upload**. L'interfaccia web è inclusa nel firmware, quindi non c'è un'immagine del file system separata da caricare.

## Aggiornare il Firmware (OTA)

Quando la scheda usa già ESP32-NUT, puoi aggiornarla dal browser:

1. Scarica lo zip della tua scheda dalla pagina delle [release](https://github.com/maverick1982/esp32-nut/releases).
2. Nell'interfaccia web, apri **Firmware OTA** nella barra laterale.
3. Trascina lo zip, o il `firmware.bin` che contiene, nell'area di caricamento, oppure fai clic per scegliere il file.
4. Attendi la fine del caricamento: la scheda si riavvia con la nuova versione.

Le impostazioni vengono conservate.

> [!WARNING]
> **Aggiorni dalla v1.5.x o precedente?** Installa la v1.6.0 o successiva con il web installer, non via OTA. La v1.6 ha aggiunto una partizione della flash (`coredump`) che conserva i dettagli dei crash, e un aggiornamento OTA non può cambiare la tabella delle partizioni. Lascia **"Erase device"** deselezionato per conservare le impostazioni. Dopo questa installazione una tantum, l'OTA torna a funzionare.

---

Prossimo passo: la [Configurazione Iniziale](configuration.md).
