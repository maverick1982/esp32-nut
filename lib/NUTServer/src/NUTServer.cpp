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
                if (data->has.manufacturer) client.printf("VAR %s ups.mfr \"%s\"\n", upsName.c_str(), data->manufacturer.c_str());
                if (data->has.product) client.printf("VAR %s ups.model \"%s\"\n", upsName.c_str(), data->product.c_str());
                if (data->has.serialNumber) client.printf("VAR %s ups.serial \"%s\"\n", upsName.c_str(), data->serialNumber.c_str());
                if (data->has.remainingCapacity) client.printf("VAR %s battery.charge \"%d\"\n", upsName.c_str(), data->remainingCapacity);
                if (data->has.remainingCapacityLimit) client.printf("VAR %s battery.charge.low \"%d\"\n", upsName.c_str(), data->remainingCapacityLimit);
                if (data->has.designCapacity) client.printf("VAR %s battery.capacity \"%d\"\n", upsName.c_str(), data->designCapacity);
                if (data->has.fullChargeCapacity) client.printf("VAR %s battery.capacity.full \"%d\"\n", upsName.c_str(), data->fullChargeCapacity);
                if (data->has.runTimeToEmpty) client.printf("VAR %s battery.runtime \"%d\"\n", upsName.c_str(), data->runTimeToEmpty);
                if (data->has.outputVoltage) client.printf("VAR %s output.voltage \"%.1f\"\n", upsName.c_str(), data->outputVoltage);
                if (data->has.inputVoltage) client.printf("VAR %s input.voltage \"%.1f\"\n", upsName.c_str(), data->inputVoltage);
                if (data->has.batteryVoltage) client.printf("VAR %s battery.voltage \"%.1f\"\n", upsName.c_str(), data->batteryVoltage);
                if (data->has.batteryTemperature) client.printf("VAR %s battery.temperature \"%.1f\"\n", upsName.c_str(), data->batteryTemperature);
                if (data->has.highVoltageTransfer) client.printf("VAR %s input.transfer.high \"%d\"\n", upsName.c_str(), data->highVoltageTransfer);
                if (data->has.lowVoltageTransfer) client.printf("VAR %s input.transfer.low \"%d\"\n", upsName.c_str(), data->lowVoltageTransfer);
                if (data->has.configApparentPower) client.printf("VAR %s ups.power.nominal \"%d\"\n", upsName.c_str(), data->configApparentPower);
                if (data->has.configActivePower) client.printf("VAR %s ups.realpower.nominal \"%d\"\n", upsName.c_str(), data->configActivePower);
                if (data->has.configFrequency) client.printf("VAR %s input.frequency.nominal \"%d\"\n", upsName.c_str(), data->configFrequency);
                if (data->has.configVoltage) client.printf("VAR %s input.voltage.nominal \"%d\"\n", upsName.c_str(), data->configVoltage);
                if (data->has.outputVoltageNominal) client.printf("VAR %s output.voltage.nominal \"%d\"\n", upsName.c_str(), data->outputVoltageNominal);
                if (data->has.outputFrequencyNominal) client.printf("VAR %s output.frequency.nominal \"%d\"\n", upsName.c_str(), data->outputFrequencyNominal);
                if (data->has.load) client.printf("VAR %s ups.load \"%d\"\n", upsName.c_str(), data->load);
                if (data->has.realPower) client.printf("VAR %s ups.realpower \"%d\"\n", upsName.c_str(), data->realPower);
                if (data->has.delayShutdown) client.printf("VAR %s ups.delay.shutdown \"%d\"\n", upsName.c_str(), data->delayShutdown);
                if (data->has.delayStart) client.printf("VAR %s ups.delay.start \"%d\"\n", upsName.c_str(), data->delayStart);
                if (data->has.timerStart) client.printf("VAR %s ups.timer.start \"%d\"\n", upsName.c_str(), data->timerStart);
                if (data->has.timerShutdown) client.printf("VAR %s ups.timer.shutdown \"%d\"\n", upsName.c_str(), data->timerShutdown);
                if (data->has.batteryType) client.printf("VAR %s battery.type \"%s\"\n", upsName.c_str(), data->batteryType.c_str());
                if (data->has.upsMfrDate) client.printf("VAR %s ups.mfr.date \"%s\"\n", upsName.c_str(), data->upsMfrDate.c_str());
                if (data->has.batteryMfrDate) client.printf("VAR %s battery.mfr.date \"%s\"\n", upsName.c_str(), data->batteryMfrDate.c_str());
                if (data->has.batteryDate) client.printf("VAR %s battery.date \"%s\"\n", upsName.c_str(), data->batteryDate.c_str());
                if (data->has.upsType) client.printf("VAR %s ups.type \"%s\"\n", upsName.c_str(), data->upsType.c_str());
                if (data->has.beeperEnabled) client.printf("VAR %s ups.beeper.status \"%s\"\n", upsName.c_str(), data->beeperEnabled ? "enabled" : "disabled");
                if (data->has.outlet1Switch) client.printf("VAR %s outlet.1.switch \"%d\"\n", upsName.c_str(), data->outlet1Switch ? 1 : 0);
                if (data->has.outlet2Switch) client.printf("VAR %s outlet.2.switch \"%d\"\n", upsName.c_str(), data->outlet2Switch ? 1 : 0);
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
                if (_usb_ups->getUPSData()->has.beeperEnabled) {
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
            if (!_usb_ups || !_usb_ups->getUPSData()->has.beeperEnabled) {
                client.print("ERR CMD-NOT-SUPPORTED\n");
            } else {
                _usb_ups->setBeeper(true);
                client.print("OK\n");
            }
            return;
        } else if (cmdName == "beeper.disable") {
            if (!_usb_ups || !_usb_ups->getUPSData()->has.beeperEnabled) {
                client.print("ERR CMD-NOT-SUPPORTED\n");
            } else {
                _usb_ups->setBeeper(false);
                client.print("OK\n");
            }
            return;
        } else if (cmdName == "beeper.toggle") {
            if (!_usb_ups || !_usb_ups->getUPSData()->has.beeperEnabled) {
                client.print("ERR CMD-NOT-SUPPORTED\n");
            } else {
                _usb_ups->setBeeper(!_usb_ups->getUPSData()->beeperEnabled);
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
            } else if (varNameLower == "ups.mfr" && data->has.manufacturer) {
                client.printf("VAR %s ups.mfr \"%s\"\n", upsName.c_str(), data->manufacturer.c_str());
            } else if (varNameLower == "ups.model" && data->has.product) {
                client.printf("VAR %s ups.model \"%s\"\n", upsName.c_str(), data->product.c_str());
            } else if (varNameLower == "ups.serial" && data->has.serialNumber) {
                client.printf("VAR %s ups.serial \"%s\"\n", upsName.c_str(), data->serialNumber.c_str());
            } else if (varNameLower == "battery.charge" && data->has.remainingCapacity) {
                client.printf("VAR %s battery.charge \"%d\"\n", upsName.c_str(), data->remainingCapacity);
            } else if (varNameLower == "battery.charge.low" && data->has.remainingCapacityLimit) {
                client.printf("VAR %s battery.charge.low \"%d\"\n", upsName.c_str(), data->remainingCapacityLimit);
            } else if (varNameLower == "battery.capacity" && data->has.designCapacity) {
                client.printf("VAR %s battery.capacity \"%d\"\n", upsName.c_str(), data->designCapacity);
            } else if (varNameLower == "battery.capacity.full" && data->has.fullChargeCapacity) {
                client.printf("VAR %s battery.capacity.full \"%d\"\n", upsName.c_str(), data->fullChargeCapacity);
            } else if (varNameLower == "battery.runtime" && data->has.runTimeToEmpty) {
                client.printf("VAR %s battery.runtime \"%d\"\n", upsName.c_str(), data->runTimeToEmpty);
            } else if (varNameLower == "output.voltage" && data->has.outputVoltage) {
                client.printf("VAR %s output.voltage \"%.1f\"\n", upsName.c_str(), data->outputVoltage);
            } else if (varNameLower == "input.voltage" && data->has.inputVoltage) {
                client.printf("VAR %s input.voltage \"%.1f\"\n", upsName.c_str(), data->inputVoltage);
            } else if (varNameLower == "battery.voltage" && data->has.batteryVoltage) {
                client.printf("VAR %s battery.voltage \"%.1f\"\n", upsName.c_str(), data->batteryVoltage);
            } else if (varNameLower == "battery.temperature" && data->has.batteryTemperature) {
                client.printf("VAR %s battery.temperature \"%.1f\"\n", upsName.c_str(), data->batteryTemperature);
            } else if (varNameLower == "input.transfer.high" && data->has.highVoltageTransfer) {
                client.printf("VAR %s input.transfer.high \"%d\"\n", upsName.c_str(), data->highVoltageTransfer);
            } else if (varNameLower == "input.transfer.low" && data->has.lowVoltageTransfer) {
                client.printf("VAR %s input.transfer.low \"%d\"\n", upsName.c_str(), data->lowVoltageTransfer);
            } else if (varNameLower == "ups.power.nominal" && data->has.configApparentPower) {
                client.printf("VAR %s ups.power.nominal \"%d\"\n", upsName.c_str(), data->configApparentPower);
            } else if (varNameLower == "ups.realpower.nominal" && data->has.configActivePower) {
                client.printf("VAR %s ups.realpower.nominal \"%d\"\n", upsName.c_str(), data->configActivePower);
            } else if (varNameLower == "input.frequency.nominal" && data->has.configFrequency) {
                client.printf("VAR %s input.frequency.nominal \"%d\"\n", upsName.c_str(), data->configFrequency);
            } else if (varNameLower == "input.voltage.nominal" && data->has.configVoltage) {
                client.printf("VAR %s input.voltage.nominal \"%d\"\n", upsName.c_str(), data->configVoltage);
            } else if (varNameLower == "output.voltage.nominal" && data->has.outputVoltageNominal) {
                client.printf("VAR %s output.voltage.nominal \"%d\"\n", upsName.c_str(), data->outputVoltageNominal);
            } else if (varNameLower == "output.frequency.nominal" && data->has.outputFrequencyNominal) {
                client.printf("VAR %s output.frequency.nominal \"%d\"\n", upsName.c_str(), data->outputFrequencyNominal);
            } else if (varNameLower == "ups.load" && data->has.load) {
                client.printf("VAR %s ups.load \"%d\"\n", upsName.c_str(), data->load);
            } else if (varNameLower == "ups.realpower" && data->has.realPower) {
                client.printf("VAR %s ups.realpower \"%d\"\n", upsName.c_str(), data->realPower);
            } else if (varNameLower == "ups.delay.shutdown" && data->has.delayShutdown) {
                client.printf("VAR %s ups.delay.shutdown \"%d\"\n", upsName.c_str(), data->delayShutdown);
            } else if (varNameLower == "ups.delay.start" && data->has.delayStart) {
                client.printf("VAR %s ups.delay.start \"%d\"\n", upsName.c_str(), data->delayStart);
            } else if (varNameLower == "ups.timer.start" && data->has.timerStart) {
                client.printf("VAR %s ups.timer.start \"%d\"\n", upsName.c_str(), data->timerStart);
            } else if (varNameLower == "ups.timer.shutdown" && data->has.timerShutdown) {
                client.printf("VAR %s ups.timer.shutdown \"%d\"\n", upsName.c_str(), data->timerShutdown);
            } else if (varNameLower == "battery.type" && data->has.batteryType) {
                client.printf("VAR %s battery.type \"%s\"\n", upsName.c_str(), data->batteryType.c_str());
            } else if (varNameLower == "ups.mfr.date" && data->has.upsMfrDate) {
                client.printf("VAR %s ups.mfr.date \"%s\"\n", upsName.c_str(), data->upsMfrDate.c_str());
            } else if (varNameLower == "battery.mfr.date" && data->has.batteryMfrDate) {
                client.printf("VAR %s battery.mfr.date \"%s\"\n", upsName.c_str(), data->batteryMfrDate.c_str());
            } else if (varNameLower == "battery.date" && data->has.batteryDate) {
                client.printf("VAR %s battery.date \"%s\"\n", upsName.c_str(), data->batteryDate.c_str());
            } else if (varNameLower == "ups.type" && data->has.upsType) {
                client.printf("VAR %s ups.type \"%s\"\n", upsName.c_str(), data->upsType.c_str());
            } else if (varNameLower == "ups.beeper.status" && data->has.beeperEnabled) {
                client.printf("VAR %s ups.beeper.status \"%s\"\n", upsName.c_str(), data->beeperEnabled ? "enabled" : "disabled");
            } else if (varNameLower == "outlet.1.switch" && data->has.outlet1Switch) {
                client.printf("VAR %s outlet.1.switch \"%d\"\n", upsName.c_str(), data->outlet1Switch ? 1 : 0);
            } else if (varNameLower == "outlet.2.switch" && data->has.outlet2Switch) {
                client.printf("VAR %s outlet.2.switch \"%d\"\n", upsName.c_str(), data->outlet2Switch ? 1 : 0);
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



