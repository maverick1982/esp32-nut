# Eaton 3S - HID Report Mapping (Serie 700/850)

Dall'analisi esplorativa dell'albero HID generato direttamente dal driver NUT (`usbhid-ups -x explore`), abbiamo estratto la mappatura ufficiale e completa per gli UPS della serie **Eaton 3S** (VID: `0x0463`, PID: `0xFFFF`).

Nei payload USB, il byte all'indice `[0]` corrisponde sempre al **Report ID**. L'offset indicato si riferisce ai bit successivi al Report ID (es. Offset 0 = `data[1]`, Offset 8 = `data[2]`). I valori multi-byte sono trasmessi in formato Little Endian.

## Mappatura dei Report Principali

### Report `0x01`: Stato di Alimentazione e Allarmi (PresentStatus)
Contiene i flag di stato dell'UPS mappati a livello di singolo bit nell'offset 0 (`data[1]`) e byte successivi:
- `data[1]` (Offset 0, Size 1): `ACPresent` (0 = Assente, 1 = Rete Elettrica Presente / Online)
- `data[1]` (Offset 1, Size 1): `BelowRemainingCapacityLimit` (Batteria sotto la soglia critica)
- `data[1]` (Offset 2, Size 1): `Charging` (Batteria in carica)
- `data[1]` (Offset 3, Size 1): `CommunicationLost`
- `data[1]` (Offset 4, Size 1): `Discharging` (In funzione a batteria / On Battery)
- `data[1]` (Offset 5, Size 1): `Good` (Stato generale OK)
- `data[1]` (Offset 6, Size 1): `InternalFailure` (Guasto interno)
- `data[1]` (Offset 7, Size 1): `NeedReplacement` (Batteria da sostituire)
- `data[2]` (Offset 8, Size 8): `Overload` (Sovraccarico)
- `data[3]` (Offset 16, Size 8): `ShutdownImminent` (Spegnimento imminente)

### Report `0x02`: Stato Prese (Switch On/Off)
- `data[1]` (Offset 0, Size 8): `Outlet.[1].SwitchOn/Off` (Stato gruppo prese 1)
- `data[2]` (Offset 8, Size 8): `Outlet.[2].SwitchOn/Off` (Stato gruppo prese 2)

### Report `0x06`: Autonomia e Carica Batteria (PowerSummary)
- `data[1]` (Offset 0, Size 8): `RemainingCapacity` (Percentuale di carica residua della batteria, 0-100%).
- `data[2]`-`data[5]` (Offset 8, Size 32): `RunTimeToEmpty` (Autonomia residua in secondi). Valore intero a 32-bit. *Es: `20 0D 00 00` -> `0x00000D20` = 3360 s.*

### Report `0x08` e `0x0c`: Info Capacità
- Report `0x08`, `data[1]` (Offset 0, Size 8): `RemainingCapacityLimit` (Soglia batteria scarica, es. 20%).
- Report `0x0c`, `data[5]` (Offset 32, Size 8): `DesignCapacity` (es. 100).
- Report `0x0c`, `data[6]` (Offset 40, Size 8): `FullChargeCapacity` (es. 100).

### Report `0x0d` e `0x12`: Dati Nominali (Rating & Config)
- Report `0x0d`, `data[1]`-`data[2]` (Offset 0, Size 16): `ConfigApparentPower` (Potenza nominale in VA, es. 700 o 850).
- Report `0x0d`, `data[3]` (Offset 16, Size 8): `ConfigFrequency` (Frequenza di lavoro, es. 50 Hz).
- Report `0x12`, `data[1]` (Offset 0, Size 8): `ConfigVoltage` (Tensione configurata, es. 230 V).

### Report `0x0e`: Tensione in Uscita
- `data[1]`-`data[2]` (Offset 0, Size 16): `Output.Voltage` (Tensione RMS di uscita istantanea in Volt, es. `E6 00` = 230 V).

### Report `0x13` e `0x14`: Soglie di Intervento
- Report `0x13`, `data[1]`-`data[2]` (Offset 0, Size 16): `HighVoltageTransfer` (Soglia alta tensione, es. 264 V).
- Report `0x14`, `data[1]` (Offset 0, Size 8): `LowVoltageTransfer` (Soglia bassa tensione, es. 161 V).

## Note Hardware (ESP32-S3)
L'Eaton 3S 700 (e i dispositivi HID Low-Speed in generale) necessitano fisicamente dei **5V (VBUS)** attivi da parte dell'Host (ESP32). Senza questa tensione, l'UPS non applica la resistenza di pull-up sul pin dati D- e la periferica non viene in alcun modo rilevata. Nelle board in cui la porta Type-C OTG è bloccata da un diodo, è necessario un bypass hardware (es. saldatura diretta) per alimentare l'UPS.
