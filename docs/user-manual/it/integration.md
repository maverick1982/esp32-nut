---
title: "Integrazione Client NUT"
type: manual
tags: [user-manual]
---

# Integrazione Client NUT

Quando la scheda è in rete e legge l'UPS, funziona come un server NUT (`upsd`) con il driver `usbhid-ups`. Qualsiasi client NUT può usarla.

## Parametri di Connessione

- **Host:** l'indirizzo IP della scheda, o il suo nome host se la tua rete lo risolve. La pagina **Network** mostra l'indirizzo.
- **Porta:** `3493`, la porta standard di NUT.
- **Nome UPS:** l'**UPS Name** della pagina NUT Server (predefinito `ups`), non il modello dell'UPS.
- **Username / Password:** le credenziali NUT della pagina NUT Server, se le hai impostate. Servono per comandi e impostazioni; la lettura dei dati funziona anche senza.

Possono essere collegati fino a **8 client NUT** contemporaneamente. Oltre, una nuova connessione viene rifiutata e il System Log indica quali indirizzi occupano le connessioni. Un client che sparisce senza chiudere la connessione libera il suo posto dopo circa 90 secondi.

## Home Assistant

1. Vai su **Impostazioni → Dispositivi e servizi**.
2. Premi **Aggiungi integrazione** e cerca **Network UPS Tools (NUT)**.
3. Inserisci l'indirizzo della scheda e la porta `3493`.
4. Inserisci username e password NUT, se li hai impostati.
5. Conferma: Home Assistant trova l'UPS e crea i suoi sensori (batteria, carico, tensioni, stato…).

I comandi dell'UPS (test della batteria, cicalino, …) sono disponibili come azioni di Home Assistant quando le credenziali NUT sono impostate.

> [!TIP]
> Home Assistant riconosce l'UPS da produttore, modello e numero di serie. Se sostituisci l'UPS e vuoi conservare lo stesso dispositivo e il suo storico, imposta `ups.mfr`, `ups.model` e `ups.serial` come [variabili personalizzate](web-interface.md#custom-variables).

## Linux: upsc, upscmd, upsrw

Installa gli strumenti client di NUT (per esempio `sudo apt install nut-client`), poi usa `<nome ups>@<indirizzo>`:

```bash
# Tutte le variabili
upsc ups@192.168.1.50

# Comandi supportati dall'UPS, poi eseguine uno
upscmd -l ups@192.168.1.50
upscmd -u admin ups@192.168.1.50 test.battery.start.quick

# Variabili scrivibili, poi cambiane una
upsrw ups@192.168.1.50
upsrw -s battery.charge.low=20 -u admin ups@192.168.1.50
```

## Linux: upsmon (Spegnimento Automatico di un Computer)

Per spegnere un computer quando la batteria si sta scaricando, aggiungi la scheda a `upsmon.conf` su quel computer:

```
MONITOR ups@192.168.1.50 1 admin latuapassword secondary
```

Username e password NUT sono quelli della pagina NUT Server.

Se nessun client NUT deve gestire lo spegnimento, la scheda può spegnere da sola l'UPS a batteria scarica: vedi [Automatic shutdown](web-interface.md#automatic-shutdown).

## Quando l'UPS Non Risponde

Quando la scheda non ha dati aggiornati dall'UPS (UPS scollegato, collegamento USB in recupero), i client NUT ricevono `ERR DATA-STALE`, come con NUT. Home Assistant mostra i sensori come *Non disponibile* finché i dati non tornano.
