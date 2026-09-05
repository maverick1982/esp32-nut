#include "NUTServer.h"

// Shared by LIST UPS and GET UPSDESC so the two cannot drift.
static const char* NUT_UPS_DESCRIPTION = "ESP32-S3 UPS Bridge";

// Injected at build time from the release tag; "dev" for local builds, matching
// src/network/web_config_server.cpp.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Protocol level the implemented command subset targets, as upsd reports it.
static const char* NUT_PROTOCOL_VERSION = "1.3";

NUTServer::NUTServer() : 
    _usb_ups(nullptr), 
    _port(NUT_DEFAULT_PORT), 
    _initialized(false)
#ifndef PIO_UNIT_TESTING
    , _server(NUT_DEFAULT_PORT)
#endif
{
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        _clientActive[i] = false;
        _clientAuthenticated[i] = false;
        _clientLastActivity[i] = 0;
        _clientBuffer[i] = "";
        _clientUsername[i] = "";
    }
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

bool NUTServer::begin(const NUTServerConfig& config, IUSBHostUPS* usb_ups, uint16_t port) {
    _config = config;
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

void NUTServer::closeSession(int slot) {
    if (slot >= 0 && slot < NUT_MAX_CLIENTS) {
#ifndef PIO_UNIT_TESTING
        if (_clients[slot]) {
            _clients[slot].stop();
        }
#endif
        _clientActive[slot] = false;
        _clientAuthenticated[slot] = false;
        _clientLastActivity[slot] = 0;
        _clientBuffer[slot] = "";
        _clientUsername[slot] = "";
        Serial.printf("[NUTServer] Session for slot %d closed.\n", slot);
    }
}

std::vector<String> NUTServer::splitTokens(const String& input) {
    std::vector<String> tokens;
    String currentToken = "";
    bool inQuotes = false;
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (currentToken.length() > 0) {
                tokens.push_back(currentToken);
                currentToken = "";
            }
        } else {
            currentToken += c;
        }
    }
    if (currentToken.length() > 0) {
        tokens.push_back(currentToken);
    }
    return tokens;
}

