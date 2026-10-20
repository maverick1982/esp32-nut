---
type: plan
title: "Supporto agli UPS Q* (nutdrv_qx)"
description: "Analisi preliminare: mercato, trasporti USB, impatto sull'architettura, issue coperte e costi per portare nutdrv_qx in ESP32-NUT."
tags: [piano, analisi, nutdrv_qx, megatec, usb, driver]
---
# Supporto agli UPS Q* (`nutdrv_qx`)

**Data:** 2026-10-20  
**Stato:** Analisi preliminare, non ancora pianificata  
**Obiettivo:** Raccogliere le considerazioni fatte finora per decidere se e come portare il driver NUT `nutdrv_qx` (protocolli Megatec/Q*) in ESP32-NUT, che oggi supporta solo gli UPS USB HID (`usbhid-ups`).

---

## 1. Perché: la quota di mercato

Non esistono dati pubblici affidabili sulle quote di mercato UPS consumer/SOHO per marca, né statistiche sull'uso dei driver NUT. La stima che segue è una triangolazione di tre fonti:

1. **NUT DDL** ([nut-ddl](https://github.com/networkupstools/nut-ddl)): 551 dispositivi unici (marca + modello + driver) da dump inviati dagli utenti. È l'indicatore più vicino alla base utenti NUT reale.
2. **Lista di compatibilità NUT** (`nut_repo/data/driver.list.in`, 1462 righe): dice quanti modelli supporta un driver, non quanti apparecchi sono in uso.
3. **Dati di mercato pubblici**: scarsi. Le quote per marca sono solo nei report a pagamento (Omdia, IDC). Le fonti gratuite dicono solo che Schneider/APC guida il mercato UPS globale (circa 11%, tutti i segmenti).

### NUT DDL, dispositivi unici per driver

| Driver | Dispositivi | Quota |
|---|---|---|
| `usbhid-ups` | 184 | 33% |
| Famiglia Q* (`blazer_usb` 86, `blazer_ser` 45, `nutdrv_qx` 30) | 161 | 29% |
| `snmp-ups` | 45 | 8% |
| Seriali e USB proprietari (`apcsmart`, `bcmxcp`, `tripplite_usb`, `mge-shut`, `powercom`…) | ~160 | ~30% |

Contando solo i driver USB: `usbhid-ups` circa **56%**, famiglia Q* via USB circa **35%**, altri (`tripplite_usb`, `bcmxcp_usb`, `riello_usb`) circa 9%.

La DDL conta i modelli, non gli apparecchi venduti. Sovrarappresenta i dispositivi rari o problematici (per quelli gli utenti inviano un dump) e sottorappresenta i modelli di massa (un dump di Back-UPS copre milioni di apparecchi).

### Stima corretta (UPS collegati via USB a NUT, valori indicativi)

| Driver / marca | Quota stimata | ESP32-NUT oggi |
|---|---|---|
| `usbhid-ups` – APC | 30–35% | ✅ |
| `usbhid-ups` – CyberPower | 15–20% | ✅ |
| `usbhid-ups` – Eaton/MGE | 8–12% | ✅ |
| `usbhid-ups` – Tripp Lite | 3–6% (soprattutto Nord America) | ❌ |
| `usbhid-ups` – altri (Powercom, PowerWalker, Salicru, Liebert, Legrand, EcoFlow…) | 5–8% | in parte |
| **Famiglia Q*** (`nutdrv_qx`/`blazer_usb`) | **20–30%** | ❌ |
| Altri USB (`bcmxcp_usb`, `riello_usb`, `richcomm_usb`, `tripplite_usb`) | 3–6% | ❌ |

Conclusioni:
- ESP32-NUT copre verosimilmente il **55–65%** degli UPS USB in uso.
- La lacuna più grande è la **famiglia Q***, non un sottodriver HID mancante. È dominante nella fascia economica in Europa (soprattutto Est e Sud), Asia e America Latina. Voltronic, il principale costruttore, lavora solo come ODM: i suoi UPS sono venduti con decine di marchi diversi.
- Tra i sottodriver HID mancanti, Tripp Lite è il più rilevante (vedi `docs/no-commit/missing_hid_drivers.md`).

---

## 2. Come comunicano gli UPS Q* via USB

Il protocollo è testuale (comandi come `Q1\r`, `F\r`, `I\r`; risposte come `(230.0 230.0 …\r`) ma passa su bridge USB economici. `nutdrv_qx` ha più di 10 "sottodriver USB" (trasporti), in `nut_repo/drivers/nutdrv_qx.c`:

| Trasporto | Sottodriver | Primitive necessarie |
|---|---|---|
| SET_REPORT di classe HID (comando a blocchi di 8 byte) + risposta su interrupt IN | cypress (0665:5161, il "Megatec USB" più diffuso), phoenix/phoenixtec, ippon, sgs, ablerex, armac, gtec | SET_REPORT grezzo + interrupt IN |
| Comando codificato come indice di string descriptor (GET_DESCRIPTOR) | krauler, fabula, hunnox | string descriptor con indice e langid arbitrari |
| Interrupt OUT | fuji | interrupt OUT (assente) |
| Control transfer vendor + interrupt IN | snr | control vendor (assente) |
| Convertitore USB-seriale (CH340 1A86:7523, …) | nessun sottodriver USB: NUT usa la porta seriale del kernel | driver host CDC/vendor (assente) |

La maggior parte dei bridge Q* si presenta come dispositivo di **classe HID** con usage page vendor-defined: questo è ciò che rende il porting fattibile senza riscrivere lo stack USB.

---

## 3. Impatto sull'architettura attuale

**Resta invariato:** server NUT, Web UI, `UPSData`, calcolo di `ups.status` dai flag `ups.status.*`, LinkMonitor, watchdog e ripristino dell'interfaccia, `DriverRegistry`. Il nuovo driver scrive le stesse chiavi (`ups.status.ac_present`, `battery_low`, …).

**Da cambiare:**

1. **Attach** (`USBHostUPS::claimInterface`): oggi un'interfaccia viene scartata se il report descriptor non contiene le pagine UPS (0x84) o Battery (0x85), che è il caso tipico dei bridge Q*. Va accettata anche quando il `DriverRegistry` riconosce il VID/PID come Q*.
   - Nota: alcuni dispositivi 0001:0000 "MEC0003" (issue #91) espongono un report descriptor con la pagina Power Device e oggi vengono agganciati dal `GenericDriver`, che non riceve dati ("Not Detected").
2. **Due primitive host nuove**:
   - SET_REPORT grezzo (byte e lunghezza arbitrari, senza `HIDUsageDef`);
   - string descriptor "di comando" con langid esplicito e senza la blacklist degli indici falliti di `requestStringDescriptor()`.
   Gli interrupt IN arrivano già al driver tramite `decodeReport()`: le risposte vanno riassemblate fino al `\r`.
3. **Nuovo `QxDriver`**, che implementa `IUPSDriver` direttamente (non eredita da `GenericDriver`): ciclo di poll con `Q1`/`F`/`I`, motore a tabelle, flag di stato.
4. **Comandi e beeper**: `executeCommand`, `CommandCatalog` e il beeper oggi sono costruiti sugli usage HID. Serve un hook per delegarli al driver (`Q` beeper, `T`/`TL` test, `S..R..` e `C` spegnimento). È la modifica più invasiva, ma resta un'estensione.
5. **Riconoscimento del sottodriver**: VID/PID 0001:0000 è condiviso da fabula, hunnox, fuji, krauler e snr. NUT sceglie in base alle stringhe produttore/prodotto (`nutdrv_qx.c:2721-2725`); va fatto lo stesso.

**Limiti:**
- Il `hid_host` vendorizzato si aggancia solo a interfacce di classe HID: fuji, snr e i convertitori seriali restano esclusi finché non si aggiungono primitive o un secondo stack host.
- Il replay attuale è centrato su HID: per i Q* serve una variante con risposte testuali catturate.

---

## 4. Issue aperte coinvolte

Analisi fatta sulle diagnostiche USB allegate e sulla tabella dispositivi di `nutdrv_qx.c`.

| Issue | UPS | VID:PID e stringhe | Sottodriver NUT | Cosa serve |
|---|---|---|---|---|
| #28 | Safire 1500VA | 0001:0000, "MEC" / "MEC0003" | **fabula** (`nutdrv_qx.c:2721`, "Fideltronik/MEC LUPUS 500") | trasporto fabula + protocollo megatec (da confermare) |
| #91 | VIDA Armour 1200VA | 0001:0000, "" / "MEC0003" | **fabula_hunnox** (`nutdrv_qx.c:2722`); l'utente usa `protocol = hunnox`, `langid_fix = 0x0409`, `novendor`, `noscanlangid` | trasporto hunnox + protocollo hunnox |
| #34 | Qoltec Monolith 850VA | 1A86:7523, "USB Serial" (CH340) | nessun sottodriver USB (`nutdrv_qx.c:2718`) | driver host CH34x (es. `usb_host_ch34x_vcp` di `esp-usb`) + megatec seriale: fase separata |

Dettagli:
- **fabula/hunnox** sono della famiglia krauler (comando = indice di string descriptor: `Q1` → 0x03, `F` → 0x0d, `I` → 0x0c), con tabella indici diversa. Hunnox aggiunge una sequenza "lock-step" di richieste preliminari (0x00, 0x01, 0x0d, 0x03, 0x0c) con langid 0x0409 e limita il buffer a 102 byte (`nutdrv_qx.c:1240-1291`, `1546-1690`).
- Il protocollo **hunnox** è la tabella megatec Q1/F/I più il comando `BL` per la carica batteria (`nutdrv_qx_hunnox.c`).
- **cypress** è utile per il mercato in generale ma non serve a nessuna delle tre issue.

---

## 5. Costo delle varianti

Il costo è quasi tutto nell'infrastruttura. A condizione di replicare l'**architettura a tabelle** di `nutdrv_qx` (protocollo = tabella `qx2nut`, trasporto = funzione `*_command()`), aggiungere varianti costa poco. Un parser Q1 scritto a mano renderebbe invece ogni variante un lavoro a sé.

### Trasporti USB (righe di codice in NUT, senza commenti)

| Costo | Trasporti | Righe |
|---|---|---|
| Basso, primitive già previste | cypress 49, gtec 50, phoenixtec 61, ippon 63, sgs 64, phoenix 68, ablerex 71, fabula 73, omron 74, krauler 90, hunnox 97 | 50–100 |
| Basso + una primitiva nuova | fuji 75 (interrupt OUT), snr 77 (control vendor) | 75 + primitiva |
| Medio | armac 147 (cerca gli endpoint da sé) | ~150 |

### Protocolli (righe dei file `nutdrv_qx_*.c`)

| Costo | Protocolli | Contenuto |
|---|---|---|
| Molto basso | q1 123, megatec-old 138, mustek 138, zinto 138, innovatae 148, hunnox 169, megatec 173 | quasi solo tabella (~48 righe di dati) e 0–1 funzioni; stato e comandi nel `blazer-common` condiviso |
| Medio | gtec 235, voltronic-qs 242, innovart31 243, innovart33 258, mecer 284, q2 301, q6 302 | tabella + alcune conversioni dedicate |
| Alto | ablerex 470, voltronic-qs-hex 502, omron 717, bestups 729, masterguard 1090 | logica propria consistente |
| Molto alto | voltronic 4336, voltronic-axpert 4379 (414 righe di tabella, 74 funzioni) | configurazione completa di inverter/UPS Voltronic; eventualmente parziale e più avanti |

### Infrastruttura (costo una tantum)

- motore che esegue le tabelle `qx2nut` (in NUT è dentro i ~5000 righe di `nutdrv_qx.c`, insieme a tutti i trasporti e alla parte seriale);
- `blazer-common` (469 righe in NUT): bit di stato, comandi di spegnimento e test;
- riassemblaggio delle risposte, riconoscimento del sottodriver, hook per comandi e beeper, variante del replay per le risposte testuali.

Dopo l'infrastruttura, fabula/hunnox e il protocollo hunnox sopra megatec e krauler sono poche centinaia di righe in tutto, test compresi.

**Costo che non scende:** la verifica. Non abbiamo hardware Q*: ogni trasporto va provato da un tester con un apparecchio reale. Il parsing si testa bene con risposte catturate, il trasporto USB no.

---

## 6. Fasi proposte

1. **Infrastruttura**: attach per VID/PID, primitive host, `QxDriver` con motore a tabelle portato fedelmente da NUT (come `NutTable` per gli HID, ADR 0003), `blazer-common`, replay testuale.
2. **Megatec + hunnox su fabula/hunnox (+ krauler)**: copre #28 e #91. Tester: gli autori delle due issue.
3. **Cypress e gli altri trasporti a SET_REPORT** (phoenix, ippon, sgs…): copertura del mercato più ampia.
4. **Comandi e beeper** via hook, se non già fatti in fase 1.
5. **Convertitori seriali (CH340, #34)**: secondo stack host accanto a `hid_host`. Fase separata, da valutare a parte.
6. Protocolli medi/alti su richiesta degli utenti.

Prima di iniziare conviene scrivere un ADR sulle decisioni della fase 1 (motore a tabelle, hook dei comandi, coesistenza con il layer HID).

---

## 7. Fonti

- [NUT Device Dump Library (repository nut-ddl)](https://github.com/networkupstools/nut-ddl), [pagina DDL](https://www.networkupstools.org/ddl/)
- [NUT – compatibilità hardware](https://networkupstools.org/)
- [nutdrv_qx(8)](https://dyn.manpages.debian.org/testing/nut-server/nutdrv_qx.8)
- [Voltronic Power, modello ODM](https://www.marketscreener.com/quote/stock/VOLTRONIC-POWER-TECHNOLOG-19476232/news/Voltronic-Power-Technology-Corp-Reports-Earnings-Results-for-the-Second-Quarter-and-Six-Months-Ende-44588431/)
- [Confronto marche UPS 2025 (quota Schneider ~11%)](https://upsplusbattery.ca/blogs/the-battery-series-1/ups-brand-showdown-2025-vertiv-vs-eaton-vs-apc-vs-tripp-lite-for-enterprise-uptime)
- [Omdia – UPS hardware market analysis (a pagamento)](https://omdia.tech.informa.com/om030040/ups-hardware-market-analysis--2023)
- Sorgenti locali: `nut_repo/drivers/nutdrv_qx.c`, `nut_repo/drivers/nutdrv_qx_*.c`, `nut_repo/data/driver.list.in`
- Issue: #28, #34, #91 (diagnostiche USB allegate)
