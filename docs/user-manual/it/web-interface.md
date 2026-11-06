---
title: "Interfaccia Web"
type: manual
tags: [user-manual]
---

# Interfaccia Web

Apri l'indirizzo della scheda in un browser ed esegui l'accesso (vedi [Configurazione Iniziale](configuration.md#login-dellinterfaccia-web)). La barra laterale elenca le pagine descritte qui sotto. L'interfaccia è in inglese: i nomi di pagine e pulsanti sono riportati come compaiono.

## Barra Laterale e Riquadro di Stato

La barra laterale mostra:

- la versione del firmware;
- lo stato del collegamento di rete (Wi-Fi o Ethernet) e dell'UPS;
- l'utente collegato, con **Log out** e **Log out of all devices**.

In cima alla pagina compare un riquadro giallo quando qualcosa richiede attenzione:

- l'UPS non risponde e i suoi dati sono vecchi (stale);
- l'ultimo riavvio automatico e la sua causa, per esempio `too many USB port power cycles in an hour`;
- l'ultimo crash o reset del watchdog, con il task che ha smesso di rispondere;
- la modalità degradata, quando i riavvii automatici sono sospesi.

Vedi [Risoluzione Problemi](troubleshooting.md#il-riquadro-giallo-riavvii-automatici) per il significato di ogni messaggio.

## UPS Telemetry

I dati in tempo reale dell'UPS:

- **UPS Status** (per esempio `OL` in rete, `OB` a batteria, `LB` batteria scarica);
- **Battery Charge**, **UPS Load** e **Real Power**;
- una tabella con tutte le variabili che l'UPS riporta, il loro valore e la descrizione NUT.

Se l'UPS non è riconosciuto e usa il driver generico, un avviso chiede di esportare la diagnostica USB e aprire una issue (vedi [Risoluzione Problemi](troubleshooting.md#ups-riconosciuto-ma-mancano-dei-valori)).

## UPS Commands

I comandi supportati dall'UPS, come con il driver `usbhid-ups` di NUT:

- **Tests & beeper:** test della batteria e del pannello e comandi del cicalino. In cima è mostrato il risultato dell'ultimo test della batteria.
- **Power control:** comandi di carico e spegnimento (`load.off`, `shutdown.return`, `shutdown.stayoff`, …). Chiedono prima una conferma.

L'elenco dipende dal modello di UPS. Senza UPS collegato la pagina lo segnala.

## UPS Settings

I parametri dell'UPS che puoi modificare (`upsrw` in NUT), con i valori attuali: per esempio soglie di batteria scarica, tensioni di commutazione, data della batteria e ritardi di spegnimento.

- I valori vengono letti dall'UPS all'apertura della pagina e dopo ogni **Apply**. Le modifiche hanno effetto subito.
- I parametri che commutano prese, bypass o modalità ECO chiedono prima una conferma.
- I valori conservati sulla scheda, come `ups.delay.*` e i nomi delle prese, hanno un pulsante **Reset to default**.
- Una variabile con un valore personalizzato (vedi [NUT Server](#nut-server)) qui è in sola lettura, con un link alla pagina NUT Server.

## Network

Una sezione per ogni collegamento di rete della scheda:

- **Wi-Fi** (ESP32-S3): stato della connessione, nome della rete (SSID), segnale, indirizzo IP, gateway, indirizzo MAC e DHCP. **Connection Setup** cambia la rete Wi-Fi: scrivi il nome della rete e la sua password, poi premi **Initialize Connection**. La scheda salva le impostazioni e si riavvia.
- **Ethernet** (ESP32-P4): stato della connessione, velocità del collegamento, cavo, indirizzo IP, gateway, indirizzo MAC e DHCP.
- **Hostname:** il nome che la scheda comunica al router, sia in Wi-Fi sia in Ethernet. Usa da 1 a 32 lettere, cifre e trattini, senza trattino all'inizio o alla fine. Lascialo vuoto per usare il predefinito `esp32-nut-xxxxxx`. Premi **Save Hostname**: la scheda si riavvia con il nuovo nome.

Quando la scheda smette di rispondere, gli ultimi valori restano sulla pagina in grigio, con "No answer for N s".

## NUT Server

### NUT Server Credentials

L'**UPS Name** e lo **Username** e la **Password** NUT usati dai client NUT. Vedi [Impostazioni del Server NUT](configuration.md#impostazioni-del-server-nut). Il salvataggio riavvia la scheda.

### Custom variables

Valori che i client NUT vedono al posto di quelli riportati dall'UPS, o come variabili in più quando l'UPS non le riporta. Funzionano come `override.*` nel file `ups.conf` di NUT. Esempi:

- impostare `ups.model` con il nome reale del modello quando l'UPS ne riporta uno sbagliato;
- aggiungere `battery.date` con la data sull'etichetta della batteria;
- fissare `ups.mfr`, `ups.model` e `ups.serial`, così Home Assistant conserva lo stesso dispositivo e il suo storico quando sostituisci l'UPS.

Come funzionano:

- Fino a 16 variabili, ognuna con **Key**, **Value** e una **Description** opzionale. Valore e descrizione accettano ASCII stampabile, fino a 64 caratteri.
- Le vedono solo i client NUT. La dashboard, lo spegnimento automatico e il resto della scheda continuano a usare i valori reali dell'UPS.
- I client NUT vedono anche `driver.parameter.override.<variabile>`, come in NUT.
- Un valore personalizzato di `ups.mfr`, `ups.model` o `ups.serial` vale anche per `device.mfr`, `device.model` e `device.serial`.
- Una variabile scrivibile con un valore personalizzato diventa di sola lettura per `upsrw` e nella pagina UPS Settings.
- `ups.status`, `ups.status.*`, `device.type` e `driver.parameter.*` non si possono impostare.
- Senza UPS collegato, i client NUT ricevono `ERR DATA-STALE` come sempre, variabili personalizzate comprese.

Le modifiche vengono salvate sulla scheda e applicate subito, senza riavvio. Restano dopo i riavvii e gli aggiornamenti OTA.

## Board Settings

Impostazioni della scheda stessa, conservate dopo un riavvio. La prima riga mostra il modello della scheda.

### Status LED (ESP32-S3)

Spegni il LED, o scegli la luminosità da 1 a 5. Le modifiche si applicano subito. Da spento, il LED resta buio in ogni stato. Vedi [LED di Stato](status-led.md).

### Automatic shutdown

**Shut down the UPS on low battery**, disattivato di default. Con l'opzione attiva, quando l'UPS è a batteria e segnala batteria scarica (`OB LB`), la scheda invia una volta `shutdown.default` all'UPS, anche senza client NUT collegati. Non lo invia di nuovo finché l'UPS non torna in rete (`OL`).

- Serve per gli UPS che alimentano dispositivi senza client NUT, come una TV o un impianto audio.
- La soglia è quella dell'UPS, `battery.charge.low` / `battery.runtime.low`, nella pagina UPS Settings.
- Il comando e il suo esito compaiono nel System Log come `[CMD] Automatic shutdown: ...`.
- Se l'UPS non ha un comando di spegnimento, o non c'è un UPS collegato, l'interruttore è disattivato e la pagina spiega perché.
- Se l'UPS riaccende le prese al ritorno della corrente dipende dal modello.

È un'impostazione della scheda, non una variabile dell'UPS: i client NUT non la vedono e non possono cambiarla, e `upsmon` funziona come prima.

### Time

- **Get the time from an NTP server** (attivo di default), il **Server** (predefinito `pool.ntp.org`) e il **Time zone**, da un elenco con ora legale automatica (predefinito UTC).
- La sezione mostra se l'ora è sincronizzata e l'ora della scheda.
- Le modifiche si applicano subito, senza riavvio.

L'ora serve solo a mostrarla e a scriverla nei log: login, sessioni e timeout non dipendono da essa. Senza accesso a Internet la scheda funziona come prima, e le righe di log non hanno l'ora.

## Web Login

Cambia nome utente e password dell'interfaccia web. Vedi [Cambia le credenziali](configuration.md#cambia-le-credenziali). In modalità access point la pagina mostra **Reset credentials**, che non richiede la password attuale.

## System Logs

Il System Log della scheda, in un terminale:

- **Auto-refresh** lo tiene aggiornato; **Refresh** lo legge una volta; **Clear** svuota la vista.
- **Export** scarica il log in un file.
- **Export USB Diagnostics** scarica `usb_diagnostics.json`, con il descrittore USB dell'UPS, i report a cui ha risposto e lo stato del collegamento USB. Prima del download la scheda legge una volta tutti i report FEATURE dell'UPS, anche quelli che non usa per NUT, così un solo file dà un quadro completo. Allegalo quando segnali un problema con un UPS.

Quando la scheda ha l'ora di rete, ogni riga mostra l'ora della scheda. Altrimenti il terminale usa l'ora del browser.

La scheda tiene in memoria solo le righe più recenti, e un riavvio le cancella. Per conservare uno storico più lungo usa il log remoto.

### Remote logging (syslog)

Invia una copia del System Log a un server syslog via UDP. È disattivato di default.

- **Server** e **UDP port** (predefinita 514).
- **Format:** RFC 3164 (predefinito) o RFC 5424.
- **Facility** (predefinita `local0`), **Minimum level** (predefinito INFO) e **Max lines per second** (predefinito 10).

Il pannello mostra lo stato e quante righe sono state inviate, fallite, limitate o scartate. Anche l'indicatore **Syslog** accanto ad Auto-refresh mostra lo stato.

- Le righe vengono inviate da un task in background: non rallentano mai l'UPS né il server NUT.
- Le righe scritte prima che la rete sia pronta vengono inviate appena la connessione è attiva. In modalità access point non viene inviato nulla.
- Con l'ora sincronizzata, ogni riga porta l'ora in cui è stata scritta. Le righe RFC 5424 portano sempre il nome host; quelle RFC 3164 lo portano quando l'ora è sincronizzata.
- Le impostazioni vengono salvate sulla scheda e si applicano senza riavvio.

> [!WARNING]
> Il syslog su UDP non è cifrato e non ha conferma di consegna. Chiunque sulla rete può leggere le righe, compresi i nomi utente dei login falliti. Se il server si ferma mentre la rete è attiva, lo stato può continuare a mostrarlo come attivo.

## Firmware OTA

Apre la pagina di aggiornamento: trascina lo zip della release della tua scheda, o il suo `firmware.bin`, nell'area di caricamento. Vedi [Aggiornare il Firmware](installation.md#aggiornare-il-firmware-ota).
