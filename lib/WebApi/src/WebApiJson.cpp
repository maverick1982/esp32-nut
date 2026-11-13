#include "WebApiJson.h"
#include "NUTServer.h"
#include "NetHostname.h"
#include <string.h>

String WebApiJson::generateUpsVars(IUpsSource* usb_ups) {
    if (!usb_ups) {
        return "{\"error\": \"UPS non inizializzato\"}";
    }
    
    JsonDocument doc;
    
    if (!usb_ups->isConnected()) {
        doc["_disconnected"] = true;
        doc["ups.status"] = "Disconnected";
        // A Q* UPS recognised but not supported yet (US-132)
        String unsupported = usb_ups->unsupportedDevice();
        if (unsupported.length() > 0) doc["_unsupported"] = unsupported;
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

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}

void WebApiJson::upsStatusJson(JsonObject ups, IUpsSource* src) {
    if (src && src->isConnected()) {
        {
            auto data = src->getUPSData();
            ups["status"] = data->hasKey("ups.model") ? data->get("ups.model") : String("Unknown Model");
        }
        // The stale banner is for an attached UPS, and not for the first seconds after the
        // claim, while the first full poll runs
        if (src->isDataStale() && !src->isWaitingFirstData()) ups["stale"] = true;
        return;
    }
    String unsupported = src ? src->unsupportedDevice() : String();
    if (unsupported.length() > 0) {
        // US-132: a Q* UPS recognised but not supported yet
        ups["status"] = "Not supported yet";
        ups["unsupported"] = unsupported;
        return;
    }
    ups["status"] = "Disconnected";
}

String WebApiJson::generateUpsCommands(IUpsSource* usb_ups) {
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

int WebApiJson::runUpsCommand(IUpsSource* usb_ups, const String& body, String& response) {
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
    const UPSCommandInfo* info = UpsCommands::find(name);
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
int sendValue(String& response, IUpsSource* usb_ups, const String& name) {
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
int checkHost(IUpsSource* usb_ups, String& response) {
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

String WebApiJson::generateUpsVariables(IUpsSource* usb_ups, const std::vector<NutOverride>* overrides) {
    JsonDocument doc;
    bool connected = usb_ups && usb_ups->isConnected();
    doc["connected"] = connected;
    JsonArray variables = doc["variables"].to<JsonArray>();

    if (connected) {
        // Catalogo letto prima del lock dei dati: getWritableVariables() prende i suoi lock
        std::vector<NutTable::RWVariable> rw = usb_ups->getWritableVariables();
        IUpsSource::UPSDataLock data = usb_ups->getUPSData();
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
            // US-115: read-only while the NUT Server page overrides it
            if (const NutOverride* o = NutOverrides::find(overrides, v.name)) obj["override"] = o->value;
        }
    }

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}

int WebApiJson::runUpsVariable(IUpsSource* usb_ups, const String& body, String& response, bool* host_called,
                               const std::vector<NutOverride>* overrides) {
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
    // US-115: immutable while overridden, as SET VAR (READONLY)
    if (NutOverrides::find(overrides, name)) return sendError(response, 400, OVERRIDDEN_VARIABLE_ERROR);

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

int WebApiJson::resetUpsVariable(IUpsSource* usb_ups, const String& body, String& response, bool* host_called,
                                 const std::vector<NutOverride>* overrides) {
    if (host_called) *host_called = false;
    JsonDocument req;
    const char* name;
    const char* value;
    if (!parseVariableBody(body, false, req, name, value)) return sendError(response, 400, "Invalid request");
    int status = checkHost(usb_ups, response);
    if (status) return status;
    if (NutOverrides::find(overrides, name)) return sendError(response, 400, OVERRIDDEN_VARIABLE_ERROR);

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

int WebApiJson::nutConfigFromBody(JsonVariantConst body, const NUTServerConfig& current, NUTServerConfig& next,
                                  String& response) {
    next = current;
    if (int status = checkNutConfigBody(body, response)) return status;
    if (body["ups_name"].is<const char*>()) next.ups_name = body["ups_name"].as<const char*>();

    // is<const char*>() before reading: as<String>() of a missing key is "null"
    JsonVariantConst user = body["username"];
    JsonVariantConst pass = body["password"];
    bool has_user = user.is<const char*>() && strlen(user.as<const char*>()) > 0;
    bool has_pass = pass.is<const char*>() && strlen(pass.as<const char*>()) > 0;

    JsonVariantConst remove = body["remove_credentials"];
    if (!remove.isNull() && !remove.is<bool>()) return sendError(response, 400, "Invalid request");
    if (remove.is<bool>() && remove.as<bool>()) {
        if (has_user || has_pass) return sendError(response, 400, "Invalid request");
        next.username = "";
        next.password = "";
        return 0;
    }

    if (user.is<const char*>()) next.username = user.as<const char*>();
    if (has_pass) next.password = pass.as<const char*>();
    if ((next.username.length() > 0) != (next.password.length() > 0)) {
        return sendError(response, 400, "Set both the NUT username and password, or remove the NUT credentials");
    }
    return 0;
}

String WebApiJson::nutConfigAppliedJson(bool applied, bool changed, unsigned sessions_closed) {
    JsonDocument doc;
    doc["success"] = true;
    doc["applied"] = applied;
    doc["restart"] = false;
    doc["changed"] = changed;
    doc["sessions_closed"] = sessions_closed;
    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}

String WebApiJson::boardSettingsJson(const BoardConfig& board, const BoardSettings& settings,
                                     AutoShutdownAvailability auto_shutdown, const BoardQxInfo* qx) {
    JsonDocument doc;
    doc["id"] = board.name;
    doc["label"] = board.label;
    if (hasLed(board)) {
        JsonObject led = doc["led"].to<JsonObject>();
        led["present"] = true;
        led["on"] = settings.led_on;
        led["level"] = settings.led_level;
        led["levels"] = LED_LEVELS;
    }
    JsonObject usb = doc["usb"].to<JsonObject>();
    JsonArray ports = usb["ports"].to<JsonArray>();
    for (uint8_t i = 0; i < board.usb.port_count; i++) {
        JsonObject p = ports.add<JsonObject>();
        p["id"] = board.usb.ports[i].id;
        p["label"] = board.usb.ports[i].label;
    }
    usb["port"] = board.usb.ports[settings.usb_port].id;
    // US-114: the switch, and why it cannot be turned on
    JsonObject sd = doc["auto_shutdown"].to<JsonObject>();
    sd["on"] = settings.auto_shutdown;
    sd["available"] = auto_shutdown == AutoShutdownAvailability::AVAILABLE;
    if (auto_shutdown == AutoShutdownAvailability::NO_UPS) sd["reason"] = "no_ups";
    if (auto_shutdown == AutoShutdownAvailability::NO_COMMAND) sd["reason"] = "no_command";
    // US-138: the Q* driver section, only with a Q* driver bound
    if (qx) {
        JsonObject q = doc["qx"].to<JsonObject>();
        q["protocol"] = settings.qx_protocol;
        q["novendor"] = settings.qx_novendor;
        JsonArray names = q["protocols"].to<JsonArray>();
        char name[QX_PROTOCOL_NAME_MAX];
        for (const QxSubdriver* const* s = QX_SUBDRIVER_LIST; *s; s++) {
            qxProtocolName(**s, name, sizeof(name));
            names.add(String(name));
        }
        q["detected"] = qx->detected ? qx->detected : "";
    }
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

int WebApiJson::checkBoardSettingsBody(JsonVariantConst body, const BoardConfig& board,
                                       const BoardSettings& current, AutoShutdownAvailability auto_shutdown,
                                       BoardSettings& next, bool& restart, String& response) {
    next = current;
    restart = false;
    if (!body.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");
    JsonObjectConst obj = body.as<JsonObjectConst>();

    JsonVariantConst led_on = obj["led_on"];
    JsonVariantConst led_level = obj["led_level"];
    if ((!led_on.isUnbound() || !led_level.isUnbound()) && !hasLed(board)) {
        return sendError(response, 400, "No status LED on this board");
    }
    if (!led_on.isUnbound()) {
        if (!led_on.is<bool>()) return sendError(response, 400, "Invalid request");
        next.led_on = led_on.as<bool>();
    }
    if (!led_level.isUnbound()) {
        // An integer only: true, 2.5 or "3" are rejected
        if (!led_level.is<int>()) return sendError(response, 400, "Invalid LED level");
        int level = led_level.as<int>();
        if (level < 1 || level > LED_LEVELS) return sendError(response, 400, "Invalid LED level");
        next.led_level = (uint8_t)level;
    }

    JsonVariantConst port = obj["usb_port"];
    if (!port.isUnbound()) {
        if (!port.is<const char*>()) return sendError(response, 400, "Invalid USB port");
        const char* id = port.as<const char*>();
        uint8_t found = board.usb.port_count;
        for (uint8_t i = 0; i < board.usb.port_count; i++) {
            if (strcmp(board.usb.ports[i].id, id) == 0) found = i;
        }
        if (found == board.usb.port_count) return sendError(response, 400, "Invalid USB port");
        next.usb_port = found;
        restart = found != current.usb_port;
    }

    // US-114: turning it off is always allowed, on only when the UPS offers shutdown.default
    JsonVariantConst sd = obj["auto_shutdown"];
    if (!sd.isUnbound()) {
        if (!sd.is<bool>()) return sendError(response, 400, "Invalid request");
        bool on = sd.as<bool>();
        if (on && auto_shutdown == AutoShutdownAvailability::NO_UPS) {
            return sendError(response, 400, "UPS not connected");
        }
        if (on && auto_shutdown == AutoShutdownAvailability::NO_COMMAND) {
            return sendError(response, 400, "The UPS offers no shutdown command");
        }
        next.auto_shutdown = on;
    }

    // US-138: protocol and novendor of the Q* driver, board configuration as ups.conf
    JsonVariantConst proto = obj["qx_protocol"];
    if (!proto.isUnbound()) {
        if (!proto.is<const char*>()) return sendError(response, 400, "Invalid protocol");
        const char* name = proto.as<const char*>();
        if (*name == '\0') {
            next.qx_protocol[0] = '\0';
        } else {
            const QxSubdriver* p = qxFindProtocol(name);
            if (!p) return sendError(response, 400, "Invalid protocol");
            qxProtocolName(*p, next.qx_protocol, sizeof(next.qx_protocol));
        }
    }
    JsonVariantConst novendor = obj["qx_novendor"];
    if (!novendor.isUnbound()) {
        if (!novendor.is<bool>()) return sendError(response, 400, "Invalid request");
        next.qx_novendor = novendor.as<bool>();
    }
    return 0;
}

static const char* netStateName(NetLinkState state) {
    switch (state) {
        case NetLinkState::CONNECTED: return "connected";
        case NetLinkState::CONNECTING: return "connecting";
        case NetLinkState::ACCESS_POINT: return "ap";
        default: return "disconnected";
    }
}

// An address the link does not have: empty, or 0.0.0.0 from an IPAddress without a value
static bool hasAddress(const String& a) {
    return a.length() > 0 && a != "0.0.0.0";
}

void WebApiJson::networkStatusJson(JsonObject net, const NetLinkInfo& info) {
    net["type"] = info.type;
    net["state"] = netStateName(info.state);
    if (hasAddress(info.ip)) net["ip"] = info.ip;
    if (hasAddress(info.gateway)) net["gateway"] = info.gateway;
    bool mac_set = false;
    for (int i = 0; i < 6; i++) mac_set = mac_set || info.mac[i] != 0;
    if (mac_set) {
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", info.mac[0], info.mac[1],
                 info.mac[2], info.mac[3], info.mac[4], info.mac[5]);
        net["mac"] = mac;
    }
    // The access point is the DHCP server, not a client
    if (info.state != NetLinkState::ACCESS_POINT) net["dhcp"] = info.dhcp;
    if (strcmp(info.type, "wifi") == 0) {
        if (info.ssid.length() > 0) net["ssid"] = info.ssid;
        if (info.has_rssi) net["rssi"] = info.rssi;
    } else if (strcmp(info.type, "ethernet") == 0 && info.link_speed_mbps > 0) {
        net["link_speed"] = info.link_speed_mbps;
        net["full_duplex"] = info.full_duplex;
    }
}

void WebApiJson::networkLinksJson(JsonArray links, const BoardConfig& board) {
    networkLinksJson(links, board, false);
}

void WebApiJson::networkLinksJson(JsonArray links, const BoardConfig& board, bool spi_eth_enabled) {
    if (board.has_wifi) links.add("wifi");
    if (board.has_ethernet || spi_eth_enabled) links.add("ethernet");
}

// --- US-126: W5500 wired by the user ---

// JSON keys of the pins, in the order of ethSpiPin()
static const char* const ETH_SPI_KEYS[ETH_SPI_PIN_COUNT] = { "sck", "mosi", "miso", "cs", "irq", "rst" };

static void setEthSpiPin(EthSpiSettings& s, uint8_t index, int8_t pin) {
    switch (index) {
        case 0: s.sck = pin; break;
        case 1: s.mosi = pin; break;
        case 2: s.miso = pin; break;
        case 3: s.cs = pin; break;
        case 4: s.irq = pin; break;
        case 5: s.rst = pin; break;
        default: break;
    }
}

// mode == nullptr: the US-126 reply, without "mode"
static String ethSpiJsonImpl(const BoardConfig& board, const EthSpiSettings& settings, const NetworkMode* mode,
                             const char* state, const char* reason) {
    JsonDocument doc;
    doc["available"] = board.spi_eth.available;
    doc["enabled"] = settings.enabled;
    if (mode) doc["mode"] = networkModeName(*mode);
    JsonObject pins = doc["pins"].to<JsonObject>();
    for (uint8_t i = 0; i < ETH_SPI_PIN_COUNT; i++) {
        const int8_t pin = ethSpiPin(settings, i);
        if (pin >= 0) {
            pins[ETH_SPI_KEYS[i]] = pin;
        } else {
            pins[ETH_SPI_KEYS[i]] = nullptr;
        }
    }
    JsonArray strapping = doc["strapping"].to<JsonArray>();
    for (uint8_t i = 0; i < board.spi_eth.strapping_count && i < 4; i++) strapping.add(board.spi_eth.strapping[i]);
    JsonObject status = doc["status"].to<JsonObject>();
    status["state"] = state;
    if (reason && reason[0] != '\0') status["reason"] = reason;
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

String WebApiJson::ethSpiJson(const BoardConfig& board, const EthSpiSettings& settings, const char* state,
                              const char* reason) {
    return ethSpiJsonImpl(board, settings, nullptr, state, reason);
}

String WebApiJson::ethSpiJson(const BoardConfig& board, const EthSpiSettings& settings, NetworkMode mode,
                              const char* state, const char* reason) {
    return ethSpiJsonImpl(board, settings, &mode, state, reason);
}

int WebApiJson::checkEthSpiBody(JsonVariantConst body, const BoardConfig& board, const EthSpiSettings& current,
                                EthSpiSettings& next, bool& restart, String& response) {
    // The US-126 contract has no mode: a "mode" key is ignored, so it neither refuses the
    // request nor asks for a restart that would save nothing
    JsonDocument pins_only;
    pins_only.set(body);
    if (pins_only.is<JsonObject>()) pins_only.remove("mode");
    NetworkMode next_mode = NetworkMode::AUTO;
    return checkEthSpiBody(pins_only.as<JsonVariantConst>(), board, current, NetworkMode::AUTO, next, next_mode,
                           restart, response);
}

int WebApiJson::checkEthSpiBody(JsonVariantConst body, const BoardConfig& board, const EthSpiSettings& current,
                                NetworkMode current_mode, EthSpiSettings& next, NetworkMode& next_mode,
                                bool& restart, String& response) {
    next = current;
    next_mode = current_mode;
    restart = false;
    if (!body.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");
    JsonVariantConst enabled = body["enabled"];
    if (!enabled.is<bool>()) return sendError(response, 400, "Invalid request");
    JsonVariantConst pins = body["pins"];
    if (!pins.isNull() && !pins.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");

    EthSpiSettings candidate = ethSpiDefaults();
    candidate.enabled = enabled.as<bool>();
    for (uint8_t i = 0; i < ETH_SPI_PIN_COUNT; i++) {
        JsonVariantConst v = pins[ETH_SPI_KEYS[i]];
        if (v.isNull()) continue;  // missing or null: not set
        // An integer only: true, 2.5 or "12" are rejected
        if (!v.is<long>()) return sendError(response, 400, "Invalid request");
        const long gpio = v.as<long>();
        if (gpio < -1 || gpio > 127) {
            // Outside int8_t: the same message the check gives for a GPIO the chip lacks
            char msg[64];
            snprintf(msg, sizeof(msg), "GPIO %ld does not exist on this board", gpio);
            return sendError(response, 400, msg);
        }
        setEthSpiPin(candidate, i, (int8_t)gpio);
    }

    EthPinCheck check;
    if (!checkEthSpiSettings(board, candidate, check)) return sendError(response, 400, check.message);

    // US-127: optional; missing keeps the saved mode, anything but a known name is refused
    NetworkMode candidate_mode = current_mode;
    JsonVariantConst mode = body.as<JsonObjectConst>()["mode"];
    if (!mode.isUnbound()) {
        if (!mode.is<const char*>() || !parseNetworkMode(mode.as<const char*>(), candidate_mode)) {
            return sendError(response, 400, INVALID_NETWORK_MODE_ERROR);
        }
    }

    next = candidate;
    next_mode = candidate_mode;
    restart = next != current || next_mode != current_mode;
    JsonDocument doc;
    doc["success"] = true;
    doc["restart"] = restart;
    JsonArray warnings = doc["warnings"].to<JsonArray>();
    int8_t straps[ETH_SPI_PIN_COUNT];
    const uint8_t n = strappingWarnings(board, next, straps);
    for (uint8_t i = 0; i < n; i++) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "GPIO %d is a strapping pin: the board may not boot if the module pulls it at power-on",
                 (int)straps[i]);
        warnings.add(msg);
    }
    std::string out;
    serializeJson(doc, out);
    response = String(out.c_str());
    return 0;
}

int WebApiJson::checkHostnameBody(JsonVariantConst body, String& next, String& response) {
    JsonVariantConst value = body["hostname"];
    if (!value.is<const char*>()) return sendError(response, 400, "Invalid request");
    const char* name = value.as<const char*>();
    if (name[0] != '\0' && !NetHostname::isValid(name)) {
        return sendError(response, 400, INVALID_HOSTNAME_ERROR);
    }
    next = String(name);
    return 0;
}

void WebApiJson::hostnameJson(JsonObject out, const String& in_use, const String& saved, const String& def) {
    out["name"] = in_use;
    out["custom"] = saved;
    out["default"] = def;
}

String WebApiJson::nutOverridesJson(const std::vector<NutOverride>& list, IUpsSource* usb_ups) {
    JsonDocument doc;
    doc["max"] = (unsigned)NutOverrides::MAX_COUNT;
    doc["max_value_length"] = (unsigned)NutOverrides::MAX_VALUE_LENGTH;
    doc["max_description_length"] = (unsigned)NutOverrides::MAX_DESCRIPTION_LENGTH;
    JsonArray arr = doc["overrides"].to<JsonArray>();
    bool connected = usb_ups && usb_ups->isConnected();
    for (const auto& o : list) {
        JsonObject obj = arr.add<JsonObject>();
        obj["key"] = o.key;
        obj["value"] = o.value;
        obj["description"] = o.description;
        obj["ups_value"] = nullptr;
        if (!connected) continue;
        // The real value the override replaces: the page shows "Overrides UPS" or "Added"
        String key = o.key;
        key.toLowerCase();
        const char* alias = NUTServer::deviceAliasOf(key);
        String source = alias ? String(alias) : key;
        IUpsSource::UPSDataLock data = usb_ups->getUPSData();
        if (data->hasKey(source)) obj["ups_value"] = data->get(source);
    }
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

int WebApiJson::checkNutOverridesBody(const String& body, std::vector<NutOverride>& next, String& response) {
    next.clear();
    if (body.length() > MAX_OVERRIDES_BODY_LENGTH) return sendError(response, 400, "Invalid request");
    JsonDocument req;
    if (deserializeJson(req, body.c_str())) return sendError(response, 400, "Invalid request");
    JsonVariantConst items = req["overrides"];
    if (!items.is<JsonArrayConst>()) return sendError(response, 400, "Invalid request");
    for (JsonVariantConst item : items.as<JsonArrayConst>()) {
        JsonVariantConst desc = item["description"];
        if (!item["key"].is<const char*>() || !item["value"].is<const char*>() ||
            !(desc.isNull() || desc.is<const char*>())) {
            next.clear();
            return sendError(response, 400, "Invalid request");
        }
        NutOverride o;
        o.key = item["key"].as<const char*>();
        o.value = item["value"].as<const char*>();
        o.description = desc.is<const char*>() ? desc.as<const char*>() : "";
        next.push_back(o);
    }
    size_t bad = 0;
    NutOverrides::Check c = NutOverrides::checkList(next, bad);
    if (c != NutOverrides::Check::OK) {
        // The message carries the key typed by the user: serialized with escapes
        JsonDocument err;
        err["error"] = NutOverrides::errorMessage(c, bad < next.size() ? next[bad].key : String());
        err["index"] = (unsigned)bad;
        std::string out;
        serializeJson(err, out);
        response = String(out.c_str());
        next.clear();
        return 400;
    }
    return 0;
}

// --- Remote logging (US-116) ---

String WebApiJson::diagReadJson(const DiagReadStatus& status) {
    const char* state = "idle";
    switch (status.state) {
        case DiagReadState::Reading:     state = "reading"; break;
        case DiagReadState::Done:        state = "done"; break;
        case DiagReadState::Interrupted: state = "interrupted"; break;
        default: break;
    }
    JsonDocument doc;
    doc["state"] = state;
    doc["total"] = status.total;
    doc["done"] = status.done;
    doc["read"] = status.read;
    doc["empty"] = status.empty;
    doc["failed"] = status.failed;
    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

String WebApiJson::remoteLogJson(const SyslogSettings& syslog, const SyslogStatus& status) {
    JsonDocument doc;
    JsonObject sl = doc["syslog"].to<JsonObject>();
    sl["enabled"] = syslog.enabled;
    sl["host"] = syslog.host.c_str();
    sl["port"] = syslog.port;
    sl["protocol"] = syslogProtocolName(syslog.protocol);
    const char* fac = syslogFacilityName(syslog.facility);
    sl["facility"] = fac ? fac : "local0";
    sl["min_level"] = logLevelName(syslog.min_level);
    sl["rate"] = syslog.rate;

    JsonObject st = doc["status"]["syslog"].to<JsonObject>();
    st["state"] = syslogStateName(status.state);
    st["target"] = status.target.c_str();
    st["error"] = status.error.c_str();
    st["sent"] = status.stats.sent;
    st["failed"] = status.stats.failed;
    st["rate_limited"] = status.stats.rate_limited;
    st["not_ready"] = status.stats.not_ready;

    JsonObject opt = doc["options"].to<JsonObject>();
    JsonArray protocols = opt["protocols"].to<JsonArray>();
    protocols.add(syslogProtocolName(SyslogProtocol::RFC3164));
    protocols.add(syslogProtocolName(SyslogProtocol::RFC5424));
    JsonArray facilities = opt["facilities"].to<JsonArray>();
    for (uint8_t i = 0; i < SYSLOG_FACILITY_COUNT; i++) facilities.add(SYSLOG_FACILITIES[i].name);
    JsonArray levels = opt["levels"].to<JsonArray>();
    for (uint8_t i = 0; i < LOG_LEVEL_COUNT; i++) levels.add(logLevelName((LogLevel)i));
    // By value: ArduinoJson takes a reference, and the native build has no C++17 inline
    // static members
    opt["rate_min"] = (int)LogOutputFilter::MIN_RATE;
    opt["rate_max"] = (int)LogOutputFilter::MAX_RATE;

    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

int WebApiJson::checkRemoteLogBody(JsonVariantConst body, const SyslogSettings& current, SyslogSettings& next,
                                   String& response) {
    next = current;
    if (!body.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");
    JsonVariantConst sl = body["syslog"];
    if (!sl.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");
    JsonObjectConst obj = sl.as<JsonObjectConst>();

    JsonVariantConst v = obj["enabled"];
    if (!v.isUnbound()) {
        if (!v.is<bool>()) return sendError(response, 400, "Invalid request");
        next.enabled = v.as<bool>();
    }
    v = obj["host"];
    if (!v.isUnbound()) {
        if (!v.is<const char*>()) return sendError(response, 400, "Invalid server");
        String host = v.as<const char*>();
        host.trim();
        if (host.length() > 0 && !validSyslogHost(host)) return sendError(response, 400, "Invalid server");
        next.host = host;
    }
    v = obj["port"];
    if (!v.isUnbound()) {
        // An integer only: true, 514.5 or "514" are rejected
        if (!v.is<int>() || v.as<int>() < 1 || v.as<int>() > 65535) return sendError(response, 400, "Invalid port");
        next.port = (uint16_t)v.as<int>();
    }
    v = obj["protocol"];
    if (!v.isUnbound()) {
        SyslogProtocol p;
        if (!v.is<const char*>() || !parseSyslogProtocol(v.as<const char*>(), p)) {
            return sendError(response, 400, "Invalid protocol");
        }
        next.protocol = p;
    }
    v = obj["facility"];
    if (!v.isUnbound()) {
        uint8_t code;
        if (!v.is<const char*>() || !parseSyslogFacility(v.as<const char*>(), code)) {
            return sendError(response, 400, "Invalid facility");
        }
        next.facility = code;
    }
    v = obj["min_level"];
    if (!v.isUnbound()) {
        LogLevel l;
        if (!v.is<const char*>() || !parseLogLevel(v.as<const char*>(), l)) {
            return sendError(response, 400, "Invalid level");
        }
        next.min_level = l;
    }
    v = obj["rate"];
    if (!v.isUnbound()) {
        if (!v.is<int>() || v.as<int>() < LogOutputFilter::MIN_RATE || v.as<int>() > LogOutputFilter::MAX_RATE) {
            return sendError(response, 400, "Invalid rate");
        }
        next.rate = (uint16_t)v.as<int>();
    }
    if (next.enabled && next.host.length() == 0) return sendError(response, 400, "Server required to enable syslog");
    return 0;
}

// --- Network time (US-117) ---

String WebApiJson::timeJson(const TimeSettings& time, const TimeStatusView& status) {
    JsonDocument doc;
    JsonObject t = doc["time"].to<JsonObject>();
    t["ntp"] = time.ntp;
    t["server"] = time.server.c_str();
    t["zone"] = time.zone.c_str();

    JsonObject st = doc["status"].to<JsonObject>();
    st["state"] = timeStateName(status.state);
    if (status.has_time) {
        st["local"] = status.local.c_str();
        st["abbr"] = status.abbr.c_str();
        st["utc"] = status.utc.c_str();
        st["since_sync_s"] = status.since_sync_s;
    }

    JsonArray zones = doc["options"]["zones"].to<JsonArray>();
    for (size_t i = 0; i < timeZoneCount(); i++) zones.add(timeZoneAt(i)->name);

    std::string out;
    serializeJson(doc, out);
    return String(out.c_str());
}

int WebApiJson::checkTimeBody(JsonVariantConst body, const TimeSettings& current, TimeSettings& next,
                              String& response) {
    next = current;
    if (!body.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");
    JsonVariantConst tv = body["time"];
    if (!tv.is<JsonObjectConst>()) return sendError(response, 400, "Invalid request");
    JsonObjectConst obj = tv.as<JsonObjectConst>();

    JsonVariantConst v = obj["ntp"];
    if (!v.isUnbound()) {
        if (!v.is<bool>()) return sendError(response, 400, "Invalid request");
        next.ntp = v.as<bool>();
    }
    v = obj["server"];
    if (!v.isUnbound()) {
        if (!v.is<const char*>()) return sendError(response, 400, "Invalid server");
        String server = v.as<const char*>();
        server.trim();
        if (server.length() > 0 && !validNtpServer(server)) return sendError(response, 400, "Invalid server");
        next.server = server;
    }
    v = obj["zone"];
    if (!v.isUnbound()) {
        if (!v.is<const char*>() || !findTimeZone(v.as<const char*>())) {
            return sendError(response, 400, "Invalid time zone");
        }
        next.zone = v.as<const char*>();
    }
    if (next.ntp && next.server.length() == 0) return sendError(response, 400, "Server required to enable NTP");
    return 0;
}
