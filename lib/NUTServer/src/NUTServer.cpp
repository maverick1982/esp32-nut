#include "NUTServer.h"

NUTServer::NUTServer() : 
    _usb_ups(nullptr), 
    _port(NUT_DEFAULT_PORT), 
    _initialized(false),
    _server(NUT_DEFAULT_PORT) {
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        _clientActive[i] = false;
        _clientAuthenticated[i] = false;
        _clientLastActivity[i] = 0;
        _clientBuffer[i] = "";
        _clientUsername[i] = "";
    }
}

NUTServer::~NUTServer() {
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (_clientActive[i]) {
            _clients[i].stop();
        }
    }
}

bool NUTServer::begin(const NUTServerConfig& config, USBHostUPS* usb_ups, uint16_t port) {
    _config = config;
    _usb_ups = usb_ups;
    _port = port;

    _server = WiFiServer(_port);
    _server.begin();
    
    _initialized = true;
    Serial.printf("[NUTServer] Server in ascolto sulla porta %d\n", _port);
    return true;
}

void NUTServer::closeSession(int slot) {
    if (slot >= 0 && slot < NUT_MAX_CLIENTS) {
        if (_clients[slot]) {
            _clients[slot].stop();
        }
        _clientActive[slot] = false;
        _clientAuthenticated[slot] = false;
        _clientLastActivity[slot] = 0;
        _clientBuffer[slot] = "";
        _clientUsername[slot] = "";
        Serial.printf("[NUTServer] Sessione dello slot %d chiusa.\n", slot);
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
                
                // Ottimizzazione stabilità: timeout ridotto a 200ms per evitare blocchi
                _clients[slot].setTimeout(200);
                
                _clientActive[slot] = true;
                _clientAuthenticated[slot] = false;
                _clientLastActivity[slot] = millis();
                _clientBuffer[slot] = "";
                _clientUsername[slot] = "";
                Serial.printf("[NUTServer] Client connesso allo slot %d da %s:%d\n", 
                              slot, newClient.remoteIP().toString().c_str(), newClient.remotePort());
            } else {
                Serial.println("[NUTServer] Rifiutata connessione: raggiunto limite massimo di client");
                newClient.print("ERR FAILED - Max clients reached\n");
                newClient.stop();
            }
        }
    }

    // 2. Gestisci i client attivi
    for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
        if (_clientActive[i]) {
            if (!_clients[i].connected()) {
                closeSession(i);
                continue;
            }

            // Verifica timeout di inattività (60 secondi)
            if (millis() - _clientLastActivity[i] > NUT_TIMEOUT_MS) {
                Serial.printf("[NUTServer] Timeout inattività per lo slot %d. Disconnessione in corso.\n", i);
                _clients[i].print("ERR ACCESS-DENIED\n");
                closeSession(i);
                continue;
            }

            // Leggi dati disponibili dal buffer del client
            while (_clients[i].available()) {
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
}

void NUTServer::handleCommand(int slot, const String& cmdLine) {
    std::vector<String> tokens = splitTokens(cmdLine);
    if (tokens.empty()) {
        return;
    }

    String cmd = tokens[0];
    cmd.toUpperCase();

    WiFiClient& client = _clients[slot];

    bool authRequired = (_config.username.length() > 0 && _config.password.length() > 0);

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

    if (authRequired && !_clientAuthenticated[slot]) {
        client.print("ERR ACCESS-DENIED\n");
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
            client.printf("UPS %s \"ESP32-S3 UPS Bridge\"\n", _config.ups_name.c_str());
            client.print("END LIST UPS\n");
            return;
        } 
        else if (subcmd == "VAR") {
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

            uint8_t charge = 0;
            String status = "Unknown";
            float voltage = 0.0f;
            if (_usb_ups) {
                charge = _usb_ups->getBatteryCharge();
                status = _usb_ups->getUPSStatus();
                voltage = _usb_ups->getInputVoltage();
            }

            client.printf("BEGIN LIST VAR %s\n", upsName.c_str());
            client.printf("VAR %s battery.charge \"%d\"\n", upsName.c_str(), charge);
            client.printf("VAR %s input.voltage \"%.1f\"\n", upsName.c_str(), voltage);
            client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), status.c_str());
            client.printf("END LIST VAR %s\n", upsName.c_str());
            return;
        }
        else {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
    }

    if (cmd == "GET") {
        if (tokens.size() < 2) {
            client.print("ERR INVALID-ARGUMENT\n");
            return;
        }
        String subcmd = tokens[1];
        subcmd.toUpperCase();

        if (subcmd == "VAR") {
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

            uint8_t charge = 0;
            String status = "Unknown";
            float voltage = 0.0f;
            if (_usb_ups) {
                charge = _usb_ups->getBatteryCharge();
                status = _usb_ups->getUPSStatus();
                voltage = _usb_ups->getInputVoltage();
            }

            String varNameLower = varName;
            varNameLower.toLowerCase();

            if (varNameLower == "battery.charge") {
                client.printf("VAR %s battery.charge \"%d\"\n", upsName.c_str(), charge);
            } else if (varNameLower == "input.voltage") {
                client.printf("VAR %s input.voltage \"%.1f\"\n", upsName.c_str(), voltage);
            } else if (varNameLower == "ups.status") {
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), status.c_str());
            } else {
                client.print("ERR VAR-NOT-SUPPORTED\n");
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
