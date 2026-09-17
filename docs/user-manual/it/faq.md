---
title: "Domande Frequenti"
type: manual
tags: [user-manual]
---

# Domande Frequenti (FAQ)

### Posso usare un normale ESP32 o un ESP8266?
**No.** Questo progetto richiede tassativamente un microcontrollore **ESP32-S3**. Solo la serie S3 (e la S2) dispone dell'hardware interno necessario per agire nativamente come USB Host (On-The-Go) e poter comunicare direttamente con l'UPS. I normali ESP32 non hanno questa funzionalità.

### Devo per forza fare delle saldature?
Dipende dalla scheda che acquisti. Se compri una scheda di sviluppo "Generic ESP32-S3 DevKit" con due porte USB-C, al 99% **sì**. È necessario unire due piccoli pad (chiamati `USB-OTG`) sul retro della scheda per far sì che la scheda possa erogare corrente all'UPS. Se hai competenze avanzate puoi aggirare il problema usando pin GPIO esterni e moduli step-up, ma la saldatura dei pad è la via raccomandata.

### Perché il captive portal non si apre in automatico?
Alcuni smartphone Android e versioni di iOS bloccano l'apertura automatica del captive portal per ragioni di sicurezza o se rilevano impostazioni proxy particolari. In questi casi, basta connettersi alla rete `NUT_ESP32_Config`, ignorare l'avviso "Internet non disponibile", aprire il browser e navigare su `http://192.168.4.1`.

### Posso collegare l'ESP32-NUT via cavo Ethernet (LAN)?
Il firmware è attualmente scritto e ottimizzato per lavorare tramite connessione Wi-Fi. Le schede ESP32-S3 dotate di chip Ethernet integrato (come la serie WT32-ETH01) non dispongono facilmente del supporto nativo USB Host esposto. Pertanto, l'Ethernet non è supportato.

### Il mio UPS ha una porta RJ45 o RJ11 dietro, posso usare quella?
Le porte RJ45/RJ11 presenti sul retro degli UPS (spesso etichettate "Surge Protection" o "Data Port") servono solitamente a proteggere le linee telefoniche o di rete da sovratensioni, oppure usano protocolli seriali RS232 proprietari (tramite cavi speciali RJ45-to-USB). **ESP32-NUT richiede una vera porta USB di tipo B sull'UPS** (quella quadrata, come quella delle stampanti) in quanto supporta solo il protocollo USB HID nativo.
