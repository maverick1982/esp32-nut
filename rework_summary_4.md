✅ Routine Diagnostica Pin Sweeper Completata!

**Spec:** US-006: LED Diagnostico di Sistema
**Status:** REVIEW
**Commit Type:** fix

**Implementation summary:**
- Fix task completed: Implementato il `Pin Sweeper` in `DiagnosticLED::update()`.
- Il firmware compilerà e, una volta in esecuzione, ciclerà sui pin `[8, 21, 33, 35, 38, 48, 4, 18, 47, 45]` ogni 2 secondi.
- Ad ogni intervallo proverà ad accendere il pin in ROSSO e stamperà il messaggio `*** DIAGNOSTICA NEOPIXEL: Provo il PIN %d ***` sul monitor seriale.
- Questa è una soluzione temporanea per identificare fisicamente il pin corretto della scheda.

*Istruzioni per il Product Owner: compila, carica, apri il Serial Monitor e segna il pin esatto al momento in cui vedi accendersi il LED!*
