### Completamento Implementazione

* Tutti i task pianificati sono stati implementati:
  * Resettato lo stato interno dell'UPS a "Unknown" all'evento `DEV_GONE`.
  * Ottimizzato il loop principale per ripartire immediatamente al rilevamento `NEW_DEV`.
  * Aggiunto unit test `test_dev_gone` per validare i cambi di stato alla disconnessione.
* Nessun blocco hardware o deviazione dal piano originale.
* La spec è pronta per il collaudo.
