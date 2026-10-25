# <img src="docs/images/logo.png" width="40" height="40" align="left" style="margin-right: 15px;"> ESP32 NUT Server Bridge

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

*🇬🇧 [Read in English](README.md)*

> [!WARNING]
> **Aggiorni da una versione v1.5.x o precedente? Installa la v1.6.x o successive con il [web installer](https://maverick1982.github.io/esp32-nut/), non con un aggiornamento OTA.**
>
> La v1.6 aggiunge una nuova partizione della flash (`coredump`) che conserva i dettagli dei crash per la diagnostica. Un aggiornamento OTA non può modificare la tabella delle partizioni: la scheda funzionerebbe, ma senza quella partizione.
>
> Quando l'installer chiede **"Do you want to erase the device before installing?"**, lascia **"Erase device" deselezionato** e premi **Next**: le impostazioni Wi-Fi e NUT vengono conservate. Selezionalo solo se vuoi un'installazione pulita (la scheda ripartirà in modalità configurazione con l'access point `NUT_ESP32_Config`).
>
> Dopo questa installazione, una tantum, gli aggiornamenti successivi si possono di nuovo fare via OTA.

Un firmware open-source per ESP32-S3 che funge da **Server NUT (Network UPS Tools) bridge** standalone via Wi-Fi. Si connette a un gruppo di continuità (UPS) USB (Eaton, APC, CyberPower, Powercom e altri) tramite USB Host e ne espone i dati sulla rete locale utilizzando il protocollo standard NUT (porta 3493) e un'interfaccia web integrata.

Questo progetto permette di integrare facilmente il tuo UPS (che magari dispone solo della porta USB) in sistemi come Home Assistant, TrueNAS, Synology o qualsiasi altro client NUT senza la necessità di tenere acceso un Raspberry Pi o un PC 24 ore su 24.

## 🌟 Funzionalità

- **Supporto USB Host Nativo**: Legge direttamente i dati HID dai dispositivi UPS compatibili via USB (testato con Eaton, APC, CyberPower, Powercom).
- **Protocollo Server NUT**: Implementa il protocollo standard NUT, rendendolo immediatamente compatibile con i client NUT esistenti (`upsc`, `upsmon`, Home Assistant, PeaNUT, …), fino a 8 contemporaneamente.
- **Comandi UPS**: Test della batteria e del pannello, beeper, comandi di carico e di spegnimento, dalla Web UI oppure con `upscmd` e Home Assistant, come con il driver `usbhid-ups` di NUT.
- **Parametri UPS**: Modifica dei parametri che l'UPS permette di cambiare (soglie di batteria scarica, punti di intervento, data della batteria, ritardi di spegnimento, …) dalla Web UI oppure con `upsrw`.
- **Interfaccia Web & Captive Portal**: Configurazione facile del Wi-Fi e delle credenziali NUT tramite una dashboard web moderna e responsiva, protetta da login.
- **Aggiornamenti Over-The-Air (OTA)**: Carica nuove versioni del firmware direttamente dal browser web senza collegare fisicamente la scheda al PC.
- **LED Diagnostico**: Feedback visivo immediato per lo stato della connessione Wi-Fi e del collegamento con l'UPS.

## 🔌 UPS Supportati

Il firmware utilizza le mappature HID ufficiali del progetto Network UPS Tools (NUT) e supporta automaticamente diversi produttori:

<details>
<summary><b>Eaton</b> (Clicca per espandere i modelli testati)</summary>

- Serie Eaton 3S
- Eaton 5P 1U
</details>

<details>
<summary><b>APC</b> (Clicca per espandere i modelli testati)</summary>

- APC Back-UPS CS 500
- APC BackUPS Pro 900
- APC Back-UPS Pro BN1050M, BGM2200
- APC Back-UPS XS 700U, XS 1500M
- APC Back-UPS BX750MI, BX1200MI
- APC Back-UPS ES 600M1, ES 700G
- APC Smart-UPS 750, 750L, 1000L

**Serie Back-UPS BX:** il firmware APC di questi modelli smette di rispondere alle richieste USB a intervalli irregolari, spesso più volte all'ora (succede anche con NUT su Linux). Dalla v1.7.1 la scheda si riavvia da sola pochi secondi dopo ogni blocco, e lo stato dell'UPS continua ad aggiornarsi fino a quel momento. I client NUT possono comunque vedere i dati non aggiornati per qualche secondo. ([#60](https://github.com/maverick1982/esp32-nut/issues/60))
</details>

<details>
<summary><b>CyberPower</b> (Clicca per espandere i modelli testati)</summary>

- CyberPower UT650EG, UT850EG
- CyberPower CP 1350C / CP1350AVRLCD
- CyberPower CP1500AVRLCDa
- CyberPower CP1500EPFCLCD, CP1600EPFCLCD
- CyberPower CP1500PFCLCDa
- CyberPower CP1500PFCRM2U
- CyberPower SL700U
</details>

<details>
<summary><b>Powercom</b> (Clicca per espandere i modelli testati)</summary>

- Powercom SPD-750U
</details>

<details>
<summary><b>Legrand</b> (Clicca per espandere i modelli testati)</summary>

- Legrand Keor PDU 800
</details>

<details>
<summary><b>Powerwalker</b> (Clicca per espandere i modelli testati)</summary>

- Powerwalker VI 2200 SHL
</details>

<details>
<summary><b>EcoFlow</b> (Clicca per espandere i modelli non testati / forniti dalla community)</summary>

- EcoFlow River 3 Plus
- EcoFlow Delta 3 Plus

**Non testati / forniti dalla community.** Questi modelli sono riconosciuti dal driver ma non sono ancora stati validati su hardware reale.
</details>

<details>
<summary><b>WalleCube</b> (Clicca per espandere i modelli testati)</summary>

- WalleCube Smart UPS W150
</details>

<br>

- **Generici / Altre Marche**: Se la marca dell'UPS non è riconosciuta, il sistema usa un driver generico che legge i valori standard USB HID (carica e autonomia della batteria, carico, tensioni, stato). I valori specifici della marca possono mancare. In questo caso, o per segnalare un problema su un driver esistente, usa il pulsante **Export USB Diagnostics** nella Web UI (tab System Logs) per scaricare un file diagnostico (`usb_diagnostics.json`) e allegalo quando apri una Issue su GitHub.

> [!NOTE]
> **Requisito Protocollo USB HID:** Questo firmware supporta attualmente solo UPS conformi allo standard **USB HID Power Device Class** (`usbhid-ups`). I gruppi di continuità che comunicano tramite convertitori USB-Seriale o protocolli seriali proprietari (es. Megatec/Q1, serial over USB CDC) non sono attualmente supportati.

## 📸 Screenshot

| Telemetria UPS | Comandi UPS |
| :---: | :---: |
| [![Telemetria UPS](docs/images/ui-ups.png)](docs/images/ui-ups.png) | [![Comandi UPS](docs/images/ui-commands.png)](docs/images/ui-commands.png) |

| Parametri UPS | Configurazione Wi-Fi |
| :---: | :---: |
| [![Parametri UPS](docs/images/ui-settings.png)](docs/images/ui-settings.png) | [![Configurazione Wi-Fi](docs/images/ui-wifi.png)](docs/images/ui-wifi.png) |

| Server NUT | Login Web |
| :---: | :---: |
| [![Server NUT](docs/images/ui-nut.png)](docs/images/ui-nut.png) | [![Login Web](docs/images/ui-weblogin.png)](docs/images/ui-weblogin.png) |

| Log di Sistema | Aggiornamento OTA |
| :---: | :---: |
| [![Log di Sistema](docs/images/ui-logs.png)](docs/images/ui-logs.png) | [![Aggiornamento OTA](docs/images/ui-ota.png)](docs/images/ui-ota.png) |

## 🛠 Requisiti Hardware & Cablaggio

- **Scheda ESP32-S3**: È **obbligatorio** utilizzare una scheda di sviluppo basata su ESP32-S3 poiché supporta l'USB OTG Host in modo nativo. I normali ESP32 o ESP32-C3 non funzioneranno.
- **Cavo USB OTG**: Un adattatore per collegare il cavo USB dell'UPS all'ESP32.

### Configurazione su Board generiche con doppia USB-C (es. Generic ESP32-S3 DevKit)
Molte schede generiche ESP32-S3 con due porte USB-C nascondono delle piazzole (solder pads) sul retro per abilitare la modalità Host. Per ottenere un'installazione pulita senza saldare manualmente fili ai pin GPIO:
1. **Saldare le piazzole "USB-OTG"**: Sul retro della scheda, trova le due piazzole denominate `USB-OTG` e uniscile con una goccia di stagno. Questo convoglia i 5V alla porta `USB`, permettendole di agire da Host per alimentare l'interfaccia USB dell'UPS.
2. **Saldare le piazzole "RGB" (Facoltativo)**: Unisci le piazzole `RGB` se desideri abilitare il LED di stato integrato.
3. **Alimentazione**: Collega un alimentatore da muro alla porta etichettata `COM` (o `UART`).
4. **Dati**: Collega un adattatore USB-C OTG alla porta etichettata `USB` e collegalo all'UPS.

### Case in Stampa 3D (Opzionale)

Trasforma la tua scheda ESP32-S3 nuda in un prodotto finito! Abbiamo disegnato un case elegante da stampare in 3D.

<img src="hardware/case/images/case-front-green-light.jpg" width="400" alt="Case in Stampa 3D">

Scarica i file pronti per la stampa dalla tua piattaforma preferita:
- [**Printables**](https://www.printables.com/model/1794471-case-esp32-nut-server-bridge)
- [**Thingiverse**](https://www.thingiverse.com/thing:7389257)

I file sorgente `.3mf`, il progetto CAD originale `.f3d` e altre foto sono disponibili nella cartella [`hardware/case`](hardware/case).

## 🚀 Guida all'uso

### 1. Caricare il Firmware (Flashing)

**Opzione A: Installazione Zero-Click via Browser (Consigliato)**
Puoi installare il firmware direttamente dal tuo browser senza scaricare alcun programma!
👉 <a href="https://maverick1982.github.io/esp32-nut/" target="_blank">**Installa via Browser**</a>

**Opzione B: Compilazione Manuale tramite PlatformIO**
1. Clona questo repository.
2. Apri la cartella del progetto in VSCode con l'estensione PlatformIO installata.
3. Collega il tuo ESP32-S3 al PC (utilizzando la porta USB UART/Prog).
4. Clicca su **Build** e poi su **Upload**.

### 2. Primo Avvio e Configurazione
1. Al primo avvio (o in assenza di una configurazione Wi-Fi salvata), l'ESP32 creerà un Access Point chiamato **`NUT_ESP32_Config`** (Password: `12345678`).
   > **Nota:** Per forzare manualmente l'avvio in modalità Access Point (ad esempio per cambiare rete Wi-Fi), collega l'ESP32 all'alimentazione e scollegalo entro 3 secondi. Al successivo avvio, l'ESP32 entrerà in modalità Access Point. (L'AP non si attiva più in automatico alla perdita del segnale). Se la configurazione salvata è valida e nessun dispositivo resta collegato all'access point per 10 minuti, la scheda riparte da sola in modalità normale; finché un dispositivo è collegato resta in modalità Access Point.
2. Connetti il tuo telefono o PC a questa rete Wi-Fi.
3. Si aprirà automaticamente un Captive Portal. In caso contrario, naviga su `http://192.168.4.1`.
4. Inserisci le credenziali del Wi-Fi di casa e imposta un nome utente/password per il Server NUT.
5. Salva e riavvia.

### 3. 🔐 Login alla Web UI
La Web UI è protetta da nome utente e password.
- **Crea subito le credenziali**: dopo l'aggiornamento a questo firmware (o dopo un flash completo), la prima visita alla Web UI dalla tua rete locale mostra solo il form **Create credentials**. Chi le crea per primo diventa l'amministratore, quindi fallo appena terminato l'aggiornamento.
- **Regole**: nome utente da 1 a 32 caratteri stampabili, password da 8 a 64 byte (da 8 a 64 caratteri ASCII semplici; lettere accentate ed emoji occupano 2-4 byte ciascuna). La password è salvata come hash PBKDF2-SHA256 con salt, mai in chiaro.
- **Tutto richiede il login**: ogni pagina e ogni API, compreso l'aggiornamento firmware OTA.
- **Sessione**: termina dopo 30 minuti senza richieste (un'interfaccia aperta continua ad aggiornarsi, quindi resti connesso finché è aperta) e a ogni riavvio. Per uscire usa il pulsante **Log out** nella barra laterale.
- **Remember me**: se spunti la casella al login, la sessione dura 30 giorni e sopravvive a riavvii e aggiornamenti firmware. Al massimo 8 dispositivi possono essere ricordati: il login su un 9° disconnette il più vecchio.
- **Log out of all devices**: il pulsante nella barra laterale chiude tutte le sessioni, comprese quelle ricordate.
- **Password sbagliate**: dopo 5 password sbagliate di fila, ogni tentativo successivo deve attendere 1 s, poi 2, 4… fino a 60 s. Nessun blocco permanente, e intanto NUT continua a funzionare.
- **Comandi di spegnimento**: i comandi di carico e spegnimento (`load.off`, `shutdown.*`, …) si eseguono dalla pagina **UPS Commands** dopo una conferma, anche in modalità Access Point.
- **UPS Settings**: la pagina **UPS Settings** elenca i parametri dell'UPS che puoi cambiare, con i valori attuali. I parametri che commutano prese, bypass o modalità ECO chiedono prima una conferma. I valori salvati sull'ESP32 (`ups.delay.*`, nomi delle prese) hanno il pulsante **Reset to default**.
- **Separate da NUT**: le credenziali della Web UI sono indipendenti da quelle NUT (scheda NUT), che continuano a funzionare come prima per Home Assistant, PeaNUT, `upscmd`, ecc.
- **Modalità Access Point** (doppio reset entro 3 secondi): la Web UI è accessibile senza login, come prima. Dà accesso completo: credenziali, impostazioni NUT, OTA, comandi e parametri dell'UPS. Tieni la finestra Access Point breve.
- ⚠️ **HTTP in chiaro**: la Web UI è servita in HTTP, quindi le password viaggiano non cifrate sulla rete. Usala su una rete locale fidata e non riutilizzare una password usata altrove.
- **Cambiare le credenziali**: apri la scheda **Web Login** (barra laterale, dopo **NUT Server**), inserisci la password attuale e il nuovo nome utente/password. Il salvataggio disconnette tutti gli altri dispositivi; quello in uso resta connesso.
- **Password dimenticata**:
  1. Doppio reset entro 3 secondi: la scheda si avvia come access point `NUT_ESP32_Config` (password `12345678`).
  2. Collegati, apri la Web UI e vai alla scheda **Web Login**.
  3. Imposta le nuove credenziali con **Reset credentials** (senza la password attuale). Tutte le sessioni vengono chiuse.
  4. Riavvia la scheda per tornare sulla rete di casa (ci torna anche da sola 10 minuti dopo che l'ultimo dispositivo si è scollegato dall'access point).
- ⚠️ **In modalità Access Point** la scheda non è sulla rete di casa, quindi i client NUT (Home Assistant, PeaNUT, …) non la raggiungono. Con una configurazione valida il server NUT resta attivo, ma lo raggiungono solo i dispositivi collegati all'access point. Fai il recupero in tempi brevi.
- **Le credenziali restano**: entrare in modalità Access Point non cancella le credenziali della Web UI, e cambiare solo le impostazioni Wi-Fi le mantiene.
### 4. Utilizzo Quotidiano
- Una volta connesso al Wi-Fi di casa, l'ESP32 otterrà un indirizzo IP dal tuo router.
- **Dashboard Web**: Naviga verso l'indirizzo IP dell'ESP32 nel tuo browser per vedere in tempo reale le statistiche dell'UPS (Batteria, Tensione, Carico, ecc.).
- **Client NUT**: Configura Home Assistant o il tuo NAS per connettersi all'IP dell'ESP32 sulla porta `3493` utilizzando le credenziali definite in precedenza.
- **Nome dell'UPS**: i client NUT indicano l'UPS con il campo **UPS Name** della scheda NUT (predefinito `ups`), non con il modello, per esempio `upsc ups@192.168.1.50`.
- **Fino a 8 client NUT** possono essere connessi contemporaneamente. Oltre questo numero una nuova connessione viene rifiutata, e il System Log mostra quali indirizzi occupano le connessioni. Un client che sparisce senza chiudere la connessione libera il suo posto dopo circa 90 secondi.

## 🛠 Supporta il Progetto

Questo progetto è completamente open-source e gratuito. 
Se ti è stato utile per integrare il tuo UPS nella domotica o nel tuo server rack, prendi in considerazione l'idea di supportarne lo sviluppo!

⭐ **Il modo più semplice per supportarlo è mettere una stellina al repository su GitHub.** Non costa nulla e aiuta altre persone a trovare il progetto.

Se vuoi fare di più, le donazioni verranno utilizzate per l'acquisto di nuovi modelli di UPS da testare, schede di sviluppo ESP32 e altri strumenti hardware necessari per migliorare il codice e garantire la compatibilità con un numero sempre maggiore di dispositivi.

Puoi supportare il progetto tramite:

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/maverick1982)
[![PayPal](https://img.shields.io/badge/PayPal-00457C?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/pasqualecuorpo)

### 💻 Donazioni hardware: un notebook per lo sviluppo

Oggi il limite più grande del progetto è la mia macchina di sviluppo: un notebook di 11 anni su cui ogni ciclo di build e test richiede più di 10 minuti. Un computer più veloce vorrebbe dire correzioni più rapide, più build di test per la community e più modelli di UPS supportati.

Se la tua azienda sta dismettendo dei notebook, uno di questi potrebbe fare una grande differenza. Idealmente:
- **16+ GB di RAM**
- disco **SSD**
- **CPU di non più di 5-6 anni**

Non serve che sia nuovo o perfetto. Se puoi aiutare, scrivi a **support [at] esp32-nut [dot] simplelogin [dot] com**.

Grazie di cuore a chiunque decida di contribuire! 🚀

## 📄 Licenza

Questo progetto è rilasciato sotto licenza **GNU General Public License v3.0 o successiva**. Consulta il file [LICENSE](LICENSE) per maggiori dettagli.

esp32-nut segue il comportamento di [Network UPS Tools](https://networkupstools.org/) (GPL v2 o successiva), e parte delle sue tabelle HID-NUT e della logica dei driver deriva dai sorgenti di NUT. I file derivati riportano gli avvisi di copyright dei file di NUT da cui provengono, e il file [NOTICE](NOTICE) elenca i componenti di terze parti. Le release fino alla v1.8.0 sono state pubblicate con licenza MIT e restano disponibili con quella licenza.