void NUTServer::loop() {
    if (!_initialized) {
        return;
    }

#ifndef PIO_UNIT_TESTING
    // 1. Accetta nuove connessioni client
    if (_server.hasClient()) {
        WiFiClient newClient = _server.accept();
        if (newClient) {
            int slot = -1;
            for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
                if (!_clientActive[i] || !_clients[i].connected()) {
                    slot = i;
                    break;
                }
            }

            if (slot != -1) {
                if (_clients[slot]) {
                    _clients[slot].stop();
                }
                _clients[slot] = newClient;
                
                _clientActive[slot] = true;
                _clientAuthenticated[slot] = false;
                _clientLastActivity[slot] = millis();
                _clientBuffer[slot] = "";
                _clientBuffer[slot].reserve(256);
                _clientUsername[slot] = "";
                Serial.printf("[NUTServer] Client connected to slot %d from %s:%d\n", 
                              slot, newClient.remoteIP().toString().c_str(), newClient.remotePort());
            } else {
                Serial.println("[NUTServer] Connection rejected: max clients reached");
                newClient.print("ERR FAILED - Max clients reached\n");
                newClient.stop();
            }
        }
    }

    // 2. Gestisci i client attivi
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (_clientActive[i]) {
            if (!_clients[i].connected() && !_clients[i].available()) {
                closeSession(i);
                continue;
            }

            // Verifica timeout di inattività (60 secondi)
            if (millis() - _clientLastActivity[i] > NUT_TIMEOUT_MS) {
                Serial.printf("[NUTServer] Inactivity timeout for slot %d. Disconnecting.\n", i);
                _clients[i].print("ERR ACCESS-DENIED\n");
                closeSession(i);
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
        client.print("OK\n");
        return;
    }

    if (cmd == "PASSWORD") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String password = tokens[1];
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
        closeSession(slot);
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
            String upsName;
            if (tokens.size() < 3) {
                upsName = _config.ups_name;
            } else {
                upsName = tokens[2];
            }
            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }

            client.printf("BEGIN LIST VAR %s\n", upsName.c_str());
            if (_usb_ups) {
                auto data = _usb_ups->getUPSData();
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), _usb_ups->getUPSStatusString().c_str());
                for (const auto& param : data->getAll()) {
                    if (param.key.startsWith("ups.status.") && param.key != "ups.status") continue;
                    client.printf("VAR %s %s \"%s\"\n", upsName.c_str(), param.key.c_str(), param.value.c_str());
                }
            }
            client.printf("END LIST VAR %s\n", upsName.c_str());
            return;
        }
        else if (subcmd == "CMD" || subcmd == "RW" || subcmd == "CLIENT") {
            String upsName;
            if (tokens.size() < 3) {
                upsName = _config.ups_name;
            } else {
                upsName = tokens[2];
            }
            client.printf("BEGIN LIST %s %s\n", subcmd.c_str(), upsName.c_str());
            if (subcmd == "CMD" && _usb_ups) {
                if (_usb_ups->getUPSData()->hasKey("ups.beeper.status")) {
                    client.printf("CMD %s beeper.enable\n", upsName.c_str());
                    client.printf("CMD %s beeper.disable\n", upsName.c_str());
                    client.printf("CMD %s beeper.toggle\n", upsName.c_str());
                }
            }
            client.printf("END LIST %s %s\n", subcmd.c_str(), upsName.c_str());
            return;
        }
        else if (subcmd == "ENUM" || subcmd == "RANGE") {
            String upsName;
            String varName;
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            } else if (tokens.size() == 3) {
                upsName = _config.ups_name;
                varName = tokens[2];
            } else {
                upsName = tokens[2];
                varName = tokens[3];
            }
            client.printf("BEGIN LIST %s %s %s\n", subcmd.c_str(), upsName.c_str(), varName.c_str());
            client.printf("END LIST %s %s %s\n", subcmd.c_str(), upsName.c_str(), varName.c_str());
            return;
        }
        else {
            client.print("ERR UNKNOWN-COMMAND\n");
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

        if (cmdName == "beeper.enable") {
            if (!_usb_ups || !_usb_ups->getUPSData()->hasKey("ups.beeper.status")) {
                client.print("ERR CMD-NOT-SUPPORTED\n");
            } else {
                _usb_ups->setBeeper(true);
                client.print("OK\n");
            }
            return;
        } else if (cmdName == "beeper.disable") {
            if (!_usb_ups || !_usb_ups->getUPSData()->hasKey("ups.beeper.status")) {
                client.print("ERR CMD-NOT-SUPPORTED\n");
            } else {
                _usb_ups->setBeeper(false);
                client.print("OK\n");
            }
            return;
        } else if (cmdName == "beeper.toggle") {
            if (!_usb_ups || !_usb_ups->getUPSData()->hasKey("ups.beeper.status")) {
                client.print("ERR CMD-NOT-SUPPORTED\n");
            } else {
                _usb_ups->setBeeper(!_usb_ups->getUPSData()->getBool("ups.beeper.status"));
                client.print("OK\n");
            }
            return;
        }
        client.print("ERR CMD-NOT-SUPPORTED\n");
        return;
    }

    if (cmd == "GET") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String subcmd = tokens[1];
        subcmd.toUpperCase();

        if (subcmd == "VAR") {
            String upsName;
            String varName;
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            } else if (tokens.size() == 3) {
                upsName = _config.ups_name;
                varName = tokens[2];
            } else {
                upsName = tokens[2];
                varName = tokens[3];
            }

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

            auto data = _usb_ups->getUPSData();
            String varNameLower = varName;
            varNameLower.toLowerCase();

            if (varNameLower == "ups.status") {
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), _usb_ups->getUPSStatusString().c_str());
            } else if (data->hasKey(varNameLower)) {
                client.printf("VAR %s %s \"%s\"\n", upsName.c_str(), varNameLower.c_str(), data->get(varNameLower).c_str());
            } else {
                client.print("ERR VAR-NOT-SUPPORTED\n");
            }
            return;
        }
        else if (subcmd == "DESC" || subcmd == "CMDDESC" || subcmd == "TYPE") {
            String upsName;
            String varName;
            if (tokens.size() < 3) {
                client.print("ERR INVALID-ARGUMENT\n");
                return;
            } else if (tokens.size() == 3) {
                upsName = _config.ups_name;
                varName = tokens[2];
            } else {
                upsName = tokens[2];
                varName = tokens[3];
            }

            String upsNameLower = upsName;
            upsNameLower.toLowerCase();
            String configUpsNameLower = _config.ups_name;
            configUpsNameLower.toLowerCase();
            if (upsNameLower != configUpsNameLower) {
                client.print("ERR UNKNOWN-UPS\n");
                return;
            }

            if (subcmd == "TYPE") {
                // Every variable the bridge exposes is read-only (LIST RW is
                // empty) and goes out as a quoted string. Never answer a bare
                // "RW": clients read the token after it as the type.
                client.printf("TYPE %s %s STRING:64\n", upsName.c_str(), varName.c_str());
            } else {
                // DESC and CMDDESC share a shape. upsd answers "Unavailable"
                // when no description database is installed; this bridge
                // carries none for either variables or commands.
                client.printf("%s %s %s \"Unavailable\"\n", subcmd.c_str(),
                              upsName.c_str(), varName.c_str());
            }
            return;
        }
        else if (subcmd == "UPSDESC" || subcmd == "NUMLOGINS") {
            String upsName;
            if (tokens.size() < 3) {
                upsName = _config.ups_name;
            } else {
                upsName = tokens[2];
            }

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



