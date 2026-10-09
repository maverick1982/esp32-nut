#ifndef WEB_API_JSON_H
#define WEB_API_JSON_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "IUSBHostUPS.h"

class WebApiJson {
public:
    // Lunghezza massima del body di POST /api/ups/command
    static constexpr size_t MAX_COMMAND_BODY_LENGTH = 256;

    static String generateUpsVars(IUSBHostUPS* usb_ups);
    // Catalogo dei comandi supportati dall'UPS (US-059): {"connected", "commands": [{name, description, destructive}]}
    static String generateUpsCommands(IUSBHostUPS* usb_ups);
    // Esegue un comando non distruttivo da body {"name": "..."}; restituisce lo status HTTP e scrive il JSON in response
    static int runUpsCommand(IUSBHostUPS* usb_ups, const String& body, String& response);
};

#endif
