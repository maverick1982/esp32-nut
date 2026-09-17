---
type: concept
title: "Architettura Logica del Sistema ESP32-NUT"
description: "Descrive l'architettura software e logica del firmware ESP32-NUT."
tags: [architettura, diagramma, componenti]
---
# Architettura Logica del Sistema ESP32-NUT

Questo documento descrive l'architettura software e logica del firmware **ESP32-NUT**, dettagliando i livelli funzionali, i flussi di dati e l'interazione tra i vari moduli.

---

## 1. Schema Architetturale Generale

```mermaid
graph TB
    %% --- Livello Hardware ---
    subgraph Hardware ["1. Livello Hardware (ESP32-S3)"]
        USB_PORT["Porta USB Host (D+ / D-)"]
        WIFI_PHY["Modulo Radio Wi-Fi (2.4GHz)"]
        LED_PHY["LED Diagnostico GPIO"]
        NVS_STORAGE["Memoria Flash (NVS / Preferences)"]
    end

    %% --- Livello USB Host & Parsing ---
    subgraph USBHostSubsystem ["2. Sottosistema USB Host UPS (lib/USBHostUPS)"]
        USB_ENGINE["USBHostUPS<br/><i>(Driver ESP-IDF, Control EP0 & Interrupt IN)</i>"]
        PARSER["HIDParser<br/><i>(Decodifica Report Descriptor & Usages)</i>"]
        DATA_MODEL["UPSData<br/><i>(Single Source of Truth Telemetria)</i>"]

        subgraph Subdrivers ["Subdriver Specifici (IUPSDriver)"]
            GENERIC_D["GenericDriver<br/><i>(PDC Standard & Polling loop)</i>"]
            APC_D["APCDriver"]
            CYBER_D["CyberPowerDriver"]
            EATON_D["EatonDriver"]
            PWR_D["PowercomDriver"]
            OPEN_D["OpenUPSDriver"]
        end
    end

    %% --- Livello Core & Orchestrazione ---
    subgraph CoreEngine ["3. Core & Orchestrazione (src/core)"]
        MAIN["Main Orchestrator<br/><i>(setup / loop)</i>"]
        CFG["ConfigManager<br/><i>(Credenziali, Parametri, NVS)</i>"]
        LOGGER["AppLogger<br/><i>(Ring Buffer log su RAM)</i>"]
        DIAG_LED["DiagnosticLED<br/><i>(Macchina a Stati del LED)</i>"]
    end

    %% --- Livello Servizi di Rete ---
    subgraph NetworkSubsystem ["4. Sottosistema Servizi di Rete (src/network & lib/NUTServer)"]
        NET_MGR["AppNetworkManager<br/><i>(Client STA / Access Point & Captive Portal)</i>"]
        NUT_SRV["NUTServer (Porta TCP 3493)<br/><i>(Implementazione Protocollo Ufficiale NUT)</i>"]
        WEB_SRV["WebConfigServer (Porta HTTP 80)<br/><i>(Dashboard, Configurazione & Diagnostica USB)</i>"]
    end

    %% --- Attori Esterni ---
    subgraph External ["5. Periferiche & Client Esterni"]
        UPS_DEV["UPS Fisico (USB HID)"]
        NUT_CLIENTS["Client NUT<br/><i>(Home Assistant, Synology, TrueNAS, upsc)</i>"]
        BROWSER["Browser Web<br/><i>(Dashboard utente & Diagnostica)</i>"]
    end

    %% --- Connessioni Fisiche ---
    UPS_DEV <==>|"Cavo USB"| USB_PORT
    USB_PORT <==> USB_ENGINE
    LED_PHY <--- DIAG_LED
    NVS_STORAGE <===> CFG
    WIFI_PHY <===> NET_MGR

    %% --- Orchestrazione Main ---
    MAIN -->|"Inizializza & Tick"| CFG
    MAIN -->|"Inizializza & Tick"| NET_MGR
    MAIN -->|"Inizializza & Tick"| USB_ENGINE
    MAIN -->|"Inizializza & Tick"| NUT_SRV
    MAIN -->|"Inizializza & Tick"| WEB_SRV
    MAIN -->|"Aggiorna stato"| DIAG_LED

    %% --- Flusso USB ---
    USB_ENGINE -->|"Descriptor Raw"| PARSER
    PARSER -->|"Struttura Usages"| USB_ENGINE
    USB_ENGINE -->|"Assegna driver per VID/PID"| Subdrivers
    Subdrivers -->|"Aggiorna metriche decodificate"| DATA_MODEL

    %% --- Flusso Dati & Servizi ---
    DATA_MODEL -.->|"Lettura telemetria"| NUT_SRV
    DATA_MODEL -.->|"Lettura telemetria"| WEB_SRV
    USB_ENGINE -.->|"Generazione dump diagnostico"| WEB_SRV
    LOGGER -.->|"Stream log"| WEB_SRV

    %% --- Rete verso l'esterno ---
    NUT_SRV <==>|"Protocollo NUT (TCP 3493)"| NUT_CLIENTS
    WEB_SRV <==>|"HTTP (Porta 80)"| BROWSER
```

