---
type: decision
title: "Autenticazione dell'interfaccia web"
description: "Decisioni di prodotto per proteggere con login l'interfaccia web di esp32-nut."
tags: [pdr, prodotto, sicurezza, web-ui, autenticazione]
---
# Autenticazione dell'interfaccia web

* **PDR ID:** 0001
* **Stato:** Accettato
* **Data:** 2026-10-10
* **Autori:** @maverick1982, Claude
* **Decisioni architetturali collegate:** [ADR 0009](../adr/0009-web-ui-authentication.md)
* **Revisione:** 2026-10-16 (US-068): in AP i comandi di spegnimento non sono più bloccati (D7); in AP manuale con configurazione valida il server NUT resta attivo (D6).

## Problema

Oggi l'interfaccia web non ha login. Chiunque sulla rete locale può:
- leggere la configurazione e i log;
- cambiare Wi-Fi e credenziali NUT;
- caricare un firmware via OTA.

Per questo i comandi che spengono l'uscita dell'UPS (`load.*`, `shutdown.*`) nella pagina "UPS Commands" sono bloccati e si possono eseguire solo via NUT (US-059). Su [#78](https://github.com/maverick1982/esp32-nut/issues/78) abbiamo annunciato che con il login questi comandi saranno eseguibili anche dalla Web UI.

## Obiettivi

- Nessuna funzione dell'interfaccia web è accessibile senza autenticazione, in modalità normale (STA).
- L'utente non deve autenticarsi a ogni visita, se lo sceglie.
- Chi ha accesso fisico alla scheda può sempre recuperare l'accesso, senza procedure di recupero password.
- Chi aggiorna via OTA da una versione senza login non resta chiuso fuori.

## Fuori ambito

- HTTPS: l'interfaccia resta in HTTP (vedi "Limiti accettati").
- Più utenti o ruoli: c'è un solo utente.
- Password dell'Access Point non fissa: resta `12345678` per ora.
- Cambiare le credenziali NUT o il modo in cui sono salvate.
- Recupero password via e-mail o simili.

## Decisioni

### D1. Login con form, su tutta l'interfaccia
- In STA ogni pagina e ogni API richiede una sessione valida. Sono comprese `/update` (OTA), `/api/usb/dump`, `/api/logs` e `/api/config`.
- Restano accessibili senza login solo la pagina di login, i suoi asset e le API di login e di prima configurazione.
- Il form chiede username e password e ha la casella **"Rimani connesso"**.
- C'è un pulsante **Logout** e un'azione **"Esci da tutti i dispositivi"**.
- Se la sessione scade mentre l'interfaccia è aperta, l'utente torna alla pagina di login.

### D2. Durata delle sessioni
- Senza "Rimani connesso": la sessione scade dopo **30 minuti di inattività** e non sopravvive a un riavvio della scheda.
- Con "Rimani connesso": la sessione dura **30 giorni** e sopravvive a riavvii e aggiornamenti OTA.
- "Esci da tutti i dispositivi" e il cambio password chiudono tutte le sessioni. Al cambio password resta valida solo quella da cui è stato fatto.

### D3. Prima configurazione (TOFU)
- Se non esistono credenziali, ad esempio subito dopo un aggiornamento OTA da una versione senza login, il primo accesso dalla rete locale mostra il form **"Crea credenziali"** e nient'altro.
- Il primo che le crea diventa l'amministratore.
- Rischio accettato: tra l'aggiornamento e il primo accesso, chiunque sulla LAN potrebbe crearle per primo. Non è peggio di oggi, in cui chiunque sulla LAN ha già accesso completo.

### D4. Credenziali
- **Username** libero: da 1 a 32 caratteri stampabili.
- **Password** da 8 a 64 byte (da 8 a 64 caratteri ASCII; lettere accentate ed emoji occupano più byte).
- Le credenziali web sono **separate** da quelle NUT. In nessuna schermata vengono proposte insieme e non c'è l'opzione "usa le stesse".
- La password non è mai salvata in chiaro e non è mai restituita dalle API.

### D5. Cambio password e reset: un solo form
- C'è un solo form "Credenziali", con username, nuova password e conferma.
- **In STA**, da autenticati, il form chiede anche la **password attuale**.
- **In modalità AP** il form non chiede la password attuale: è il canale di **reset** per chi l'ha dimenticata.
- La regola è applicata dal firmware, non solo nascondendo il campo nell'interfaccia.
- Entrare in AP **non cancella** le credenziali esistenti: chi entra in AP solo per cambiare Wi-Fi le ritrova invariate.

### D6. Nessuna procedura di recupero password
Per recuperare l'accesso si porta la scheda in modalità AP con il **doppio reset entro 3 secondi**, che richiede accesso fisico, e si imposta una nuova password. In AP manuale con configurazione valida il server NUT resta attivo, ma lo raggiungono solo i dispositivi collegati all'AP: la scheda non è sulla rete di casa. Se la configurazione manca o non è valida, il server NUT non parte. La documentazione deve dirlo.

### D7. Modalità AP
- In AP l'interfaccia è accessibile **senza login**, come oggi: chi è lì ha accesso fisico alla scheda.
- In AP comandi e parametri dell'UPS funzionano come in STA, con le stesse conferme (revisione del 2026-10-16). Motivi:
  - chi entra in AP ha già accesso totale: reset delle credenziali senza la vecchia password (D5), configurazione NUT, aggiornamento OTA;
  - la protezione è l'ingresso in AP: doppio reset fisico e timeout;
  - le conferme servono a evitare gli errori.
- **Timeout:** se si entra in AP con il doppio reset e la configurazione è valida, la scheda riparte in modalità normale dopo **10 minuti senza client collegati all'AP**. Finché un dispositivo resta collegato, il timeout non scatta.
- Se l'AP è partito perché la configurazione manca o non è valida (primo avvio, NVS vuoto o corrotto), non c'è una modalità normale a cui tornare e l'AP resta attivo.
- La password dell'AP resta `12345678` (fuori ambito).

### D8. Comandi di spegnimento dalla Web UI
- Da autenticati, in STA, la pagina "UPS Commands" esegue **tutti** i comandi supportati dall'UPS, compresi `load.*` e `shutdown.*`.
- Il lucchetto e la nota "Available via NUT only until web authentication is implemented" vengono rimossi.
- Prima di eseguire un comando di spegnimento l'interfaccia chiede una conferma esplicita, con il nome del comando e l'avviso che toglie corrente alle prese.

### D9. Tentativi di login falliti
- Dopo **5 tentativi falliti consecutivi**, i successivi vengono rifiutati per un'attesa che raddoppia a ogni nuovo errore, da 1 s fino a un massimo di 60 s.
- Un login riuscito azzera il contatore.
- Nessun blocco permanente: non deve diventare un modo per chiudere fuori il proprietario.

## Limiti accettati

- **HTTP in chiaro.** Password e cookie di sessione viaggiano in chiaro sulla rete locale. L'interfaccia è pensata per una LAN fidata, e la documentazione consiglia di non riusare una password usata altrove.
- **Durata "Rimani connesso".** La scheda non ha un orologio affidabile. I 30 giorni sono fatti rispettare dal browser, mentre il firmware revoca le sessioni con logout, "Esci da tutti i dispositivi" e cambio password (ADR 0009).
- **Accesso fisico.** Chi ha in mano la scheda può reimpostare la password tramite AP, o leggere il flash. È il modello di fiducia scelto. Chi è collegato all'AP con la password `12345678` ha lo stesso accesso totale: credenziali, configurazione NUT, OTA, comandi e parametri dell'UPS. Una password dell'AP diversa resta un lavoro futuro.

## Suddivisione nel backlog

| Spec | Contenuto | Dipende da |
|---|---|---|
| A | Login, sessioni e "Rimani connesso", protezione di tutte le route, prima configurazione (TOFU), logout ed "Esci da tutti i dispositivi", tentativi falliti, sblocco dei comandi di spegnimento (D1, D2, D3, D4, D8, D9) | - |
| B | Form "Credenziali": cambio password in STA con password attuale, reset in AP senza (D5, D6, D7 primo punto) | A |
| C | Timeout della modalità AP manuale (D7 timeout) | - |
