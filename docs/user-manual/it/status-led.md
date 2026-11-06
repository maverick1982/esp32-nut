---
title: "LED di Stato (Colori e Lampeggi)"
type: manual
tags: [user-manual]
---

# LED di Stato (Colori e Lampeggi)

Il LED RGB di stato delle schede ESP32-S3 (su GPIO 48, abilitato dalle piazzole `RGB`) mostra a colpo d'occhio lo stato della rete e dell'UPS. Le schede ESP32-P4 non hanno un LED di stato: usa l'interfaccia web.

## Legenda degli Stati

| Colore | Sequenza | Stato | Significato |
| :--- | :--- | :--- | :--- |
| 🔵 **Blu** / 🔴 **Rosso** | Alternanza veloce per 4 s, poi 4 s spento | **Access point di configurazione** | La scheda è in modalità access point (`NUT_ESP32_Config`) e attende che tu ti colleghi e inserisca le impostazioni Wi-Fi. |
| 🟡 **Giallo** | Lampeggio lento (1 s acceso, 1 s spento) | **Connessione in corso** | La scheda non è ancora collegata alla rete Wi-Fi. È normale per qualche secondo dopo l'accensione. Se dura, la rete Wi-Fi non è raggiungibile o le impostazioni salvate sono sbagliate. |
| 🔴 **Rosso** | Lampeggio veloce | **Nessun UPS** | La scheda è in rete, ma sulla porta USB non c'è un UPS collegato o riconosciuto. |
| 🟢 **Verde** | Breve lampo ogni 5 s | **Operativo** | Tutto funziona: la scheda è in rete e legge l'UPS. Il lampo è breve per non disturbare in una stanza buia. |

## Impostazioni del LED

In **Board Settings → Status LED** puoi spegnere il LED o sceglierne la luminosità da 1 a 5. Da spento resta buio in ogni stato, anche dopo un riavvio.

> [!TIP]
> Con un lampeggio rosso veloce o giallo prolungato, apri l'interfaccia web e controlla la pagina **System Logs**, oppure vedi [Risoluzione Problemi](troubleshooting.md).
