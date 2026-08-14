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
    Serial.printf("[NUTServer] Server listening on port %d\n", _port);
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
                
                // Il timeout di 200ms è rimosso per evitare troncamento dei dati
                // _clients[slot].setTimeout(200);
                
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
            client.printf("UPS %s \"ESP32-S3 UPS Bridge\"\n", _config.ups_name.c_str());
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
                const UPSData& data = _usb_ups->getUPSData();
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), _usb_ups->getUPSStatusString().c_str());
                client.printf("VAR %s ups.mfr \"%s\"\n", upsName.c_str(), (data.manufacturer.length() > 0 ? data.manufacturer.c_str() : "Unknown"));
                client.printf("VAR %s ups.model \"%s\"\n", upsName.c_str(), (data.product.length() > 0 ? data.product.c_str() : "Unknown"));
                client.printf("VAR %s ups.serial \"%s\"\n", upsName.c_str(), (data.serialNumber.length() > 0 ? data.serialNumber.c_str() : "Unknown"));
                client.printf("VAR %s battery.charge \"%d\"\n", upsName.c_str(), data.remainingCapacity);
                client.printf("VAR %s battery.charge.low \"%d\"\n", upsName.c_str(), data.remainingCapacityLimit);
                client.printf("VAR %s battery.capacity \"%d\"\n", upsName.c_str(), data.designCapacity);
                client.printf("VAR %s battery.charge.full \"%d\"\n", upsName.c_str(), data.fullChargeCapacity);
                client.printf("VAR %s battery.runtime \"%d\"\n", upsName.c_str(), data.runTimeToEmpty);
                client.printf("VAR %s output.voltage \"%d\"\n", upsName.c_str(), data.outputVoltage);
                client.printf("VAR %s input.voltage \"%d\"\n", upsName.c_str(), data.inputVoltage);
                client.printf("VAR %s battery.voltage \"%d\"\n", upsName.c_str(), data.batteryVoltage);
                client.printf("VAR %s input.transfer.high \"%d\"\n", upsName.c_str(), data.highVoltageTransfer);
                client.printf("VAR %s input.transfer.low \"%d\"\n", upsName.c_str(), data.lowVoltageTransfer);
                client.printf("VAR %s ups.power.nominal \"%d\"\n", upsName.c_str(), data.configApparentPower);
                if (data.configActivePower > 0) client.printf("VAR %s ups.realpower.nominal \"%d\"\n", upsName.c_str(), data.configActivePower);
                client.printf("VAR %s input.frequency.nominal \"%d\"\n", upsName.c_str(), data.configFrequency);
                client.printf("VAR %s input.voltage.nominal \"%d\"\n", upsName.c_str(), data.configVoltage);
                if (data.outputVoltageNominal > 0) client.printf("VAR %s output.voltage.nominal \"%d\"\n", upsName.c_str(), data.outputVoltageNominal);
                if (data.outputFrequencyNominal > 0) client.printf("VAR %s output.frequency.nominal \"%d\"\n", upsName.c_str(), data.outputFrequencyNominal);
                client.printf("VAR %s ups.load \"%d\"\n", upsName.c_str(), data.load);
                client.printf("VAR %s ups.realpower \"%d\"\n", upsName.c_str(), data.realPower);
                if (data.delayShutdown >= 0) client.printf("VAR %s ups.delay.shutdown \"%d\"\n", upsName.c_str(), data.delayShutdown);
                if (data.delayStart >= 0) client.printf("VAR %s ups.delay.start \"%d\"\n", upsName.c_str(), data.delayStart);
                if (data.timerStart >= 0) client.printf("VAR %s ups.timer.start \"%d\"\n", upsName.c_str(), data.timerStart);
                if (data.timerShutdown >= 0) client.printf("VAR %s ups.timer.shutdown \"%d\"\n", upsName.c_str(), data.timerShutdown);
                if (data.batteryType.length() > 0) client.printf("VAR %s battery.type \"%s\"\n", upsName.c_str(), data.batteryType.c_str());
                if (data.upsType.length() > 0) client.printf("VAR %s ups.type \"%s\"\n", upsName.c_str(), data.upsType.c_str());
                client.printf("VAR %s ups.beeper.status \"%s\"\n", upsName.c_str(), data.beeperEnabled ? "enabled" : "disabled");
                client.printf("VAR %s outlet.1.switch \"%d\"\n", upsName.c_str(), data.outlet1Switch ? 1 : 0);
                client.printf("VAR %s outlet.2.switch \"%d\"\n", upsName.c_str(), data.outlet2Switch ? 1 : 0);
            }
            client.printf("END LIST VAR %s\n", upsName.c_str());
            return;
        }
        else if (subcmd == "CMD" || subcmd == "RW") {
            String upsName;
            if (tokens.size() < 3) {
                upsName = _config.ups_name;
            } else {
                upsName = tokens[2];
            }
            client.printf("BEGIN LIST %s %s\n", subcmd.c_str(), upsName.c_str());
            if (subcmd == "CMD") {
                client.printf("CMD %s beeper.enable\n", upsName.c_str());
                client.printf("CMD %s beeper.disable\n", upsName.c_str());
                client.printf("CMD %s beeper.toggle\n", upsName.c_str());
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
            if (_usb_ups) _usb_ups->setBeeper(true);
            client.print("OK\n");
            return;
        } else if (cmdName == "beeper.disable") {
            if (_usb_ups) _usb_ups->setBeeper(false);
            client.print("OK\n");
            return;
        } else if (cmdName == "beeper.toggle") {
            if (_usb_ups) _usb_ups->setBeeper(!_usb_ups->getUPSData().beeperEnabled);
            client.print("OK\n");
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

            const UPSData& data = _usb_ups->getUPSData();
            String varNameLower = varName;
            varNameLower.toLowerCase();

            if (varNameLower == "ups.status") {
                client.printf("VAR %s ups.status \"%s\"\n", upsName.c_str(), _usb_ups->getUPSStatusString().c_str());
            } else if (varNameLower == "ups.mfr") {
                client.printf("VAR %s ups.mfr \"%s\"\n", upsName.c_str(), (data.manufacturer.length() > 0 ? data.manufacturer.c_str() : "Unknown"));
            } else if (varNameLower == "ups.model") {
                client.printf("VAR %s ups.model \"%s\"\n", upsName.c_str(), (data.product.length() > 0 ? data.product.c_str() : "Unknown"));
            } else if (varNameLower == "ups.serial") {
                client.printf("VAR %s ups.serial \"%s\"\n", upsName.c_str(), (data.serialNumber.length() > 0 ? data.serialNumber.c_str() : "Unknown"));
            } else if (varNameLower == "battery.charge") {
                client.printf("VAR %s battery.charge \"%d\"\n", upsName.c_str(), data.remainingCapacity);
            } else if (varNameLower == "battery.charge.low") {
                client.printf("VAR %s battery.charge.low \"%d\"\n", upsName.c_str(), data.remainingCapacityLimit);
            } else if (varNameLower == "battery.capacity") {
                client.printf("VAR %s battery.capacity \"%d\"\n", upsName.c_str(), data.designCapacity);
            } else if (varNameLower == "battery.charge.full") {
                client.printf("VAR %s battery.charge.full \"%d\"\n", upsName.c_str(), data.fullChargeCapacity);
            } else if (varNameLower == "battery.runtime") {
                client.printf("VAR %s battery.runtime \"%d\"\n", upsName.c_str(), data.runTimeToEmpty);
            } else if (varNameLower == "output.voltage") {
                client.printf("VAR %s output.voltage \"%d\"\n", upsName.c_str(), data.outputVoltage);
            } else if (varNameLower == "input.voltage") {
                client.printf("VAR %s input.voltage \"%d\"\n", upsName.c_str(), data.inputVoltage);
            } else if (varNameLower == "battery.voltage") {
                client.printf("VAR %s battery.voltage \"%d\"\n", upsName.c_str(), data.batteryVoltage);
            } else if (varNameLower == "input.transfer.high") {
                client.printf("VAR %s input.transfer.high \"%d\"\n", upsName.c_str(), data.highVoltageTransfer);
            } else if (varNameLower == "input.transfer.low") {
                client.printf("VAR %s input.transfer.low \"%d\"\n", upsName.c_str(), data.lowVoltageTransfer);
            } else if (varNameLower == "ups.power.nominal") {
                client.printf("VAR %s ups.power.nominal \"%d\"\n", upsName.c_str(), data.configApparentPower);
            } else if (varNameLower == "ups.realpower.nominal" && data.configActivePower > 0) {
                client.printf("VAR %s ups.realpower.nominal \"%d\"\n", upsName.c_str(), data.configActivePower);
            } else if (varNameLower == "input.frequency.nominal") {
                client.printf("VAR %s input.frequency.nominal \"%d\"\n", upsName.c_str(), data.configFrequency);
            } else if (varNameLower == "input.voltage.nominal") {
                client.printf("VAR %s input.voltage.nominal \"%d\"\n", upsName.c_str(), data.configVoltage);
            } else if (varNameLower == "output.voltage.nominal" && data.outputVoltageNominal > 0) {
                client.printf("VAR %s output.voltage.nominal \"%d\"\n", upsName.c_str(), data.outputVoltageNominal);
            } else if (varNameLower == "output.frequency.nominal" && data.outputFrequencyNominal > 0) {
                client.printf("VAR %s output.frequency.nominal \"%d\"\n", upsName.c_str(), data.outputFrequencyNominal);
            } else if (varNameLower == "ups.load") {
                client.printf("VAR %s ups.load \"%d\"\n", upsName.c_str(), data.load);
            } else if (varNameLower == "ups.realpower") {
                client.printf("VAR %s ups.realpower \"%d\"\n", upsName.c_str(), data.realPower);
            } else if (varNameLower == "ups.delay.shutdown" && data.delayShutdown >= 0) {
                client.printf("VAR %s ups.delay.shutdown \"%d\"\n", upsName.c_str(), data.delayShutdown);
            } else if (varNameLower == "ups.delay.start" && data.delayStart >= 0) {
                client.printf("VAR %s ups.delay.start \"%d\"\n", upsName.c_str(), data.delayStart);
            } else if (varNameLower == "ups.timer.start" && data.timerStart >= 0) {
                client.printf("VAR %s ups.timer.start \"%d\"\n", upsName.c_str(), data.timerStart);
            } else if (varNameLower == "ups.timer.shutdown" && data.timerShutdown >= 0) {
                client.printf("VAR %s ups.timer.shutdown \"%d\"\n", upsName.c_str(), data.timerShutdown);
            } else if (varNameLower == "battery.type" && data.batteryType.length() > 0) {
                client.printf("VAR %s battery.type \"%s\"\n", upsName.c_str(), data.batteryType.c_str());
            } else if (varNameLower == "ups.type" && data.upsType.length() > 0) {
                client.printf("VAR %s ups.type \"%s\"\n", upsName.c_str(), data.upsType.c_str());
            } else if (varNameLower == "ups.beeper.status") {
                client.printf("VAR %s ups.beeper.status \"%s\"\n", upsName.c_str(), data.beeperEnabled ? "enabled" : "disabled");
            } else if (varNameLower == "outlet.1.switch") {
                client.printf("VAR %s outlet.1.switch \"%d\"\n", upsName.c_str(), data.outlet1Switch ? 1 : 0);
            } else if (varNameLower == "outlet.2.switch") {
                client.printf("VAR %s outlet.2.switch \"%d\"\n", upsName.c_str(), data.outlet2Switch ? 1 : 0);
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
