---
type: plan
title: "Piano di Refactoring: `extractUsage` Resiliente e Tollerante ai Guasti (Fase 2, Issue #41)"
description: "Documento: Piano di Refactoring per blindare l'estrazione dati dal buffer USB."
tags: [plan, hid, refactoring, robustness]
---
# Piano di Refactoring: `extractUsage` Resiliente e Tollerante ai Guasti (Fase 2, Issue #41)

## 1. Obiettivo e Contesto
Durante la risoluzione della Issue #41 (Fase 1) abbiamo sistemato il calcolo dell'`expected_length` per non troncare più le richieste legittime dei dispositivi USB. Tuttavia, la funzione responsabile di interpretare il payload ricevuto (`HIDParser::extractUsage`) è rimasta in uno stato "ottimistico". 

L'obiettivo della **Fase 2** è rendere questa funzione completamente corazzata contro buffer corrotti, letture parziali e valori spazzatura (garbage) inviati dai microcontrollori UPS economici, garantendo che i dati vengano filtrati in modo sicuro prima di essere passati al resto del sistema.

## 2. Modifiche Architetturali Previste

### A. Refactoring del Tipo di Ritorno (Error Handling Esplicito)
**Problema:** Attualmente `extractUsage` restituisce `0.0` in caso di errore (es. report_id mismatch o puntatore nullo). I driver non possono distinguere tra una lettura valida pari a `0.0` (es. 0 W di carico) e un errore.
**Soluzione:** 
Modificare la firma della funzione passando da un ritorno diretto a un parametro di output (passato per referenza) abbinato a un ritorno booleano per l'esito dell'operazione.
*Vecchia firma:* `double extractUsage(const HIDUsageDef* def, uint8_t report_id, const uint8_t* data, size_t length);`
*Nuova firma:* `bool extractUsage(const HIDUsageDef* def, uint8_t report_id, const uint8_t* data, size_t length, double& out_value);`
Questo permetterà ai driver di ignorare l'assegnazione alla classe `UPSData` se l'estrazione fallisce.

### B. Gestione del Segno (Sign Extension)
**Problema:** I bit estratti dal buffer vengono sempre trattati come un intero senza segno (`raw &= (1ULL << def->bit_size) - 1`). Valori nativamente negativi (es. temperature, calibrazioni con `logical_min < 0`) causano overflow positivi giganteschi.
**Soluzione:** 
Verificare `def->logical_min < 0`. Se il limite inferiore è negativo, il valore binario estratto deve subire una corretta operazione di "sign extension" in base alla sua dimensione (`def->bit_size`) prima di essere sottoposto a cast a `double`.

### C. Tolleranza e Scarto dei Report Troncati
**Problema:** Il loop di estrazione si ferma prima se la dimensione dichiarata eccede il buffer ricevuto. Il byte mancante viene interpretato come zero, falsando potenzialmente la lettura.
**Soluzione:** 
Se il ciclo `for` si interrompe per il raggiungimento di `length` prima di aver consumato tutti i bit richiesti (`def->bit_size`), considerare il pacchetto irreparabilmente corrotto per quello specifico usage e restituire `false`, evitando così di memorizzare un dato parziale.

### D. Validazione Range (Logical Bounds Filtering)
**Problema:** Nessun filtro applicato. I dispositivi possono inviare valori sballati (es. 65535 V).
**Soluzione:** 
A valle dell'estrazione, dell'eventuale estensione del segno e prima dell'applicazione dell'esponente (`val *= pow(10, def->exponent)`), validare che l'intero puro ricada rigorosamente nell'intervallo `[def->logical_min, def->logical_max]`. In caso contrario, restituire `false`.

## 3. Piano di Verifica (TDD)
Dovrà essere aggiunta una suite di test nativa `test_hid_extract_robustness.cpp` (o estesa quella esistente) con le seguenti fixture:
- Iniezione di un buffer appositamente troppo corto per verificare che restituisca `false`.
- Estrazione di un valore negativo noto a 8, 16 e 32 bit per validare il Sign Extension.
- Iniezione di un valore volutamente fuori dal range `logical_max` per verificare lo scarto del pacchetto. 

## 4. Impatto sui Driver Esistenti
L'implementazione richiederà una modifica a tappeto in tutti i file dei sub-driver (Generic, CyberPower, Eaton, APC) per accogliere il nuovo paradigma di ritorno booleano all'interno della logica di parse/update dei record in `UPSData`. 
