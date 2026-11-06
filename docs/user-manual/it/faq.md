---
title: "Domande Frequenti"
type: manual
tags: [user-manual]
---

# Domande Frequenti (FAQ)

### Posso usare un ESP32 normale o un ESP8266?
**No.** ESP32-NUT richiede un chip con un controller USB host: funziona su **ESP32-S3** e sulle schede **ESP32-P4** supportate. Le schede ESP32 standard, ESP32-C3 ed ESP8266 non possono parlare con l'UPS.

### Posso collegare ESP32-NUT con un cavo Ethernet?
**Sì, con una scheda ESP32-P4** come la Waveshare ESP32-P4-ETH, anche in PoE. Le schede ESP32-S3 usano il Wi-Fi. Vedi [Requisiti Hardware](hardware.md).

### Devo saldare?
Su una scheda ESP32-S3 generica con due porte USB-C, quasi sempre **sì**: devi unire le due piazzole `USB-OTG` sul retro perché la scheda possa alimentare l'interfaccia dell'UPS. Le schede ESP32-P4 non richiedono saldature.

### Quale immagine ESP32-P4 mi serve?
Dipende dalla revisione del chip: **v3.1 o successiva** per le schede attuali, **prima della v3** per le prime schede v1.x. Esegui `esptool chip-id` per scoprirla. Un'immagine sbagliata viene semplicemente rifiutata, quindi puoi installare l'altra. Vedi [Revisione del chip](hardware.md#revisione-del-chip-due-immagini).

### Perché il captive portal non si apre da solo?
Alcuni telefoni e sistemi operativi non aprono automaticamente i captive portal. Collegati alla rete `NUT_ESP32_Config`, ignora l'avviso "nessuna connessione a Internet", apri un browser e vai su `http://192.168.4.1`.

### Come cambio la rete Wi-Fi?
Dalla pagina **Network** mentre la scheda è in rete, oppure, se non riesce più a collegarsi, dall'access point di configurazione dopo un doppio reset (stacca l'alimentazione entro 3 secondi dall'accensione, poi ricollegala). Vedi [Configurazione Iniziale](configuration.md).

### Il login web e le credenziali NUT sono le stesse?
**No.** Il login web protegge l'interfaccia web (pagina **Web Login**). Le credenziali NUT servono ai client NUT per comandi e impostazioni (pagina **NUT Server**). Cambiare le une non cambia le altre.

### I miei dati restano privati?
La scheda non invia nulla su Internet, tranne le richieste dell'ora NTP (che puoi disattivare in **Board Settings → Time**) e le righe syslog verso il server che scegli tu. L'interfaccia web usa HTTP semplice, quindi usala su una rete fidata.

### Quanti client NUT possono collegarsi?
Fino a 8 contemporaneamente.

### La scheda può spegnere l'UPS senza un client NUT?
**Sì.** Attiva **Board Settings → Automatic shutdown**: a batteria scarica la scheda invia da sola `shutdown.default` all'UPS. Vedi [Automatic shutdown](web-interface.md#automatic-shutdown).

### Il mio UPS riporta un modello sbagliato o non ha la data della batteria. Posso correggerlo?
**Sì, per i client NUT.** Aggiungi una [variabile personalizzata](web-interface.md#custom-variables) nella pagina **NUT Server**, per esempio `ups.model` o `battery.date`.

### Perdo le impostazioni quando aggiorno?
**No.** Gli aggiornamenti OTA conservano tutte le impostazioni. Con il web installer, lascia **"Erase device"** deselezionato per conservarle.

### Il mio UPS ha una porta RJ45 o RJ11 sul retro. Posso usarla?
**No.** Quelle porte proteggono linee telefoniche o di rete, oppure usano protocolli seriali. ESP32-NUT richiede la **porta USB** dell'UPS (di solito di tipo B, quella quadrata delle stampanti).
