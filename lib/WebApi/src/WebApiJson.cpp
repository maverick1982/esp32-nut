#include "WebApiJson.h"
#include "NUTServer.h"

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

    // Un comando sconosciuto al catalogo viene rifiutato prima di qualsiasi chiamata
    // all'host. I distruttivi non hanno blocchi: in AP come in STA (US-068)
    const UPSCommandInfo* info = CommandCatalog::find(name);
    if (!info) {
        response = "{\"error\":\"Command not supported\"}";
        return 400;
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
        case CommandResult::INVALID_ARGUMENT:
            response = "{\"error\":\"Invalid request\"}";
            return 400;
        case CommandResult::FAILED:
        default:
            response = "{\"error\":\"Command rejected by the UPS\"}";
            return 500;
    }
}

// --- Pagina "UPS Settings" (US-068) ---

namespace {

int sendError(String& response, int status, const String& message) {
    // I messaggi sono costanti senza caratteri da escapare
    response = String("{\"error\":\"") + message + "\"}";
    return status;
}

// {"success":true,"value":"..."}: il valore viene dall'UPS e va serializzato con escape
// 200 con il valore in UPSData dopo la scrittura o il reset. UPS scollegato nel frattempo:
// value null, il valore vale dalla connessione successiva
int sendValue(String& response, IUSBHostUPS* usb_ups, const String& name) {
    JsonDocument doc;
    doc["success"] = true;
    if (usb_ups->isConnected()) doc["value"] = usb_ups->getUPSData()->get(name).c_str();
    else doc["value"] = nullptr;
    std::string out_std;
    serializeJson(doc, out_std);
    response = String(out_std.c_str());
    return 200;
}

bool startsWithIgnoreCase(const char* s, const char* prefix) {
    for (; *prefix; s++, prefix++) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
    }
    return true;
}

// Body {"name"} o {"name","value"} (with_value): JSON valido, name stringa non vuota e,
// se richiesto, value stringa JSON (un numero non è un valore NUT). Nessun accesso
// all'host. name e value puntano dentro req
bool parseVariableBody(const String& body, bool with_value, JsonDocument& req,
                       const char*& name, const char*& value) {
    name = nullptr;
    value = nullptr;
    if (body.length() > WebApiJson::MAX_COMMAND_BODY_LENGTH) return false;
    JsonDocument filter;
    filter["name"] = true;
    if (with_value) filter["value"] = true;
    if (deserializeJson(req, body.c_str(), DeserializationOption::Filter(filter))) return false;
    if (!req["name"].is<const char*>()) return false;
    name = req["name"].as<const char*>();
    if (!name || name[0] == '\0') return false;
    if (with_value) {
        if (!req["value"].is<const char*>()) return false;
        value = req["value"].as<const char*>();
        if (!value) return false;
    }
    return true;
}

// Solo byte 0x20-0x7E, come il lettore di riga del server NUT: un SET VAR non può
// portare a capo o caratteri di controllo, quindi nemmeno la Web UI (LIST VAR, NVS, log)
bool isPrintableAscii(const char* s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Host assente o scollegato, poi dati stale (DATA-STALE di upsd): 0 se l'host può scrivere
int checkHost(IUSBHostUPS* usb_ups, String& response) {
    if (!usb_ups || !usb_ups->isConnected()) return sendError(response, 503, "UPS not connected");
    if (usb_ups->isDataStale()) return sendError(response, 503, "UPS data stale");
    return 0;
}

} // namespace

bool WebApiJson::needsConfirmation(const char* name) {
    if (!name) return false;
    if (NutTable::equalsIgnoreCase(name, "ups.shutdown") ||
        NutTable::equalsIgnoreCase(name, "outlet.switchable") ||
        NutTable::equalsIgnoreCase(name, "input.eco.switchable") ||
        NutTable::equalsIgnoreCase(name, "input.transfer.bypass.forced")) {
        return true;
    }
    // input.bypass.*: almeno un carattere dopo il prefisso
    static const char kBypass[] = "input.bypass.";
    if (startsWithIgnoreCase(name, kBypass) && name[sizeof(kBypass) - 1] != '\0') return true;
    // outlet.<N>.switchable, N di sole cifre
    static const char kOutlet[] = "outlet.";
    if (startsWithIgnoreCase(name, kOutlet)) {
        const char* p = name + sizeof(kOutlet) - 1;
        if (!isdigit((unsigned char)*p)) return false;
        while (isdigit((unsigned char)*p)) p++;
        return NutTable::equalsIgnoreCase(p, ".switchable");
    }
    return false;
}

