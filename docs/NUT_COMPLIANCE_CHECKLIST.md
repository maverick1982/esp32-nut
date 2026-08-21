# Linee Guida per la Conformità dei Driver ESP32-NUT rispetto a NUT Ufficiale

Questo documento raccoglie la checklist e le best practice per garantire che i driver scritti per **ESP32-NUT** (es. `GenericDriver`, `EatonDriver`, ecc.) mantengano la totale aderenza comportamentale con i sorgenti ufficiali del progetto **Network UPS Tools (NUT)**. 

Deve essere utilizzato come riferimento per la validazione di un nuovo driver o per l'audit di regressioni su driver esistenti.

---

## 1. Mappatura dei Parametri (Naming Convention)
Ogni variabile esposta dall'API JSON e dal server NUT nativo di ESP32 deve rispettare scrupolosamente la nomenclatura ufficiale di NUT (vedi `nut_repo/docs/nut-names.txt`).
*   **EVITARE nomi arbitrari**: Non inventare variabili come `battery.charge.full`. Se la direttiva HID corrisponde alla capacità totale raggiunta a piena carica, il nome ufficiale imposto da NUT (es. nel driver `idowell-hid`) è `battery.capacity.full`.
*   **Mappature per Brand**: Controllare **sempre** il file subdriver specifico in NUT (es. `mge-hid.c` per Eaton, `cps-hid.c` per CyberPower) per accertarsi che il parametro HID che si vuole estrarre sia effettivamente esportato da NUT per quel brand. Se NUT lo ignora intenzionalmente (es. `FullChargeCapacity` in `mge-hid.c`), anche il driver ESP32 corrispondente dovrebbe ignorarlo.

## 2. Unità di Misura (Scaling ed Esponenti)
I dispositivi USB HID possono fornire potenze, capacità e voltaggi con unità di misura ed esponenti variabili. L'implementazione deve affidarsi al parser HID globale in `HIDParser.cpp` per compensare queste unità, replicando la logica di `libhid` di NUT:
*   **Potenze e Voltaggi**: Le unità di Voltaggio (`0x00F0D121` o `0x0000D121`) e Watt/VA richiedono intrinsecamente una scalatura per evitare errori di x10 o x0.1. Il `HIDParser` deve sottrarre `7` all'esponente grezzo per estasiare correttamente i valori in Volt (V) e Watt (W) / VoltAmpere (VA).
*   **Tempi (`runTimeToEmpty`)**: Assicurarsi che i driver applichino la conversione del tempo scalando dinamicamente il valore grezzo (applicando `def->exponent`) per restituire il tempo in **secondi** netti.
*   **Capacità Batteria (`battery.capacity`)**: Lo standard USB HID impone che la *DesignCapacity* venga restituita in **Ampere-secondi (As)** (Unit `0x00101001`). NUT converte sistematicamente questo valore in **Ampere-ora (Ah)** dividendo per `3600.0`. I driver ESP32-NUT devono implementare `val / 3600.0` durante la mappatura.

## 3. Gestione dei Valori Mancanti / Opzionali (JSON e API)
L'assenza di un parametro HID non deve produrre valori fittizi (come `0` spuri).
*   **Comportamento di NUT (upsc)**: Se un UPS non fornisce una specifica informazione, il parametro viene semplicemente **omesso** dall'output.
*   **Comportamento Web UI (JSON)**: Nel file `web_config_server.cpp`, tutte le variabili opzionali o i valori di configurazione (come tensioni nominali, frequenze, capacità, soglie d'allarme) devono essere inseriti nel JSON **solo se il loro valore è maggiore di 0**. 
*   **Eccezioni**: Il parametro `ups.load` (o la potenza reale) può essere legittimamente `0` (0% di carico quando l'UPS è in funzione ma non alimenta nulla), pertanto non va omesso se risulta zero.

## 4. Checklist di Validazione per un Nuovo Driver
Quando si implementa un nuovo driver (es. `RielloDriver.cpp`), eseguire questi controlli incrociati con il rispettivo file NUT (`riello_usb.c` o `usbhid-ups`):

- [ ] I percorsi HID (es. `UPS.PowerSummary.Voltage`) corrispondono a quelli mappati nel driver C di NUT?
- [ ] Le eventuali lambda di conversione (es. `/ 10.0` o `* 2.0`) all'interno del driver ESP32 replicano **esattamente** le funzioni helper presenti nel driver NUT (es. `riello_battery_status`)?
- [ ] Il driver evita di estrarre valori HID che NUT sceglie di scartare per incompatibilità note con quel brand?
- [ ] Le unità di misura (Hz, V, W, %, Ah, sec) esposte nel JSON rispecchiano lo standard globale atteso dalla UI senza doppi moltiplicatori?
- [ ] I campi opzionali non supportati scompaiono dal JSON senza forzare il valore a 0?

---
*Documento generato sulla base delle analisi di aderenza a NUT del 16 Agosto 2026.*
