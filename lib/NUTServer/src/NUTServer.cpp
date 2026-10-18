#include "NUTServer.h"
#include <stdarg.h>
#include <stdlib.h>
#ifndef PIO_UNIT_TESTING
#include <esp_random.h>
#include <lwip/sockets.h>
#endif

// Default clock and random source of TRACKING (US-067); tests inject their own
static uint32_t defaultClock() {
    return (uint32_t)millis();
}

static uint32_t defaultRandom() {
#ifdef PIO_UNIT_TESTING
    // rand() gives at least 15 bits per call
    return ((uint32_t)rand() << 30) ^ ((uint32_t)rand() << 15) ^ (uint32_t)rand();
#else
    return esp_random();
#endif
}

#ifndef PIO_UNIT_TESTING
// Keepalive TCP (US-084): senza, un client sparito senza FIN resta "connected()" e tiene
// lo slot fino al timeout di inattività. Un errore va solo sulla seriale
static void enableKeepalive(WiFiClient& client) {
    const int on = 1;
    const int idle = NUT_KEEPALIVE_IDLE_S;
    const int intvl = NUT_KEEPALIVE_INTVL_S;
    const int count = NUT_KEEPALIVE_COUNT;
    if (client.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) < 0 ||
        client.setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) < 0 ||
        client.setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl)) < 0 ||
        client.setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) < 0) {
        Serial.println("[NUTServer] TCP keepalive setup failed");
    }
}
#endif

// Shared by LIST UPS and GET UPSDESC so the two cannot drift.
static const char* NUT_UPS_DESCRIPTION = "ESP32-S3 UPS Bridge";

// Injected at build time from the release tag; "dev" for local builds, matching
// src/network/web_config_server.cpp.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Protocol level the implemented command subset targets, as upsd reports it.
static const char* NUT_PROTOCOL_VERSION = "1.3";

// main.c of the NUT drivers copies ups.mfr/model/serial into device.*, and sets
// device.type "ups": NAS plugins and apps read device.model (issue #60)
static const char* const DEVICE_ALIASES[][2] = {
    { "device.mfr", "ups.mfr" },
    { "device.model", "ups.model" },
    { "device.serial", "ups.serial" },
};

static const char* deviceAliasSource(const String& var) {
    for (const auto& a : DEVICE_ALIASES) {
        if (var == a[0]) return a[1];
    }
    return nullptr;
}

// upsd looks variables up with strcasecmp (common/state.c)
static const NutTable::RWVariable* findWritable(const std::vector<NutTable::RWVariable>& rw, const String& var) {
    for (const auto& v : rw) {
        if (NutTable::equalsIgnoreCase(v.name, var.c_str())) return &v;
    }
    return nullptr;
}

// Outcome of an INSTCMD sent with TRACKING ON, as the STAT_INSTCMD_* of usbhid-ups read by
// tracking_get(): HANDLED, FAILED (HIDSetDataValue failed), CONVERSION_FAILED (bad parameter).
// false for NOT_SUPPORTED and NOT_CONNECTED: immediate errors, they never reach the table.
// No default, so a new CommandResult is flagged by -Wswitch.
static bool instcmdTrackingStatus(CommandResult r, NutTable::SetStatus& out) {
    switch (r) {
    case CommandResult::OK:               out = NutTable::SetStatus::OK; return true;
    case CommandResult::FAILED:           out = NutTable::SetStatus::FAILED; return true;
    case CommandResult::INVALID_ARGUMENT: out = NutTable::SetStatus::CONVERSION_FAILED; return true;
    case CommandResult::NOT_SUPPORTED:    return false;
    case CommandResult::NOT_CONNECTED:    return false;
    }
    return false;
}

// Fallback text of upsd when cmdvartab has no entry (netget.c get_desc / get_cmddesc)
static const char* NUT_DESC_UNAVAILABLE = "Description unavailable";

NUTServer::NUTServer() : 
    _usb_ups(nullptr), 
    _port(NUT_DEFAULT_PORT), 
    _initialized(false)
#ifndef PIO_UNIT_TESTING
    , _server(NUT_DEFAULT_PORT)
