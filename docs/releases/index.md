---
type: reference
title: "Release Notes"
description: "Come scrivere le release notes di esp32-nut: template, regole e fonti da cui ricavarle. Pensato anche per gli agenti AI."
tags: [index, release, release-notes]
---

# Release Notes

Questa directory contiene il [template](template.md) delle release notes e, una per release, le note già scritte (`vX.Y.Z.md`).

## Come nasce una release

Il tag e le note sono indipendenti: si può taggare qualunque commit in qualunque momento, e le note si possono scrivere prima, dopo o mai.

1. Il push di un tag `vX.Y.Z` avvia `.github/workflows/release.yml`: build, zip del firmware, release su GitHub e aggiornamento del web installer. Il nome del tag diventa `FIRMWARE_VERSION`.
2. Testo della release:
   - se il commit taggato contiene `docs/releases/vX.Y.Z.md`, il testo della release è quel file;
   - altrimenti GitHub genera le note dalle PR unite dal tag precedente ("What's Changed" + "Full Changelog"), come prima.
3. Quando un file `docs/releases/vX.Y.Z.md` viene aggiunto o modificato su `main`, `.github/workflows/release-notes.yml` sostituisce il testo della release `vX.Y.Z` già pubblicata. Vale anche per correggere le note in seguito e per le release vecchie. Se la release non esiste ancora il file viene ignorato: lo userà `release.yml` al push del tag.
4. Da *Actions → Release notes → Run workflow* si può riapplicare a mano il file di un tag.

In entrambi i workflow `scripts/release_notes.py` toglie il frontmatter OKF: il file resta una pagina di `docs/`, la release mostra solo le note.

Il file sostituisce **tutto** il testo della release: niente "What's Changed" automatico. Per questo il template ha le sezioni *Thanks* e *Full Changelog*.

## Regole

- **Lingua: inglese**, come i commit, le PR e il README. Il resto di `docs/` è in italiano, ma le release notes le leggono utenti di tutto il mondo.
- **Parti dai commit, non dalle PR.** Usa `git log --oneline vPRECEDENTE..vX.Y.Z` (o `..HEAD` prima del tag): così compaiono anche i commit pushati direttamente su `main` senza PR.
- **Non elencare commit o PR uno per uno**: raggruppa per effetto visibile. Il dettaglio è nel link *Full Changelog*.
- **Scrivi per l'utente, non per lo sviluppatore.** Descrivi il sintomo che vedeva e i modelli di UPS coinvolti (marca, modello, VID:PID quando serve), non la funzione modificata. Esempio: "Powercom SPD-750U: no more disconnects during the full poll", non "stepSpacingMs() hook at 800 ms".
- **Cita l'issue** (`#36`) per ogni voce che ne ha una.
- **"Upgrade notes" è obbligatoria quando serve.** Ci va tutto quello che cambia per chi aggiorna: token `ups.status` diversi, variabili NUT rinominate o rimosse, impostazioni da rifare, casi in cui l'OTA non basta e serve il web installer (come per la v1.6, che ha aggiunto la partizione `coredump`). Se non c'è niente, elimina la sezione.
- **Due firmware diversi.** "ESP32-NUT firmware" è il nostro; `ups.firmware` è quello dell'UPS. Non confonderli: in passato gli utenti li hanno scambiati.
- **Ringrazia** chi ha segnalato, chi ha provato le build di test sul proprio UPS e **sempre** chi ha contribuito con una PR, con il suo handle GitHub: senza "What's Changed" è l'unico credito che riceve.
- **Elimina le sezioni vuote** e i segnaposto `[...]`.
- **Senza file di note** la release usa i titoli delle PR così come sono: se una PR è stata rielaborata e il titolo descrive la versione vecchia, rinominala prima del tag.

## Numero di versione

[Semantic Versioning](https://semver.org/):

- **patch** (`v1.6.1` → `v1.6.2`): solo correzioni;
- **minor** (`v1.6.x` → `v1.7.0`): nuove funzioni o nuovi UPS supportati, senza rotture;
- **major** (`v1.x` → `v2.0.0`): cambi incompatibili per client NUT o configurazione.

## Procedura per un agente AI

1. `git describe --tags --abbrev=0` per trovare l'ultimo tag, poi `git log --oneline <tag>..HEAD`.
2. Per ogni commit leggi il messaggio e, se serve, il diff e l'issue collegata (`docs/issues/`, se presente, o GitHub).
3. Copia [template.md](template.md) in `vX.Y.Z.md`, aggiorna il frontmatter (`type: reference`), compila le sezioni seguendo le regole sopra, togli il commento iniziale e le sezioni vuote. Verifica il risultato con `python scripts/release_notes.py docs/releases/vX.Y.Z.md`.
4. Aggiungi la riga della release nell'elenco qui sotto.
5. Proponi il testo al maintainer: non committare su `main`, non creare tag e non modificare release senza la sua conferma esplicita. Un commit su `main` di un file di note **aggiorna subito** la release pubblicata.

## Release

<!-- Una riga per ogni vX.Y.Z.md, dalla più recente. -->
- [v1.7.2](v1.7.2.md) - ups.model APC senza firmware e non più troncato, ups.firmware / ups.firmware.aux (#76); APC Back-UPS ES 700G testato (#71).
- [v1.7.1](v1.7.1.md) - Recupero automatico dei blocchi USB degli APC Back-UPS BX (#60), variabili device.*, niente FSD/COMM_LOST in ups.status (#65).
- [v1.7.0](v1.7.0.md) - Supporto EcoFlow (#61), letture Powercom SPD-750U (#36), token ups.status da PresentStatus (#62), battery.voltage.nominal arrotondato (#67).
- [v1.6.1](v1.6.1.md) - Boot loop APC Back-UPS (#55), mappature APC ripristinate (#48).
- [v1.6.0](v1.6.0.md) - Deadlock USB (#47): valori a zero o mancanti, crash e freeze; layer USB irrobustito.
