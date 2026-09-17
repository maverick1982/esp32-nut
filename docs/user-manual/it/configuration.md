---
title: "Configurazione Iniziale"
type: manual
tags: [user-manual]
---

# Configurazione Iniziale

Questo documento illustra i passaggi per connettere ESP32-NUT alla rete Wi-Fi locale e configurare i parametri del server NUT (Network UPS Tools).

## 1. Avvio in Modalità Access Point (AP)

Per poter configurare il dispositivo è necessario che questo si trovi in **Modalità Access Point (AP)**.

**Al Primissimo Avvio (dopo l'installazione):**
Non essendoci configurazioni salvate in memoria, il dispositivo entrerà **automaticamente** in modalità AP. È sufficiente collegare l'alimentazione e attendere qualche istante.

**Forzatura Manuale (per configurazioni successive):**
Se il dispositivo è già stato configurato in precedenza ma hai bisogno di cambiare la rete Wi-Fi o altri parametri, l'AP *non si attiverà mai da solo* (nemmeno in caso di perdita del segnale). Dovrai innescarlo manualmente con questa procedura:
1. Collega l'alimentazione alla scheda ESP32-S3 tramite la porta `COM` o `UART`.
2. **Entro 3 secondi** dall'accensione, scollega bruscamente l'alimentazione (stacca il cavo).
3. Ricollega l'alimentazione per riavviare la scheda. 
4. A questo avvio consecutivo, il dispositivo ignorerà la configurazione salvata ed entrerà in Modalità AP.

Una volta attivata la Modalità AP:
1. Utilizzando uno smartphone o un computer, scansiona le reti Wi-Fi disponibili.
2. Connettiti alla rete denominata **`NUT_ESP32_Config`**.
3. Quando richiesta, inserisci la password predefinita: `12345678`.

## 2. Accesso al Captive Portal

Nella maggior parte dei moderni sistemi operativi, non appena connessi alla rete del dispositivo, si aprirà automaticamente una finestra del browser nota come "Captive Portal".

Qualora il Captive Portal non si avviasse automaticamente:
- Apri il browser web.
- Digita manualmente il seguente indirizzo nella barra degli URL: `http://192.168.4.1`

## 3. Configurazione dei Parametri

L'interfaccia web di configurazione è suddivisa in sezioni intuitive:

### Impostazioni Wi-Fi (Wi-Fi Settings)
- Seleziona il nome (SSID) della tua rete domestica o aziendale dall'elenco a discesa.
- Inserisci la password della tua rete Wi-Fi. Assicurati che i dati siano corretti, altrimenti il dispositivo non riuscirà a connettersi. Ricorda che in caso di errore, il dispositivo **non tornerà in Modalità AP da solo**: dovrai ripetere la procedura di Forzatura Manuale (stacco alimentazione entro 3 secondi) per poter reinserire la password corretta.

### Parametri Server NUT (NUT Server Settings)
- **UPS Name:** Assegna un identificativo al tuo UPS (es. `ups_salotto`). **Attenzione:** questo campo non può contenere spazi vuoti.
- **Username:** Scegli un nome utente che utilizzerai nei client esterni (es. Home Assistant, NAS) per accedere ai dati dell'UPS.
- **Password:** Imposta una password sicura associata al nome utente.

> [!NOTE]
> I due moduli di configurazione si comportano in modo diverso:
> - Il salvataggio dei **Parametri Server NUT** ha effetto immediato e *non provoca il riavvio* della scheda.
> - Cliccando invece sul pulsante **"Initialize Connection"** (per salvare le Impostazioni Wi-Fi), il dispositivo **si riavvierà automaticamente** per tentare la connessione alla tua rete locale.