#endif
    , _lastSummaryMs(0)
    , _rejectLogged(false)
    , _lastRejectLogMs(0)
    , _rejectsSinceLog(0)
    , _tracking(defaultRandom)
    , _now(defaultClock)
    , _log_cb(nullptr)
{
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        _clientActive[i] = false;
        _clientAuthenticated[i] = false;
        _clientLastActivity[i] = 0;
        _clientBuffer[i] = "";
        _clientUsername[i] = "";
        _clientUsernameSet[i] = false;
        _clientPasswordSet[i] = false;
        _clientTracking[i] = false;
    }
}

void NUTServer::setClock(ClockFn now) {
    _now = now ? now : defaultClock;
}

void NUTServer::setRandom(NutTracking::RandomFn rnd) {
    _tracking.setRandom(rnd ? rnd : defaultRandom);
}

void NUTServer::log(const char* level, const char* fmt, ...) const {
    if (!_log_cb) return;
    char buf[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log_cb(level, buf);
}

// Variables GET VAR answers (sstate_getinfo() of upsd): the status, device.type, the
// device.* aliases and the keys of UPSData. Shared by GET VAR, LIST ENUM/RANGE and SET VAR.
bool NUTServer::isVarPresent(const UPSData& data, const String& varNameLower) {
    if (varNameLower == "ups.status" || varNameLower == "device.type") return true;
    const char* alias = deviceAliasSource(varNameLower);
    return (alias && data.hasKey(alias)) || data.hasKey(varNameLower);
}

NUTServer::~NUTServer() {
#ifndef PIO_UNIT_TESTING
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (_clientActive[i]) {
            _clients[i].stop();
        }
    }
#endif
}

String NUTServer::defaultUpsName() {
    return String("ups");
}

String NUTServer::normalizeUpsName(const String& name) {
    return name.length() == 0 ? defaultUpsName() : name;
}

// Printable ASCII without spaces: the NUT line reader keeps only 0x20-0x7E and a space
// splits the words. Also without " \ #, which pconf reads as quote, escape and comment
// (an ups.conf section cannot hold them), and without @, where upscli_splitname() splits
// "ups@host"
bool NUTServer::isValidUpsName(const String& name) {
    if (name.length() == 0) return false;
    for (size_t i = 0; i < name.length(); i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x21 || c > 0x7E) return false;
        if (c == '"' || c == '\\' || c == '#' || c == '@') return false;
    }
    return true;
}

bool NUTServer::begin(const NUTServerConfig& config, IUSBHostUPS* usb_ups, uint16_t port) {
    _config = config;
    _config.ups_name = normalizeUpsName(config.ups_name);
    _usb_ups = usb_ups;
    _port = port;

#ifndef PIO_UNIT_TESTING
    _server = WiFiServer(_port);
    _server.begin();
#endif
    
    _initialized = true;
    Serial.printf("[NUTServer] Server listening on port %d\n", _port);
    return true;
}

int NUTServer::openSession(const String& peerIp, uint16_t peerPort) {
    int slot = -1;
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
#ifndef PIO_UNIT_TESTING
        bool free_slot = !_clientActive[i] || !_clients[i].connected();
#else
        bool free_slot = !_clientActive[i];
#endif
        if (free_slot) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        Serial.printf("[NUTServer] Connection from %s:%u rejected: max clients reached\n",
                      peerIp.c_str(), (unsigned)peerPort);
        // Un client che riprova di continuo non deve riempire il System Log
        uint32_t now = _now();
        if (!_rejectLogged || now - _lastRejectLogMs >= NUT_LOG_INTERVAL_MS) {
            String others;
            if (_rejectsSinceLog > 0) {
                others = String(" (") + String(_rejectsSinceLog) + " more rejected since the last report)";
            }
            log("WARN", "[NUT] Connection from %s rejected: all %d slots in use (%s)%s", peerIp.c_str(),
                NUT_MAX_CLIENTS, slotSummary().c_str(), others.c_str());
            _rejectLogged = true;
            _lastRejectLogMs = now;
            _rejectsSinceLog = 0;
        } else {
            _rejectsSinceLog++;
        }
        return -1;
    }

    _clientActive[slot] = true;
    _clientAuthenticated[slot] = false;
    _clientLastActivity[slot] = millis();
    _clientBuffer[slot] = "";
    _clientBuffer[slot].reserve(256);
    _clientUsername[slot] = "";
    _clientUsernameSet[slot] = false;
    _clientPasswordSet[slot] = false;
    _clientTracking[slot] = false;
    _clientPeer[slot] = peerIp;
    Serial.printf("[NUTServer] Client connected to slot %d from %s:%u\n", slot, peerIp.c_str(),
                  (unsigned)peerPort);
    return slot;
}

