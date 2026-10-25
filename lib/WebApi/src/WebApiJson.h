#ifndef WEB_API_JSON_H
#define WEB_API_JSON_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "IUSBHostUPS.h"
#include "board/board_settings.h"

class WebApiJson {
public:
    // Lunghezza massima del body di POST /api/ups/command, /api/ups/variable e
    // /api/ups/variable/reset
    static constexpr size_t MAX_COMMAND_BODY_LENGTH = 256;

    static String generateUpsVars(IUSBHostUPS* usb_ups);
    // Catalogo dei comandi supportati dall'UPS (US-059, US-062):
    // {"connected", "commands": [{name, description, destructive}]}
    // Nessun blocco in Access Point (US-068): la conferma dei distruttivi la chiede la pagina
    static String generateUpsCommands(IUSBHostUPS* usb_ups);
    // Esegue un comando da body {"name": "..."}, distruttivi compresi, in STA come in AP
    // (US-068). Restituisce lo status HTTP e scrive il JSON in response
    static int runUpsCommand(IUSBHostUPS* usb_ups, const String& body, String& response);

    // --- Pagina "UPS Settings" (US-068) ---

    // True per le variabili che la pagina fa confermare prima di applicarle: ups.shutdown,
    // outlet.switchable e outlet.<N>.switchable, input.bypass.*, input.eco.switchable e
    // input.transfer.bypass.forced
    static bool needsConfirmation(const char* name);
    // Catalogo delle variabili scrivibili, nell'ordine di getWritableVariables():
    // {"connected", "variables": [{name, description, value, max_length, enum: [...],
    // driver_side, confirm}]}. Senza host o con UPS scollegato: connected false e lista vuota
    static String generateUpsVariables(IUSBHostUPS* usb_ups);
    // Scrive una variabile da body {"name": "...", "value": "..."} con la logica di SET VAR
    // (NutTable::checkSetValue, poi setVariable() col nome canonico). Il valore ammette
    // solo byte 0x20-0x7E, come il lettore di riga del server NUT. 200 {"success":true,
    // "value": <valore in UPSData dopo la scrittura, null se l'UPS si è scollegato>},
    // altrimenti 400/500/503 con {"error"}.
    // Restituisce lo status HTTP e scrive il JSON in response. Se host_called non è nullptr
    // vi scrive true quando la richiesta è arrivata a setVariable() (l'esito lo registra
    // già l'host), false per i rifiuti precedenti (400 di forma o di validazione, 503)
    static int runUpsVariable(IUSBHostUPS* usb_ups, const String& body, String& response,
                              bool* host_called = nullptr);
    // Riporta al default del driver una variabile lato driver da body {"name": "..."}:
    // 200 {"success":true, "value": <default, null come sopra>}, altrimenti 400/500/503 con
    // {"error"}. host_called
    // come in runUpsVariable, riferito a resetVariable()
    static int resetUpsVariable(IUSBHostUPS* usb_ups, const String& body, String& response,
                                bool* host_called = nullptr);

    // Nome dell'UPS della configurazione salvata (US-082): la stringa se non vuota,
    // altrimenti NUTServer::defaultUpsName() (chiave assente, null, non stringa o vuota).
    // *defaulted dice se si è usato il default
    static String upsNameFromConfig(JsonVariantConst value, bool* defaulted = nullptr);
    // Controllo di ups_name nel body di POST /api/nut/config (US-082): assente o null resta
    // il valore attuale; non stringa o non valido per NUTServer::isValidUpsName() → 400
    // {"error":"Invalid UPS name"}. Restituisce 0 se il body va bene
    static int checkNutConfigBody(JsonVariantConst body, String& response);

    // GET /api/board (US-094): {"id","label","led":{present,on,level,levels},
    // "usb":{"ports":[{id,label}],"port"}}; "led" absent on a board without LED
    static String boardSettingsJson(const BoardConfig& board, const BoardSettings& settings);
    // POST /api/board: optional led_on (bool), led_level (integer 1-5), usb_port (port id).
    // 0 with next filled and restart true when the USB port changes; otherwise 400
    // {"error"}: "Invalid request", "Invalid LED level", "Invalid USB port",
    // "No status LED on this board"
    static int checkBoardSettingsBody(JsonVariantConst body, const BoardConfig& board,
                                      const BoardSettings& current, BoardSettings& next,
                                      bool& restart, String& response);
};

#endif
