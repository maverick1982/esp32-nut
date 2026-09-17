---
title: "Integrazione con i Client NUT"
type: manual
tags: [user-manual]
---

# Integrazione con i Client NUT

Una volta che il tuo ESP32-NUT è connesso alla rete Wi-Fi e l'UPS è stato rilevato correttamente, il dispositivo funzionerà esattamente come un server NUT tradizionale in rete locale.

Questo significa che è "plug & play" con qualsiasi client software compatibile con il protocollo NUT standard.

## Parametri di Connessione

Per configurare il tuo client (Home Assistant, pfSense, TrueNAS, o un client Linux generico), avrai bisogno di questi quattro parametri:

- **Indirizzo IP (Host):** L'indirizzo IP assegnato dal tuo router (DHCP) all'ESP32. Puoi trovarlo nella pagina del tuo router o tramite la Web UI dell'ESP32.
- **Porta (Port):** `3493` (la porta standard del protocollo NUT).
- **Nome UPS (UPS Name):** Il nome che hai scelto in fase di [Configurazione Iniziale](configuration.md) (es. `ups_salotto`).
- **Username / Password:** Le credenziali impostate in fase di configurazione.

## Esempio 1: Home Assistant

L'integrazione ufficiale di Network UPS Tools in Home Assistant è il modo più rapido per avere una dashboard.

1. In Home Assistant, vai su **Impostazioni > Dispositivi e Servizi**.
2. Clicca su **Aggiungi Integrazione** e cerca "Network UPS Tools (NUT)".
3. Inserisci l'Indirizzo IP (Host) dell'ESP32 e lascia la porta a `3493`.
4. Inserisci l'Username e la Password.
5. Invia il modulo: Home Assistant rileverà automaticamente il nome del tuo UPS e inizierà a creare le entità sensore (Batteria, Carico, Tensione, ecc.).

## Esempio 2: Client Linux generico (`upsc`)

Se stai testando il server da una macchina Linux (es. Raspberry Pi o un server Ubuntu), puoi utilizzare il client standard da riga di comando `upsc`.

Installa il client (es. `sudo apt install nut-client`), poi esegui il comando usando la sintassi `nome_ups@indirizzo_ip`:

```bash
upsc ups_salotto@192.168.1.50
```

Se la connessione ha successo, vedrai stampata a terminale la lista completa dei dati telemetrici forniti dall'UPS.