String NUTServer::slotSummary() const {
    String out;
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (!_clientActive[i]) continue;
        if (out.length() > 0) out += ", ";
        out += _clientPeer[i];
    }
    return out;
}

void NUTServer::logClientsSummary(uint32_t now) {
    if (now - _lastSummaryMs < NUT_LOG_INTERVAL_MS) return;
    _lastSummaryMs = now;
    String summary = slotSummary();
    if (summary == _lastSummary) return;
    _lastSummary = summary;
    int in_use = 0;
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (_clientActive[i]) in_use++;
    }
    log("INFO", "[NUT] Clients: %d/%d in use%s%s", in_use, NUT_MAX_CLIENTS, in_use ? " - " : "",
        summary.c_str());
}

void NUTServer::closeSession(int slot, CloseReason reason) {
    if (slot >= 0 && slot < NUT_MAX_CLIENTS) {
#ifndef PIO_UNIT_TESTING
        if (_clients[slot]) {
            _clients[slot].stop();
        }
#endif
        if (reason == CloseReason::Inactivity) {
            log("INFO", "[NUT] Client %s on slot %d disconnected after %u s of inactivity",
                _clientPeer[slot].c_str(), slot, (unsigned)(NUT_TIMEOUT_MS / 1000));
        }
        _clientActive[slot] = false;
        _clientAuthenticated[slot] = false;
        _clientLastActivity[slot] = 0;
        _clientBuffer[slot] = "";
        _clientUsername[slot] = "";
        _clientUsernameSet[slot] = false;
        _clientPasswordSet[slot] = false;
        _clientTracking[slot] = false;
        _clientPeer[slot] = "";
        Serial.printf("[NUTServer] Session for slot %d closed.\n", slot);
    }
}

// Value as upsd sends it in VAR, RW and ENUM replies: common/state.c val_escape() keeps
// pconf_encode(raw), a backslash before each character of PCONF_ESCAPE ("#\\\""). The
// raw value stays in UPSData, NVS and the JSON APIs. pconf_encode() also truncates the
// result to ST_MAX_VALUE_LEN - 1: not reproduced, since it is unreachable here (HID
// strings are at most 64 characters, driver-side values aux 20, a request line 256).
String NUTServer::encodeValue(const char* raw) {
    String out;
    if (!raw) return out;
    for (const char* p = raw; *p; p++) {
        if (*p == '#' || *p == '\\' || *p == '"') out += '\\';
        out += *p;
    }
    return out;
}

// Words as pconf splits them (common/parseconf.c): a backslash takes the next character
// literally, in or out of quotes (upsrw escapes quotes, backslashes and # with
// pconf_encode()), and a pair of quotes ends a word even when it is empty.
std::vector<String> NUTServer::splitTokens(const String& input) {
    std::vector<String> tokens;
    String currentToken = "";
    bool inQuotes = false;
    bool quoted = false; // the word had a pair of quotes: kept even if empty
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '\\') {
            if (i + 1 < input.length()) {
                currentToken += input[++i];
            }
        } else if (c == '"') {
            if (inQuotes) quoted = true;
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (currentToken.length() > 0 || quoted) {
                tokens.push_back(currentToken);
                currentToken = "";
            }
            quoted = false;
        } else {
            currentToken += c;
        }
    }
    if (currentToken.length() > 0 || quoted) {
        tokens.push_back(currentToken);
    }
    return tokens;
}

