---
title: "Installazione"
type: manual
tags: [user-manual]
---

# Installazione del Firmware

L'installazione del firmware ESP32-NUT sulla tua scheda ESP32-S3 può essere eseguita in due modi: un'installazione rapida tramite browser web (consigliata per la maggior parte degli utenti) o un'installazione manuale tramite PlatformIO per gli utenti più avanzati.

## Metodo A: Installazione rapida via Browser (Consigliato)

Questo è il metodo più semplice e veloce. Non richiede lo scaricamento di alcun software o codice sorgente sul tuo computer.

> [!IMPORTANT]
> L'installazione via Web richiede l'utilizzo di un browser basato su Chromium, come **Google Chrome, Microsoft Edge o Brave**. Firefox e Safari non supportano attualmente le API Web Serial necessarie.

1. Collega la porta `COM` (o `UART`) della tua scheda ESP32-S3 a una porta USB del tuo computer utilizzando un cavo dati.
2. Vai alla pagina di installazione ufficiale: <a href="https://maverick1982.github.io/esp32-nut/" target="_blank">**Web Installer ESP32-NUT**</a>.
3. Clicca sul pulsante **"Connect"** (o "Install").
4. Il browser ti chiederà di selezionare una porta seriale. Seleziona quella corrispondente al tuo ESP32 (potrebbe chiamarsi "USB to UART Bridge" o "USB Serial").
5. Segui le istruzioni a schermo per avviare il flashing. Il processo cancellerà automaticamente i dati precedenti (Erase Flash) e installerà l'ultima versione stabile del firmware.
6. Al termine, la scheda si riavvierà automaticamente.

## Metodo B: Compilazione manuale tramite PlatformIO (Avanzato)

Se desideri apportare modifiche al codice, testare versioni in via di sviluppo (branch specifici) o semplicemente preferisci compilare il firmware da zero, puoi utilizzare PlatformIO.

### Prerequisiti
- [Visual Studio Code](https://code.visualstudio.com/) installato.
- Estensione **PlatformIO IDE** installata su VSCode.
- Git (opzionale, ma consigliato per clonare il repository).

### Procedura
1. Clona il repository ufficiale sul tuo computer:
   ```bash
   git clone https://github.com/maverick1982/esp32-nut.git
   ```
2. Apri la cartella `esp32-nut` appena scaricata con Visual Studio Code. PlatformIO rileverà automaticamente il progetto e scaricherà le dipendenze necessarie.
3. Collega la scheda ESP32-S3 al computer tramite la porta `COM` (UART).
4. Nella barra inferiore di PlatformIO (o dal pannello laterale a forma di formica aliena), clicca sul pulsante di spunta **"Build"** (per compilare il codice e verificare che non ci siano errori).
5. Successivamente, clicca sulla freccia verso destra **"Upload"** per caricare il firmware compilato sulla scheda.
6. (Opzionale) Clicca sull'icona a forma di presa elettrica **"Upload File System image"** se hai apportato modifiche ai file della Web UI nella cartella `data/` (sebbene il firmware principale generalmente contenga già le risorse compresse).

---

Indipendentemente dal metodo scelto, una volta installato il firmware, il passo successivo è procedere alla [Configurazione Iniziale](configuration.md) per connettere il dispositivo al Wi-Fi e all'UPS.
