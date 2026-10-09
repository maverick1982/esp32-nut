#include "WebApiJson.h"

String WebApiJson::generateUpsVars(IUSBHostUPS* usb_ups) {
    if (!usb_ups) {
        return "{\"error\": \"UPS non inizializzato\"}";
    }
    
    JsonDocument doc;
    
    if (!usb_ups->isConnected()) {
        doc["_disconnected"] = true;
        doc["ups.status"] = "Disconnected";
        std::string out_std;
        serializeJson(doc, out_std);
        return String(out_std.c_str());
    }
    
    auto data = usb_ups->getUPSData();

    doc["ups.status"] = usb_ups->getUPSStatusString();
    if (usb_ups->isDataStale()) {
        doc["_stale"] = true;
    }
    
    for (const auto& param : data->getAll()) {
        if (param.key.startsWith("ups.status.") && param.key != "ups.status") {
            continue; // Skip internal status flags
        }
        
        bool isNumeric = true;
        bool hasDot = false;
        if (param.value.length() == 0) isNumeric = false;
        for (int i = 0; i < param.value.length(); i++) {
            if (i == 0 && param.value[i] == '-') continue;
            if (param.value[i] == '.') {
                if (hasDot) { isNumeric = false; break; }
                hasDot = true;
                continue;
            }
            if (!isdigit(param.value[i])) {
                isNumeric = false;
                break;
            }
        }
        
        if (isNumeric) {
            if (hasDot) doc[param.key] = serialized(param.value); // Use serialized for floats to preserve formatting
            else doc[param.key] = param.value.toInt();
        } else {
            doc[param.key] = param.value;
        }
    }
    
    doc["ups.beeper.switchable"] = usb_ups->supportsBeeperToggle();

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}

String WebApiJson::generateUpsCommands(IUSBHostUPS* usb_ups) {
    JsonDocument doc;
    bool connected = usb_ups && usb_ups->isConnected();
    doc["connected"] = connected;
    JsonArray commands = doc["commands"].to<JsonArray>();

    if (connected) {
        for (const auto* c : usb_ups->getSupportedCommands()) {
            JsonObject obj = commands.add<JsonObject>();
            obj["name"] = c->name;
            obj["description"] = c->description;
            obj["destructive"] = c->destructive;
        }
    }

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}

int WebApiJson::runUpsCommand(IUSBHostUPS* usb_ups, const String& body, String& response) {
    // Serve solo "name": body lunghi rifiutati subito e filtro sul resto,
    // così un JSON grande non consuma heap
    if (body.length() > MAX_COMMAND_BODY_LENGTH) {
        response = "{\"error\":\"Invalid request\"}";
        return 400;
    }
    JsonDocument filter;
    filter["name"] = true;
    JsonDocument req;
    DeserializationError err = deserializeJson(req, body.c_str(), DeserializationOption::Filter(filter));
    const char* name = err ? nullptr : req["name"].as<const char*>();
    if (!name || name[0] == '\0') {
        response = "{\"error\":\"Invalid request\"}";
        return 400;
    }

    // L'ordine dei controlli fa parte del contratto: un comando distruttivo
    // viene rifiutato prima di qualsiasi chiamata all'host
    const UPSCommandInfo* info = CommandCatalog::find(name);
    if (!info) {
        response = "{\"error\":\"Command not supported\"}";
        return 400;
    }
    if (info->destructive) {
        response = "{\"error\":\"Available via NUT only\"}";
        return 403;
    }
    if (!usb_ups) {
        response = "{\"error\":\"UPS not connected\"}";
        return 503;
    }

    switch (usb_ups->executeCommand(name)) {
        case CommandResult::OK:
            response = "{\"success\":true}";
            return 200;
        case CommandResult::NOT_SUPPORTED:
            response = "{\"error\":\"Command not supported\"}";
            return 400;
        case CommandResult::NOT_CONNECTED:
            response = "{\"error\":\"UPS not connected\"}";
            return 503;
        case CommandResult::FAILED:
        default:
            response = "{\"error\":\"Command rejected by the UPS\"}";
            return 500;
    }
}