void NUTServer::loop() {
    if (!_initialized) {
        return;
    }

    // tracking_cleanup() of the upsd main loop: expired entries go before millis() wraps
    _tracking.cleanup(_now());
    logClientsSummary(_now());

#ifndef PIO_UNIT_TESTING
    // 1. Accetta nuove connessioni client
    if (_server.hasClient()) {
        WiFiClient newClient = _server.accept();
        if (newClient) {
            int slot = openSession(newClient.remoteIP().toString(), newClient.remotePort());
            if (slot != -1) {
                if (_clients[slot]) {
                    _clients[slot].stop();
                }
                _clients[slot] = newClient;
                enableKeepalive(_clients[slot]);
            } else {
                newClient.print("ERR FAILED - Max clients reached\n");
                newClient.stop();
            }
        }
    }

    // 2. Gestisci i client attivi
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (_clientActive[i]) {
            if (!_clients[i].connected() && !_clients[i].available()) {
                closeSession(i, CloseReason::PeerClosed);
                continue;
            }

            // Verifica timeout di inattività (NUT_TIMEOUT_MS)
            if (millis() - _clientLastActivity[i] > NUT_TIMEOUT_MS) {
                Serial.printf("[NUTServer] Inactivity timeout for slot %d. Disconnecting.\n", i);
                _clients[i].print("ERR ACCESS-DENIED\n");
                closeSession(i, CloseReason::Inactivity);
                continue;
            }

            // Leggi dati disponibili dal buffer del client
            for (int b = 0; b < 128 && _clients[i].available(); b++) {
                char c = _clients[i].read();
                _clientLastActivity[i] = millis(); // Resetta il timer di inattività
                
                if (c == '\n' || c == '\r') {
                    if (_clientBuffer[i].length() > 0) {
                        handleCommand(i, _clientBuffer[i]);
                        _clientBuffer[i] = "";
                    }
                } else if (c >= 32 && c <= 126) {
                    if (_clientBuffer[i].length() < 256) {
                        _clientBuffer[i] += c;
                    }
                }
            }
        }
    }
#endif
}

void NUTServer::handleCommand(int slot, const String& cmdLine) {
#ifndef PIO_UNIT_TESTING
    processCommand(_clients[slot], slot, cmdLine);
#endif
}

