---
title: "LED di Stato (Colori e Lampeggi)"
type: manual
tags: [user-manual]
---

# LED di Stato (Colori e Lampeggi)

Questo documento fornisce una guida rapida all'interpretazione del comportamento del LED di stato RGB (generalmente un WS2812 o simile, integrato sulla scheda o collegato esternamente). 

Il LED fornisce un feedback diagnostico immediato sullo stato vitale del sistema (Wi-Fi e connessione USB con l'UPS).

## Legenda degli Stati

| Colore | Pattern di Lampeggio | Significato (Stato) | Descrizione Tecnica |
| :--- | :--- | :--- | :--- |
| 🔵 **Blu** / 🔴 **Rosso** | Alternati veloci (Cicli da 4s) | **Modalità Setup (AP_MODE)** | Il dispositivo è in modalità Access Point. Attende che l'utente si connetta alla rete Wi-Fi temporanea per effettuare la configurazione iniziale. L'animazione dura 4 secondi, seguita da 4 secondi di spegnimento. |
| 🟡 **Giallo** | Lampeggio lento continuo | **In Connessione (CONNECTING)** | Il dispositivo sta tentando di stabilire la connessione con la rete Wi-Fi configurata. Questo stato è tipicamente visibile per pochi secondi durante l'avvio. |
| 🟢 **Verde** | Breve impulso ogni 5 secondi | **Operativo (OPERATIONAL)** | Funzionamento normale. Il dispositivo è connesso correttamente al Wi-Fi, il server NUT è in ascolto e l'UPS è stato rilevato ed è in comunicazione sulla porta USB. (L'impulso è breve per non disturbare in ambienti bui). |
| 🔴 **Rosso** | Lampeggio veloce continuo | **Errore (ERROR)** | Si è verificata una condizione di anomalia. Le cause più comuni includono l'impossibilità di connettersi alla rete Wi-Fi salvata, un fallimento nell'inizializzazione del server NUT, o il cavo USB dell'UPS scollegato/non riconosciuto. |

> [!TIP]
> Se il LED indica uno stato di Errore (Rosso lampeggiante veloce), ti consigliamo di consultare la sezione dedicata alla [Risoluzione dei Problemi](troubleshooting.md) o di accedere ai log di sistema tramite l'interfaccia web per individuare la causa esatta dell'anomalia.
