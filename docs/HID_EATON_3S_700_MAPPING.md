# Eaton 3S 700 - HID Report Mapping

Dall'analisi dei dati grezzi (raw payload) inviati dall'UPS tramite la classe USB HID Power Device, sono state individuate le seguenti mappature specifiche per il modello **Eaton 3S 700** (VID: `0x0463`, PID: `0xFFFF`).

## Report Identificati

- **Report `0x02`**: Stato Operativo (Status)
  - `data[1]`: Stato UPS. Valore `0x01` indica `OL` (Online). Valore `0x02` indica `OB` (On Battery).

- **Report `0x06`**: Batteria e Autonomia (Battery & Runtime)
  - `data[1]`: Percentuale di carica della batteria (es. `0x64` = 100%).
  - `data[2]` - `data[5]`: Autonomia residua (Runtime) stimata in secondi. Valore Intero a 32 bit (Little Endian). Esempio: `20 0D 00 00` -> `0x00000D20` = 3360 secondi.

- **Report `0x0E`**: Tensione di Ingresso (Input Voltage)
  - `data[1]` - `data[2]`: Tensione RMS in Volt. Valore Intero a 16 bit (Little Endian). Esempio: `E6 00` -> `0x00E6` = 230 V.

- **Report `0x0D`**: Dati Nominali UPS (Rating)
  - `data[1]` - `data[2]`: Potenza in VA. Little Endian. Esempio: `BC 02` -> `0x02BC` = 700 VA.
  - `data[3]`: Frequenza nominale. Esempio: `0x32` = 50 Hz.

## Note Hardware (ESP32-S3)
L'Eaton 3S 700 (e i dispositivi HID Low-Speed in generale) necessitano fisicamente dei **5V (VBUS)** attivi da parte dell'Host (ESP32). Senza questa tensione, l'UPS non applica la resistenza di pull-up sul pin dati D- e la periferica non viene in alcun modo rilevata. Nelle board in cui la porta Type-C OTG è bloccata da un diodo, è necessario un bypass hardware (es. saldatura diretta) per alimentare l'UPS.