void NUTServer::processCommand(Print& client, int slot, const String& cmdLine) {
    std::vector<String> tokens = splitTokens(cmdLine);
    if (tokens.empty()) {
        return;
    }

    String cmd = tokens[0];
    cmd.toUpperCase();

    bool authRequired = (_config.username.length() > 0 && _config.password.length() > 0);

    // upsd check_command(): FLAG_USER commands need USERNAME and PASSWORD before any
    // other check, arguments included
    if (cmd == "LOGIN" || cmd == "SET" || cmd == "INSTCMD") {
        const char* missing = missingCredentials(slot);
        if (missing) {
            String subcmd = tokens.size() > 1 ? tokens[1] : String("");
            subcmd.toUpperCase();
            if (cmd == "SET" && subcmd == "VAR" && tokens.size() >= 5) {
                // A refused SET VAR goes to the System Log like the others
                rejectSet(client, slot, tokens[3], tokens[4], missing);
            } else {
                client.printf("ERR %s\n", missing);
            }
            return;
        }
    }

    if (cmd == "VER") {
        client.printf("Network UPS Tools esp32-nut %s\n", FIRMWARE_VERSION);
        return;
    }

    if (cmd == "NETVER") {
        client.printf("%s\n", NUT_PROTOCOL_VERSION);
        return;
    }

    if (cmd == "USERNAME") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        _clientUsername[slot] = tokens[1];
        _clientUsernameSet[slot] = true;
        client.print("OK\n");
        return;
    }

    if (cmd == "PASSWORD") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String password = tokens[1];
        // Sent, right or wrong: LOGIN, SET and INSTCMD then answer ACCESS-DENIED, not PASSWORD-REQUIRED
        _clientPasswordSet[slot] = true;
        if (_clientUsername[slot] == _config.username && password == _config.password) {
            _clientAuthenticated[slot] = true;
            client.print("OK\n");
        } else {
            client.print("ERR ACCESS-DENIED\n");
        }
        return;
    }

    if (cmd == "LOGIN") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String upsName = tokens[1];
        String upsNameLower = upsName;
        upsNameLower.toLowerCase();
        String configUpsNameLower = _config.ups_name;
        configUpsNameLower.toLowerCase();

        if (upsNameLower != configUpsNameLower) {
            client.print("ERR UNKNOWN-UPS\n");
            return;
        }

        if (authRequired && !_clientAuthenticated[slot]) {
            client.print("ERR ACCESS-DENIED\n");
            return;
        }

        client.print("OK\n");
        return;
    }

    if (cmd == "LOGOUT") {
        client.print("OK Goodbye\n");
        closeSession(slot, CloseReason::Logout);
        return;
    }

    if (cmd == "LIST") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String subcmd = tokens[1];
        subcmd.toUpperCase();

        if (subcmd == "UPS") {
            client.print("BEGIN LIST UPS\n");
            client.printf("UPS %s \"%s\"\n", _config.ups_name.c_str(), NUT_UPS_DESCRIPTION);
            client.print("END LIST UPS\n");
            return;
        } 
        else if (subcmd == "VAR") {
            // net_list() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];
            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }

            // Like upsd: never serve frozen values as current
            if (_usb_ups && _usb_ups->isDataStale()) {
                client.print("ERR DATA-STALE\n");
                return;
            }

            client.printf("BEGIN LIST VAR %s\n", upsName.c_str());
            if (_usb_ups) {
                auto data = _usb_ups->getUPSData();
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), encodeValue(_usb_ups->getUPSStatusString().c_str()).c_str());
                client.printf("VAR %s device.type \"ups\"\n", upsName.c_str());
                for (const auto& a : DEVICE_ALIASES) {
                    if (data->hasKey(a[1])) {
                        client.printf("VAR %s %s \"%s\"\n", upsName.c_str(), a[0], encodeValue(data->get(a[1]).c_str()).c_str());
                    }
                }
                for (const auto& param : data->getAll()) {
                    if (param.key.startsWith("ups.status.") && param.key != "ups.status") continue;
                    client.printf("VAR %s %s \"%s\"\n", upsName.c_str(), param.key.c_str(), encodeValue(param.value.c_str()).c_str());
                }
            }
            client.printf("END LIST VAR %s\n", upsName.c_str());
            return;
        }
        else if (subcmd == "RW") {
            // netlist.c list_rw(): same checks as LIST VAR, then "RW <ups> <var> <value>"
            // net_list() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];
            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }
            if (_usb_ups && _usb_ups->isDataStale()) {
                client.print("ERR DATA-STALE\n");
                return;
            }
            // Listed before the data lock is taken: getWritableVariables() takes it itself
            std::vector<NutTable::RWVariable> rw;
            if (_usb_ups) rw = _usb_ups->getWritableVariables();
            client.printf("BEGIN LIST RW %s\n", upsName.c_str());
            if (_usb_ups && !rw.empty()) {
                auto data = _usb_ups->getUPSData();
                for (const auto& v : rw) {
                    if (!data->hasKey(v.name)) continue;
                    client.printf("RW %s %s \"%s\"\n", upsName.c_str(), v.name, encodeValue(data->get(v.name).c_str()).c_str());
                }
            }
            client.printf("END LIST RW %s\n", upsName.c_str());
            return;
        }
        else if (subcmd == "CMD" || subcmd == "CLIENT") {
            // net_list() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];
            // list_cmd() / list_clients() of netlist.c: an unknown UPS is an error, and
            // list_cmd() also needs fresh data (ups_available())
            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }
            if (subcmd == "CMD" && _usb_ups && _usb_ups->isDataStale()) {
                client.print("ERR DATA-STALE\n");
                return;
            }
            client.printf("BEGIN LIST %s %s\n", subcmd.c_str(), upsName.c_str());
            if (subcmd == "CMD" && _usb_ups) {
                // Commands the device supports, from its HID descriptor (US-056)
                for (const auto* c : _usb_ups->getSupportedCommands()) {
                    client.printf("CMD %s %s\n", upsName.c_str(), c->name);
                }
            }
            client.printf("END LIST %s %s\n", subcmd.c_str(), upsName.c_str());
            return;
        }
        else if (subcmd == "ENUM" || subcmd == "RANGE") {
            // net_list() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 4) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];
            String varName = tokens[3];
            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }
            if (_usb_ups && _usb_ups->isDataStale()) {
                client.print("ERR DATA-STALE\n");
                return;
            }
            // netlist.c list_enum() / list_range(): an unknown variable is an error
            String varNameLower = varName;
            varNameLower.toLowerCase();
            bool present = false;
            if (_usb_ups) {
                auto data = _usb_ups->getUPSData();
                present = isVarPresent(data.get(), varNameLower);
            }
            if (!present) {
                client.print("ERR VAR-NOT-SUPPORTED\n");
                return;
            }
            client.printf("BEGIN LIST %s %s %s\n", subcmd.c_str(), upsName.c_str(), varName.c_str());
            if (subcmd == "ENUM") {
                // HU_FLAG_ENUM rows of the NUT table (US-066); usbhid-ups adds no ranges
                std::vector<NutTable::RWVariable> rw = _usb_ups->getWritableVariables();
                const NutTable::RWVariable* v = findWritable(rw, varName);
                if (v) {
                    for (const char* e : v->enums) {
                        client.printf("ENUM %s %s \"%s\"\n", upsName.c_str(), varName.c_str(), encodeValue(e).c_str());
                    }
                }
            }
            client.printf("END LIST %s %s %s\n", subcmd.c_str(), upsName.c_str(), varName.c_str());
            return;
        }
        else {
            // Unknown LIST subcommand: INVALID-ARGUMENT, as at the end of net_list()
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
    }

    if (cmd == "INSTCMD") {
        if (authRequired && !_clientAuthenticated[slot]) {
            client.print("ERR ACCESS-DENIED\n");
            return;
        }
        if (tokens.size() < 3) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String upsName = tokens[1];
        String cmdName = tokens[2];
        
        String upsNameLower = upsName;
        upsNameLower.toLowerCase();
        String configUpsNameLower = _config.ups_name;
        configUpsNameLower.toLowerCase();
        if (upsNameLower != configUpsNameLower) {
            client.print("ERR UNKNOWN-UPS\n");
            return;
        }

        if (!_usb_ups) {
            client.print("ERR DRIVER-NOT-CONNECTED\n");
            return;
        }
        // INSTCMD <ups> <cmd> [value]: the value is used by the delay commands only
        const char* param = tokens.size() > 3 ? tokens[3].c_str() : nullptr;
        CommandResult r = _usb_ups->executeCommand(cmdName.c_str(), param);
        // send_instcmd() with a tracking id. Driver and server are one process, so the outcome
        // is known before the reply and PENDING never shows up, as in setVar()
        NutTable::SetStatus st;
        if (_clientTracking[slot] && instcmdTrackingStatus(r, st)) {
            replyTracking(client, st);
            return;
        }
        switch (r) {
        case CommandResult::OK:            client.print("OK\n"); break;
        case CommandResult::NOT_SUPPORTED: client.print("ERR CMD-NOT-SUPPORTED\n"); break;
        case CommandResult::NOT_CONNECTED: client.print("ERR DRIVER-NOT-CONNECTED\n"); break;
        case CommandResult::FAILED:        client.print("ERR INSTCMD-FAILED\n"); break;
        case CommandResult::INVALID_ARGUMENT: client.print("ERR INVALID-ARGUMENT\n"); break;
        }
        return;
    }

    if (cmd == "SET") {
        // netset.c net_set(): every malformed SET is ERR INVALID-ARGUMENT
        if (tokens.size() < 3) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String subcmd = tokens[1];
        subcmd.toUpperCase();

        if (subcmd == "VAR") {
            // SET VAR <ups> <var> <value>
            if (tokens.size() < 5) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            setVar(client, slot, tokens[2], tokens[3], tokens[4]);
            return;
        }
        if (subcmd == "TRACKING") {
            // Per session; USERNAME and PASSWORD are required (FLAG_USER) but not checked,
            // as net_set() does not call user_checkaction()
            String mode = tokens[2];
            mode.toUpperCase();
            if (mode == "ON") {
                _clientTracking[slot] = true;
            } else if (mode == "OFF") {
                _clientTracking[slot] = false;
            } else {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            client.print("OK\n");
            return;
        }
        client.print("ERR INVALID-ARGUMENT\n");
        return;
    }

    if (cmd == "GET") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String subcmd = tokens[1];
        subcmd.toUpperCase();

        // netget.c net_get(): GET TRACKING [<id>], before the subcommands that need a UPS
        if (subcmd == "TRACKING") {
            if (tokens.size() == 2) {
                client.print(_clientTracking[slot] ? "ON\n" : "OFF\n");
            } else if (_clientTracking[slot]) {
                client.printf("%s\n", _tracking.get(tokens[2].c_str(), _now()));
            } else {
                client.print("ERR FEATURE-NOT-CONFIGURED\n");
            }
            return;
        }

        if (subcmd == "VAR") {
            // net_get() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 4) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];
            String varName = tokens[3];

            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }

            if (!_usb_ups) {
                client.print("ERR VAR-NOT-SUPPORTED\n");
                return;
            }

            if (_usb_ups->isDataStale()) {
                client.print("ERR DATA-STALE\n");
                return;
            }

            auto data = _usb_ups->getUPSData();
            String varNameLower = varName;
            varNameLower.toLowerCase();

            if (!isVarPresent(data.get(), varNameLower)) {
                client.print("ERR VAR-NOT-SUPPORTED\n");
                return;
            }
            const char* alias = deviceAliasSource(varNameLower);
            if (varNameLower == "ups.status") {
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), encodeValue(_usb_ups->getUPSStatusString().c_str()).c_str());
            } else if (varNameLower == "device.type") {
                client.printf("VAR %s device.type \"ups\"\n", upsName.c_str());
            } else if (alias && data->hasKey(alias)) {
                client.printf("VAR %s %s \"%s\"\n", upsName.c_str(), varNameLower.c_str(), encodeValue(data->get(alias).c_str()).c_str());
            } else {
                client.printf("VAR %s %s \"%s\"\n", upsName.c_str(), varNameLower.c_str(), encodeValue(data->get(varNameLower).c_str()).c_str());
            }
            return;
        }
        else if (subcmd == "DESC" || subcmd == "CMDDESC" || subcmd == "TYPE") {
            // net_get() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 4) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];
            String varName = tokens[3];

            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }

            if (subcmd == "TYPE") {
                // netget.c get_type(): writable variables are "RW [ENUM] STRING:<len>"
                // (US-066). The others still go out as quoted strings (US-071 will align
                // them). Never answer a bare "RW": clients read the token after it as
                // the type.
                std::vector<NutTable::RWVariable> rw;
                if (_usb_ups) rw = _usb_ups->getWritableVariables();
                const NutTable::RWVariable* v = findWritable(rw, varName);
                if (v) {
                    client.printf("TYPE %s %s RW%s STRING:%u\n", upsName.c_str(), varName.c_str(),
                                  v->enums.empty() ? "" : " ENUM", (unsigned)v->aux);
                } else {
                    client.printf("TYPE %s %s STRING:64\n", upsName.c_str(), varName.c_str());
                }
            } else {
                // DESC and CMDDESC share a shape. The texts are the cmdvartab ones:
                // variables from the NUT table (US-066), commands from the command
                // catalog; upsd answers "Description unavailable" for the others
                // (netget.c get_desc / get_cmddesc).
                const char* desc = nullptr;
                if (subcmd == "CMDDESC") {
                    const UPSCommandInfo* info = CommandCatalog::find(varName.c_str());
                    if (info) desc = info->description;
                } else {
                    desc = NutTable::describe(varName.c_str());
                }
                client.printf("%s %s %s \"%s\"\n", subcmd.c_str(),
                              upsName.c_str(), varName.c_str(),
                              desc ? desc : NUT_DESC_UNAVAILABLE);
            }
            return;
        }
        else if (subcmd == "UPSDESC" || subcmd == "NUMLOGINS") {
            // net_get() of upsd: without the UPS name INVALID-ARGUMENT, no fallback (US-082)
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            }
            String upsName = tokens[2];

            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }

            if (subcmd == "UPSDESC") {
                client.printf("UPSDESC %s \"%s\"\n", upsName.c_str(), NUT_UPS_DESCRIPTION);
            } else {
                // This bridge exposes telemetry only; it tracks no upsmon LOGIN
                // sessions, so the count of logged-in clients is always zero.
                client.printf("NUMLOGINS %s 0\n", upsName.c_str());
            }
            return;
        }
        else {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
    }

    client.print("ERR UNKNOWN-COMMAND\n");
}

