---
title: "Configurazione Iniziale"
type: manual
tags: [user-manual]
---

# Configurazione Iniziale

Questo capitolo collega la scheda alla rete e protegge l'interfaccia web. I passaggi sono diversi per l'ESP32-S3 (Wi-Fi) e l'ESP32-P4 (Ethernet).

## ESP32-S3: Configurazione del Wi-Fi

### 1. Avvia l'access point di configurazione

La scheda offre una propria rete Wi-Fi, l'**access point di configurazione**, per ricevere le impostazioni del tuo Wi-Fi.

- **Primo avvio:** senza impostazioni Wi-Fi salvate, l'access point parte **automaticamente**.
- **In seguito:** l'access point non parte mai da solo, nemmeno se il segnale Wi-Fi si perde. Per avviarlo usa un **doppio reset**:
  1. alimenta la scheda;
  2. **entro 3 secondi** stacca l'alimentazione;
  3. ricollegala: la scheda parte in modalità access point.

Poi, da un telefono o un computer:

1. collegati alla rete Wi-Fi **`NUT_ESP32_Config`**;
2. inserisci la password **`12345678`**.

### 2. Apri la pagina di configurazione

Di solito si apre da sola una pagina di captive portal. Se non succede, apri un browser e vai su `http://192.168.4.1`.

In modalità access point l'interfaccia web è aperta **senza login** e dà accesso completo (credenziali, impostazioni, OTA, comandi dell'UPS). Tieni breve la sessione in access point.

### 3. Inserisci la tua rete Wi-Fi

1. Apri la pagina **Network**.
2. In **Connection Setup** scrivi il nome della tua rete Wi-Fi in **Network Name (SSID)** e la sua password in **Security Key**.
3. Premi **Initialize Connection**. La scheda salva le impostazioni e si riavvia sulla tua rete.

Se il nome o la password sono sbagliati, la scheda non torna da sola in modalità access point: usa di nuovo il doppio reset.

Con impostazioni valide salvate, la scheda esce da sola dalla modalità access point quando nessun dispositivo vi resta collegato per 10 minuti. Finché un dispositivo resta collegato, rimane in modalità access point.

> [!NOTE]
> Mentre la scheda è in modalità access point non è sulla rete di casa, quindi Home Assistant e gli altri client NUT non la raggiungono. Con impostazioni valide il server NUT resta attivo, ma solo per i dispositivi collegati all'access point.

### 4. Trova la scheda in rete

La scheda riceve l'indirizzo dal router (DHCP). Nella tabella DHCP del router compare come **`esp32-nut-xxxxxx`** (gli ultimi 3 byte del suo indirizzo MAC), oppure con il nome host che sceglierai nella pagina **Network**. Per trovarla sempre allo stesso indirizzo, riservane uno al suo indirizzo MAC nel router.

## ESP32-P4: Configurazione dell'Ethernet

Non c'è access point né configurazione del Wi-Fi:

1. collega il cavo Ethernet;
2. trova la scheda nella tabella DHCP del router come **`esp32-nut-xxxxxx`**;
3. apri il suo indirizzo nel browser e prosegui con il login web qui sotto.

Il server NUT parte con le impostazioni predefinite: nome UPS `ups` e nessuna credenziale NUT.

## Login dell'Interfaccia Web

L'interfaccia web è protetta da nome utente e password.

### Crea le credenziali

La prima visita all'interfaccia web dalla tua rete mostra solo il modulo **Create credentials**. Chi le crea per primo diventa l'amministratore, quindi fallo subito dopo l'installazione.

- **Nome utente:** da 1 a 32 caratteri stampabili.
- **Password:** da 8 a 64 byte. I caratteri ASCII semplici occupano 1 byte ciascuno, lettere accentate ed emoji da 2 a 4.

La password è salvata come hash PBKDF2-SHA256 con salt, mai in chiaro.

### Sessioni

- Ogni pagina e ogni API richiedono il login, aggiornamenti del firmware compresi.
- Una sessione termina dopo 30 minuti senza richieste e a ogni riavvio. Una pagina aperta continua a interrogare la scheda, quindi resta collegata finché è aperta.
- Spunta **Remember me** all'accesso per restare collegato 30 giorni, anche dopo riavvii e aggiornamenti del firmware. Si possono ricordare al massimo 8 dispositivi: l'accesso da un nono scollega il più vecchio.
- **Log out** nella barra laterale scollega questo dispositivo. **Log out of all devices** chiude tutte le sessioni, comprese quelle ricordate.
- Dopo 5 password sbagliate di fila, ogni tentativo successivo attende 1 s, poi 2, 4… fino a 60 s. Non c'è un blocco permanente, e il server NUT continua a funzionare.

> [!WARNING]
> L'interfaccia web usa HTTP semplice: le password viaggiano in chiaro sulla rete. Usala su una rete fidata e non riutilizzare una password che usi altrove.

### Cambia le credenziali

Apri la pagina **Web Login**, inserisci la password attuale e il nuovo nome utente e la nuova password, poi premi **Save credentials**. Il salvataggio scollega tutti gli altri dispositivi; questo resta collegato.

Le credenziali web sono separate da quelle NUT (pagina **NUT Server**): cambiare le une non cambia le altre.

### Password dimenticata

**ESP32-S3:**

1. Usa il doppio reset per avviare l'access point `NUT_ESP32_Config`.
2. Collegati, apri l'interfaccia web e vai alla pagina **Web Login**.
3. Imposta le nuove credenziali con **Reset credentials**: non serve la password attuale. Tutte le sessioni vengono chiuse.
4. Riavvia la scheda per tornare sulla tua rete. Ci torna anche da sola 10 minuti dopo che l'ultimo dispositivo si è scollegato dall'access point.

**ESP32-P4:** un doppio reset (spegni e riaccendi entro 3 secondi) apre una finestra di 10 minuti in cui la pagina **Web Login** permette di impostare nuove credenziali senza la password attuale.

Entrare in modalità access point non cancella le credenziali web, e anche cambiare solo le impostazioni Wi-Fi le conserva.

## Impostazioni del Server NUT

Apri la pagina **NUT Server** per impostare ciò che i client NUT usano per collegarsi:

- **UPS Name:** il nome con cui i client indicano l'UPS (predefinito `ups`), come in `upsc ups@192.168.1.50`. Non può essere vuoto e accetta caratteri stampabili senza spazi, `"`, `\`, `#` e `@`.
- **Username** e **Password:** le credenziali NUT. Servono per eseguire comandi e cambiare impostazioni dai client NUT (`upscmd`, `upsrw`, comandi di Home Assistant). Leggere i dati non le richiede, come in NUT. Senza credenziali impostate, ogni client è ammesso.

Premi **Save Credentials**: la scheda salva le impostazioni e si riavvia.

---

Prossimo passo: un giro nell'[Interfaccia Web](web-interface.md), oppure vai direttamente all'[Integrazione Client](integration.md).