String WebApiJson::generateUpsVariables(IUSBHostUPS* usb_ups) {
    JsonDocument doc;
    bool connected = usb_ups && usb_ups->isConnected();
    doc["connected"] = connected;
    JsonArray variables = doc["variables"].to<JsonArray>();

    if (connected) {
        // Catalogo letto prima del lock dei dati: getWritableVariables() prende i suoi lock
        std::vector<NutTable::RWVariable> rw = usb_ups->getWritableVariables();
        IUSBHostUPS::UPSDataLock data = usb_ups->getUPSData();
        for (const auto& v : rw) {
            JsonObject obj = variables.add<JsonObject>();
            obj["name"] = v.name;
            // Come il server NUT per GET DESC
            const char* desc = NutTable::describe(v.name);
            obj["description"] = desc ? desc : "Description unavailable";
            obj["value"] = data->get(v.name);
            obj["max_length"] = v.aux;
            JsonArray enums = obj["enum"].to<JsonArray>();
            for (const char* e : v.enums) enums.add(e);
            obj["driver_side"] = v.driver_side;
            obj["confirm"] = needsConfirmation(v.name);
        }
    }

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}

int WebApiJson::runUpsVariable(IUSBHostUPS* usb_ups, const String& body, String& response, bool* host_called) {
    if (host_called) *host_called = false;
    // L'ordine dei controlli fa parte del contratto (piano US-068, sezione 3): forma del
    // body, host, validazione di SET VAR, scrittura
    JsonDocument req;
    const char* name;
    const char* value;
    if (!parseVariableBody(body, true, req, name, value)) return sendError(response, 400, "Invalid request");
    if (!isPrintableAscii(value)) return sendError(response, 400, "Invalid value");
    int status = checkHost(usb_ups, response);
    if (status) return status;

    // Una variabile assente dal catalogo LIST RW non è scrivibile; aux < 1 dà 400 e non
    // 500, perché all'UPS non arriva nulla
    std::vector<NutTable::RWVariable> rw = usb_ups->getWritableVariables();
    const NutTable::RWVariable* var = nullptr;
    switch (NutTable::checkSetValue(rw, name, value, var)) {
        case NutTable::SetCheck::OK:
            break;
        case NutTable::SetCheck::TOO_LONG:
            return sendError(response, 400, String("Value too long (max ") + String((unsigned)var->aux) + ")");
        case NutTable::SetCheck::INVALID_VALUE:
            return sendError(response, 400, "Invalid value");
        case NutTable::SetCheck::NOT_WRITABLE:
        case NutTable::SetCheck::INVALID_AUX:
        default:
            return sendError(response, 400, "Variable not writable");
    }

    // Nome canonico del catalogo, non quello del client
    String canonical = var->name;
    if (host_called) *host_called = true;
    switch (usb_ups->setVariable(canonical.c_str(), value)) {
        case NutTable::SetStatus::OK:
            return sendValue(response, usb_ups, canonical);
        case NutTable::SetStatus::CONVERSION_FAILED:
            return sendError(response, 400, "Invalid value");
        case NutTable::SetStatus::UNKNOWN:
            return sendError(response, 400, "Variable not supported");
        case NutTable::SetStatus::NOT_CONNECTED:
            return sendError(response, 503, "UPS not connected");
        case NutTable::SetStatus::FAILED:
        default:
            return sendError(response, 500, "Rejected by the UPS");
    }
}

int WebApiJson::resetUpsVariable(IUSBHostUPS* usb_ups, const String& body, String& response, bool* host_called) {
    if (host_called) *host_called = false;
    JsonDocument req;
    const char* name;
    const char* value;
    if (!parseVariableBody(body, false, req, name, value)) return sendError(response, 400, "Invalid request");
    int status = checkHost(usb_ups, response);
    if (status) return status;

    // Solo le variabili del catalogo con driver_side (ups.delay.*, outlet.*.desc)
    String canonical;
    for (const auto& v : usb_ups->getWritableVariables()) {
        if (v.driver_side && NutTable::equalsIgnoreCase(v.name, name)) {
            canonical = v.name;
            break;
        }
    }
    if (canonical.length() == 0) return sendError(response, 400, "Not a driver-side variable");

    if (host_called) *host_called = true;
    switch (usb_ups->resetVariable(canonical.c_str())) {
        case NutTable::SetStatus::OK:
            return sendValue(response, usb_ups, canonical);
        case NutTable::SetStatus::UNKNOWN:
            return sendError(response, 400, "Not a driver-side variable");
        case NutTable::SetStatus::NOT_CONNECTED:
            return sendError(response, 503, "UPS not connected");
        case NutTable::SetStatus::FAILED:
        default:
            return sendError(response, 500, "Could not save the setting");
    }
}

String WebApiJson::upsNameFromConfig(JsonVariantConst value, bool* defaulted) {
    // Not as<String>(): with ArduinoJson 7 a missing key gives the string "null"
    const char* name = value.is<const char*>() ? value.as<const char*>() : nullptr;
    bool use_default = !name || name[0] == '\0';
    if (defaulted) *defaulted = use_default;
    return use_default ? NUTServer::defaultUpsName() : String(name);
}

int WebApiJson::checkNutConfigBody(JsonVariantConst body, String& response) {
    JsonVariantConst name = body["ups_name"];
    if (name.isNull()) return 0;
    if (!name.is<const char*>() || !NUTServer::isValidUpsName(String(name.as<const char*>()))) {
        return sendError(response, 400, "Invalid UPS name");
    }
    return 0;
}