// upsd check_command(), FLAG_USER: USERNAME-REQUIRED / PASSWORD-REQUIRED when the
// client has not sent them. Whether they are right is checked by each command
// (ACCESS-DENIED). With no NUT credentials configured every client is allowed.
const char* NUTServer::missingCredentials(int slot) const {
    bool authRequired = (_config.username.length() > 0 && _config.password.length() > 0);
    if (!authRequired) return nullptr;
    if (!_clientUsernameSet[slot]) return "USERNAME-REQUIRED";
    if (!_clientPasswordSet[slot]) return "PASSWORD-REQUIRED";
    return nullptr;
}

// A SET VAR refused by the checks of upsd: the error goes to the client and to the
// System Log. Valid requests are logged by the host with their outcome.
void NUTServer::rejectSet(Print& client, int slot, const String& varName, const String& value, const char* err) {
    client.printf("ERR %s\n", err);
    const char* user = _clientUsername[slot].length() > 0 ? _clientUsername[slot].c_str() : "-";
    log("WARN", "SET %s \"%s\" from %s: ERR %s", varName.c_str(), value.c_str(), user, err);
}

// tracking_add() + "OK TRACKING <id>" of set_var() and send_instcmd(): one table for
// SET VAR and INSTCMD, with the outcome already known
void NUTServer::replyTracking(Print& client, NutTable::SetStatus st) {
    char id[NutTracking::ID_LEN];
    _tracking.newId(id);
    _tracking.add(id, st, _now());
    client.printf("OK TRACKING %s\n", id);
}

