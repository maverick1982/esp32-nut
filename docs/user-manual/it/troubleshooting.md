---
title: "Risoluzione Problemi"
type: manual
tags: [user-manual]
---

# Risoluzione Problemi

Questo capitolo aiuta a trovare la causa dei problemi più comuni.

## Parti dai Log

1. Apri l'interfaccia web e vai su **System Logs**. Il terminale mostra cosa sta facendo il firmware: connessione di rete, enumerazione USB, errori dall'UPS, client NUT.
2. Usa **Export** per salvare il log subito dopo un problema. La scheda tiene solo le righe più recenti, e un riavvio le cancella.
3. Per conservare uno storico più lungo, o le righe scritte poco prima di un riavvio, attiva il [log remoto](web-interface.md#remote-logging-syslog) verso un server syslog.

Per i problemi USB di basso livello aiuta anche un log seriale: collega la scheda a un computer e apri un monitor seriale a 115200 baud.

## Il Riquadro Giallo: Riavvii Automatici

La scheda sorveglia il collegamento USB e lo recupera da sola quando l'UPS smette di rispondere. I passaggi vanno dal più leggero al più forte:

1. **Recupero dell'interfaccia USB**, senza scollegare l'UPS.
2. **Ciclo di alimentazione della porta USB:** quando una richiesta USB resta bloccata, la scheda spegne e riaccende la sua porta USB. L'UPS torna in pochi secondi mentre rete, interfaccia web e server NUT restano attivi. Il System Log mostra `[USB] Control transfer stuck: power-cycling the USB port (cycle N since boot)`, e la riga `[USB] Last 60 s` conta i cicli.
3. **Riavvio automatico della scheda:** se il recupero non basta, la scheda si riavvia. Il primo riavvio avviene subito, i successivi dopo 1, 5 e 15 minuti.
4. **Modalità degradata:** dopo 4 riavvii di fila senza recuperare l'UPS, la scheda smette di riavviarsi. I client NUT ricevono `ERR DATA-STALE`. Solo un nuovo collegamento dell'UPS o un riavvio manuale fanno uscire la scheda da questo stato.

Il riquadro giallo in cima all'interfaccia web mostra l'ultimo riavvio automatico e la sua causa. Le cause più comuni:

| Causa | Significato |
| :--- | :--- |
| `control transfer stuck again after the USB port power cycle` | Il ciclo della porta non ha sbloccato l'UPS, quindi la scheda si è riavviata. |
| `too many USB port power cycles in an hour` | Più di 12 cicli della porta in un'ora: la scheda si è riavviata. |
| `UPS not back after the USB port power cycle` | L'UPS non è tornato entro 30 s dal ciclo della porta. |
| `INPUT reports stopped after recovery` | L'UPS ha smesso di inviare i suoi messaggi di stato, e la scheda non legge gli stessi valori con le proprie richieste. |
| `UPS not enumerated after the automatic restart` | Dopo un riavvio l'UPS non è comparso sulla porta USB, nemmeno dopo due cicli della porta. |

Se l'UPS non torna nemmeno dopo i riavvii, il riquadro giallo chiede di **scollegare e ricollegare il cavo USB dell'UPS**.

Una riga `Last crash` o `Task watchdog: tasks not responding: ...` indica che il firmware è andato in crash o si è bloccato. Esporta subito il System Log e apri una issue allegandolo, insieme al testo del riquadro giallo.

## L'UPS Non Viene Rilevato

Se l'interfaccia web non mostra l'UPS e il LED lampeggia rosso veloce, controlla:

1. **ESP32-S3: piazzole USB-OTG.** Sulle schede generiche con due porte USB-C, le due piazzole `USB-OTG` sul retro devono essere unite con lo stagno. Senza, la porta `USB` non dà alimentazione e l'interfaccia dell'UPS resta spenta.
2. **ESP32-S3: adattatore OTG.** Usa un vero adattatore **USB OTG**. Gli adattatori solo per la ricarica non hanno la resistenza interna che dice alla scheda di fare da host.
3. **ESP32-S3: porte.** L'alimentazione va sulla porta `COM`/`UART`, l'UPS sulla porta `USB`.
4. **ESP32-P4: porta.** L'UPS va sulla porta USB 2.0 High-Speed (connettore J3 sulla Waveshare ESP32-P4-ETH).
5. **Hub e cavi amplificati.** Un cavo lungo con amplificatore integrato contiene un hub, che sull'ESP32-P4 nasconde l'UPS. Vedi [Hub USB e Prolunghe Attive](hardware.md#hub-usb-e-prolunghe-attive).
6. **Il cavo USB.** Prova un altro cavo: alcuni portano solo l'alimentazione.

## L'UPS Smette di Rispondere ogni Tanto

Alcuni modelli di UPS ogni tanto non rispondono a una richiesta USB:

- **APC Back-UPS serie BX:** il firmware APC smette di rispondere a intervalli irregolari, spesso più volte all'ora. Succede anche con NUT su Linux.
- **APC Smart-UPS:** una richiesta a volte resta bloccata nello stack USB.

La scheda si riprende da sola, di solito con un ciclo della porta USB (vedi sopra). I client NUT possono vedere i dati come vecchi per qualche secondo.

## Home Assistant Mostra l'UPS Non Disponibile per un Po'

Brevi interruzioni capitano mentre la scheda recupera il collegamento USB o si riavvia. Controlla nel riquadro giallo l'ultimo riavvio automatico, e il System Log intorno a quell'ora. Se le interruzioni si ripetono spesso, apri una issue con l'export del System Log (o le righe del syslog) e l'export della diagnostica USB.

## UPS Riconosciuto, ma Mancano dei Valori

I produttori di UPS compilano i dati USB HID in modi leggermente diversi. Se il tuo UPS usa il driver generico o mancano dei valori, il supporto si può aggiungere:

1. In **System Logs** premi **Export USB Diagnostics**. La scheda legge una volta tutti i report dell'UPS, poi scarica `usb_diagnostics.json`.
2. Apri una issue su GitHub con il modulo [UPS Compatibility Report](https://github.com/maverick1982/esp32-nut/issues/new?template=ups_report.yml). Indica marca e modello esatti e allega il file.

Lo stesso modulo è benvenuto anche quando il tuo UPS funziona bene ma non è nell'elenco dei modelli provati.

Se l'UPS riporta un valore sbagliato (per esempio un nome di modello errato), puoi sostituirlo per i client NUT con una [variabile personalizzata](web-interface.md#custom-variables).

## I Client NUT Non si Collegano

- **`ERR UNKNOWN-UPS`:** il nome UPS nel client non corrisponde all'**UPS Name** della pagina NUT Server (predefinito `ups`).
- **`ERR ACCESS-DENIED`:** username o password NUT sbagliati.
- **`ERR DATA-STALE`:** la scheda non ha dati aggiornati dall'UPS. Guarda il riquadro giallo e il System Log.
- **Connessione rifiutata:** sono già collegati 8 client. Il System Log ne elenca gli indirizzi. Un client sparito libera il suo posto dopo circa 90 secondi.
- **Nessuna risposta:** controlla l'indirizzo della scheda, e che non sia in modalità access point.

## L'Interfaccia Web Non è Raggiungibile

- Cerca la scheda nella tabella DHCP del router come `esp32-nut-xxxxxx`, o con il nome host che hai impostato.
- **ESP32-S3:** un lampeggio giallo prolungato indica che la scheda non è sul Wi-Fi. Usa il doppio reset per avviare l'access point di configurazione e controlla le impostazioni Wi-Fi (vedi [Configurazione Iniziale](configuration.md)).
- **ESP32-P4:** controlla il cavo Ethernet e i LED di collegamento della porta.
- **Password dimenticata:** vedi [Password dimenticata](configuration.md#password-dimenticata).

## L'ESP32-P4 Non Parte Dopo l'Installazione

L'immagine per la revisione sbagliata del chip viene rifiutata dal bootloader. Installa l'altra immagine ESP32-P4 (vedi [Revisione del chip](hardware.md#revisione-del-chip-due-immagini)).

## Il Mio UPS Usa una Connessione Seriale

ESP32-NUT supporta solo gli UPS che usano la classe standard USB HID Power Device (`usbhid-ups` in NUT). Gli UPS che comunicano tramite un convertitore USB-seriale (CH340, FT232, …), Megatec/Q1 o altri protocolli seriali non sono supportati.