---

## 2. Diagramma di Sequenza: Flusso di Polling & Rete

Il seguente diagramma illustra come i dati viaggiano dall'UPS fisico fino ai client di monitoraggio esterni (es. Home Assistant o interfaccia Web).

```mermaid
sequenceDiagram
    autonumber
    participant UPS as UPS Fisico (USB)
    participant Engine as USBHostUPS & Driver
    participant Model as UPSData (Stato Condiviso)
    participant NUT as NUTServer (TCP 3493)
    participant Web as WebConfigServer (HTTP 80)
    participant Client as Home Assistant / Browser

    Note over Engine,UPS: Ciclo di Polling (ogni 2s) & Interrupt Asincroni
    alt Evento Spontaneo (Interrupt IN)
        UPS-->>Engine: Pacchetto Interrupt IN (Variazione stato linea/batteria)
        Engine->>Engine: decodeReport(report_id, type=1)
        Engine->>Model: Aggiorna flag stato (acPresent, discharging, ecc.)
    else Polling Periodico (Control Transfer EP0)
        Engine->>UPS: GET_REPORT (Feature / Input Report)
        UPS-->>Engine: Payload Report Dati (Tensione, Carica, Carico)
        Engine->>Engine: decodeReport(report_id, type=3) con fattori di scala
        Engine->>Model: Aggiorna metriche numeriche
    end

    Note over NUT,Client: Richiesta Client di Rete
    alt Richiesta Client NUT
        Client->>NUT: GET VAR ups battery.charge
        NUT->>Model: Legge remainingCapacity
        Model-->>NUT: 100
        NUT-->>Client: VAR ups battery.charge "100"
    else Accesso Web Dashboard
        Client->>Web: GET /api/status (o pagina HTML)
        Web->>Model: Legge snapshot telemetria
        Model-->>Web: JSON { inputVoltage: 230.0, load: 15, ... }
        Web-->>Client: 200 OK (Render Dashboard)
    end
```

---

## 3. Descrizione Funzionale dei Livelli

| Livello | Responsabilità Principali | Componenti Chiave |
| :--- | :--- | :--- |
| **1. Hardware** | Connettività fisica USB Host, modulo radio Wi-Fi, pinout GPIO LED, memoria Flash non volatile. | ESP32-S3 SoC, NVS, GPIO LED |
| **2. USB Host & Driver** | Gestione stack USB host ESP-IDF, enumerazione periferiche, parsing alberi HID, elaborazione quirks, driver specializzati per marca/modello e memorizzazione telemetria. | `USBHostUPS`, `HIDParser`, `IUPSDriver`, `UPSData` |
| **3. Core & Orchestrazione** | Ciclo di vita applicativo, fallback fail-safe (Access Point / Station), log di sistema su ring buffer RAM e indicazione visiva tramite LED diagnostico. | `main.cpp`, `ConfigManager`, `AppLogger`, `DiagnosticLED` |
| **4. Servizi di Rete** | Gestione interfaccia di rete (Wi-Fi STA o SoftAP con Captive Portal), server conforme al protocollo Network UPS Tools (NUT) su TCP 3493 e Web Server di configurazione/diagnostica su porta 80. | `AppNetworkManager`, `NUTServer`, `WebConfigServer` |
| **5. Client Esterni** | Dispositivi UPS monitorati, piattaforme di domotica e automazione (Home Assistant), NAS (Synology, QNAP, TrueNAS), client CLI `upsc` e browser web dell'utente. | Client NUT, Dashboard Web, UPS |