// netset.c set_var(): the checks of upsd, in its order. Nothing reaches the UPS unless
// every check passes.
void NUTServer::setVar(Print& client, int slot, const String& upsName, const String& varName, const String& value) {
    String upsNameLower = upsName;
    upsNameLower.toLowerCase();
    String configUpsNameLower = _config.ups_name;
    configUpsNameLower.toLowerCase();
    if (upsNameLower != configUpsNameLower) {
        rejectSet(client, slot, varName, value, "UNKNOWN-UPS");
        return;
    }

    // ups_available(): driver socket first, then stale data
    if (!_usb_ups || !_usb_ups->isConnected()) {
        rejectSet(client, slot, varName, value, "DRIVER-NOT-CONNECTED");
        return;
    }
    if (_usb_ups->isDataStale()) {
        rejectSet(client, slot, varName, value, "DATA-STALE");
        return;
    }

    // user_checkaction(): the credentials that allow INSTCMD allow SET. USERNAME and
    // PASSWORD were sent (missingCredentials()), here they are wrong
    bool authRequired = (_config.username.length() > 0 && _config.password.length() > 0);
    if (authRequired && !_clientAuthenticated[slot]) {
        rejectSet(client, slot, varName, value, "ACCESS-DENIED");
        return;
    }

    String varNameLower = varName;
    varNameLower.toLowerCase();
    bool present;
    {
        auto data = _usb_ups->getUPSData();
        present = isVarPresent(data.get(), varNameLower);
    }
    if (!present) {
        rejectSet(client, slot, varName, value, "VAR-NOT-SUPPORTED");
        return;
    }

    // Listed after the data lock is released: getWritableVariables() takes it itself
    std::vector<NutTable::RWVariable> rw = _usb_ups->getWritableVariables();

    // Value checks of set_var(), shared with the Web UI (NutTable::checkSetValue, US-068).
    // aux 0 is the "insanity from the driver" of upsd, e.g. input.sensitivity of cps-hid.
    const NutTable::RWVariable* v = nullptr;
    switch (NutTable::checkSetValue(rw, varName.c_str(), value.c_str(), v)) {
    case NutTable::SetCheck::OK:
        break;
    case NutTable::SetCheck::NOT_WRITABLE:
        rejectSet(client, slot, varName, value, "READONLY");
        return;
    case NutTable::SetCheck::INVALID_AUX:
        log("WARN", "auxdata for %s is invalid", v->name);
        rejectSet(client, slot, varName, value, "SET-FAILED");
        return;
    case NutTable::SetCheck::TOO_LONG:
        rejectSet(client, slot, varName, value, "TOO-LONG");
        return;
    case NutTable::SetCheck::INVALID_VALUE:
        rejectSet(client, slot, varName, value, "INVALID-VALUE");
        return;
    }

    // Driver and server are one process, so the outcome is known before the reply: OK only
    // once written, or the result kept for GET TRACKING (PENDING never shows up)
    NutTable::SetStatus st = _usb_ups->setVariable(v->name, value.c_str());
    if (_clientTracking[slot]) {
        replyTracking(client, st);
    } else if (st == NutTable::SetStatus::OK) {
        client.print("OK\n");
    } else {
        client.print("ERR SET-FAILED\n");
    }
}



