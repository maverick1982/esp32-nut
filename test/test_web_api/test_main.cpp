#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include <string>
#include <utility>
#include <vector>
#include "WebApiJson.h"

// A simple mock for testing API JSON generation natively
class APIMockUSBHost : public IUSBHostUPS {
public:
    UPSData data;
    bool connected = true;
    std::vector<HIDUsageDef> usages;

    // Chiamate ai metodi dell'host usati dalle API delle variabili, per verificare che i
    // 400 di forma non lo tocchino (US-068)
    mutable int hostCalls = 0;

    void lock() const override {}
    void unlock() const override {}
    void end() override {}
    UPSDataLock getUPSData() const override { hostCalls++; return UPSDataLock(data, this); }
    bool isConnected() const override { hostCalls++; return connected; }
    String getUPSStatusString() const override { return connected ? "OL" : "UNKNOWN"; }
    bool setBeeper(bool enable) override { return true; }
    bool supportsBeeperToggle() const override { return true; }

    // New pure virtuals
    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return usages; }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) override { return true; }
    bool requestStringDescriptor(uint8_t string_index) override { return true; }

    // Scritture HID registrate (path, valore) per i test dei comandi (US-059)
    bool writeOk = true;
    std::vector<std::pair<std::string, uint32_t>> writes;
    bool writeUsage(const HIDUsageDef& def, uint32_t value) override {
        writes.emplace_back(def.path, value);
        return writeOk;
    }

    // Chiamate a executeCommand(), per verificare quando un comando arriva all'host (US-058)
    int executeCalls = 0;
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        executeCalls++;
        return IUSBHostUPS::executeCommand(name, param);
    }

    // --- US-068: variabili scrivibili ---

    bool stale = false;
    bool isDataStale() const override { hostCalls++; return stale; }

    // Catalogo LIST RW restituito così com'è
    std::vector<NutTable::RWVariable> rw;
    std::vector<NutTable::RWVariable> getWritableVariables() const override { hostCalls++; return rw; }

    // Scritture arrivate all'host, {name, value}, e il loro esito. Con OK il valore finisce
    // nei dati, oppure storedValue se impostato (un UPS che arrotonda)
    NutTable::SetStatus setResult = NutTable::SetStatus::OK;
    const char* storedValue = nullptr;
    std::vector<std::pair<std::string, std::string>> sets;
    NutTable::SetStatus setVariable(const char* name, const char* value) override {
        hostCalls++;
        sets.push_back({name, value});
        if (setResult == NutTable::SetStatus::OK) data.set(name, storedValue ? storedValue : value);
        if (disconnectOnWrite) connected = false;
        return setResult;
    }

    // Reset arrivati all'host e il loro esito. Con OK la variabile torna a resetDefault
    NutTable::SetStatus resetResult = NutTable::SetStatus::OK;
    const char* resetDefault = "";
    std::vector<std::string> resets;
    NutTable::SetStatus resetVariable(const char* name) override {
        hostCalls++;
        resets.push_back(name);
        if (resetResult == NutTable::SetStatus::OK) data.set(name, resetDefault);
        if (disconnectOnWrite) connected = false;
        return resetResult;
    }

    // UPS scollegato durante setVariable()/resetVariable(): l'host risponde comunque OK
    bool disconnectOnWrite = false;
};

APIMockUSBHost mockHost;

// Usage FEATURE con path e codice dati
static HIDUsageDef featureUsage(const char* path, uint32_t usage = 0) {
    HIDUsageDef u;
    u.usage = usage;
    u.report_type = 0x03;
    u.bit_size = 8;
    u.found = true;
    strncpy(u.path, path, sizeof(u.path) - 1);
    return u;
}

// UPS con test batteria e comandi di spegnimento (load.off, shutdown.return, ...)
static void addTestAndShutdownUsages() {
    mockHost.usages.push_back(featureUsage("UPS.BatterySystem.Battery.Test"));
    mockHost.usages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeShutdown"));
    mockHost.usages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeStartup"));
}

void setUp(void) {
    // Stato del mock ripristinato a ogni test
    mockHost.connected = true;
    mockHost.usages.clear();
    mockHost.writes.clear();
    mockHost.writeOk = true;
    mockHost.executeCalls = 0;
    mockHost.data = UPSData();
    mockHost.hostCalls = 0;
    mockHost.stale = false;
    mockHost.rw.clear();
    mockHost.setResult = NutTable::SetStatus::OK;
    mockHost.storedValue = nullptr;
    mockHost.sets.clear();
    mockHost.resetResult = NutTable::SetStatus::OK;
    mockHost.resetDefault = "";
    mockHost.resets.clear();
    mockHost.disconnectOnWrite = false;
}
void tearDown(void) {}

void test_api_null_host() {
    String out = WebApiJson::generateUpsVars(nullptr);
    TEST_ASSERT_EQUAL_STRING("{\"error\": \"UPS non inizializzato\"}", out.c_str());
}

void test_api_disconnected_host() {
    mockHost.connected = false;
    String out = WebApiJson::generateUpsVars(&mockHost);

    JsonDocument doc;
    deserializeJson(doc, out);

    TEST_ASSERT_TRUE(doc["_disconnected"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("Disconnected", doc["ups.status"].as<const char*>());
    // Ensure no other standard fields are present
    TEST_ASSERT_TRUE(doc["ups.mfr"].isNull());
    TEST_ASSERT_TRUE(doc["ups.beeper.switchable"].isNull());
}

void test_api_connected_host_with_data() {
    mockHost.connected = true;
    mockHost.data.set("ups.mfr", "Eaton");

    String out = WebApiJson::generateUpsVars(&mockHost);

    JsonDocument doc;
    deserializeJson(doc, out);

    TEST_ASSERT_TRUE(doc["_disconnected"].isNull());
    TEST_ASSERT_EQUAL_STRING("OL", doc["ups.status"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Eaton", doc["ups.mfr"].as<const char*>());
    TEST_ASSERT_TRUE(doc["ups.beeper.switchable"].as<bool>());
}

// --- US-059: generateUpsCommands ---

void test_commands_null_host() {
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(nullptr));
    TEST_ASSERT_FALSE(doc["connected"].as<bool>());
    TEST_ASSERT_TRUE(doc["commands"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, doc["commands"].size());
}

void test_commands_disconnected_host() {
    addTestAndShutdownUsages();
    mockHost.connected = false;
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost));
    TEST_ASSERT_FALSE(doc["connected"].as<bool>());
    TEST_ASSERT_TRUE(doc["commands"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, doc["commands"].size());
}

void test_commands_catalog() {
    addTestAndShutdownUsages();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost));
    TEST_ASSERT_TRUE(doc["connected"].as<bool>());

    bool foundQuick = false, foundLoadOff = false;
    for (JsonObject c : doc["commands"].as<JsonArray>()) {
        const char* name = c["name"];
        if (strcmp(name, "test.battery.start.quick") == 0) {
            foundQuick = true;
            TEST_ASSERT_FALSE(c["destructive"].as<bool>());
            TEST_ASSERT_EQUAL_STRING("Start a quick battery test", c["description"].as<const char*>());
        } else if (strcmp(name, "load.off") == 0) {
            foundLoadOff = true;
            TEST_ASSERT_TRUE(c["destructive"].as<bool>());
        }
    }
    TEST_ASSERT_TRUE(foundQuick);
    TEST_ASSERT_TRUE(foundLoadOff);
}

// US-068 (prima US-062): nessun campo locked, i distruttivi restano marcati destructive
// e la conferma la chiede la pagina, in STA come in AP
void test_commands_catalog_without_locked() {
    addTestAndShutdownUsages();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost));
    JsonArray commands = doc["commands"].as<JsonArray>();
    TEST_ASSERT_TRUE(commands.size() > 0);
    bool anyDestructive = false, anyOther = false;
    for (JsonObject c : commands) {
        const char* name = c["name"].as<const char*>();
        TEST_ASSERT_TRUE_MESSAGE(c["locked"].isNull(), name);
        TEST_ASSERT_TRUE_MESSAGE(c["destructive"].is<bool>(), name);
        if (c["destructive"].as<bool>()) anyDestructive = true; else anyOther = true;
        // Soli campi name, description, destructive
        TEST_ASSERT_EQUAL_MESSAGE(3, c.size(), name);
    }
    TEST_ASSERT_TRUE(anyDestructive);
    TEST_ASSERT_TRUE(anyOther);
}

// --- US-059: runUpsCommand ---

void test_run_command_ok() {
    addTestAndShutdownUsages();
    String response;
    int status = WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response);
    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.Test", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, mockHost.writes[0].second);
}

void test_run_command_invalid_json() {
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "not json", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", response.c_str());
}

void test_run_command_missing_name() {
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "{}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", response.c_str());
}

void test_run_command_unknown() {
    addTestAndShutdownUsages();
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"foo\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Command not supported\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
    TEST_ASSERT_EQUAL(0, mockHost.executeCalls);
}

void test_run_command_disconnected() {
    addTestAndShutdownUsages();
    mockHost.connected = false;
    String response;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_write_failed() {
    addTestAndShutdownUsages();
    mockHost.writeOk = false;
    String response;
    TEST_ASSERT_EQUAL(500, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Command rejected by the UPS\"}", response.c_str());
}

void test_run_command_not_supported_by_ups() {
    // Comando noto al catalogo ma senza l'usage Test nel descriptor
    mockHost.usages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeShutdown"));
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Command not supported\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_body_too_long() {
    addTestAndShutdownUsages();
    std::string padded = "{\"name\":\"test.battery.start.quick\",\"pad\":\"";
    padded.append(WebApiJson::MAX_COMMAND_BODY_LENGTH, 'x');
    padded += "\"}";
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, String(padded.c_str()), response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_extra_fields_ignored() {
    addTestAndShutdownUsages();
    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"x\":[1,2,3],\"name\":\"test.battery.start.quick\"}", response));
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
}

void test_run_command_null_host() {
    String response;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsCommand(nullptr, "{\"name\":\"test.battery.start.quick\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", response.c_str());
}

// --- US-062, US-068: comandi distruttivi eseguiti senza blocco, in STA come in AP ---

// Ritardi configurati come all'avvio del driver (ups.delay.*)
static void setConfiguredDelays() {
    mockHost.data.set("ups.delay.shutdown", "20");
    mockHost.data.set("ups.delay.start", "30");
}

// Prima US-068 era il 403 "Available via NUT only in Access Point mode": ora il comando
// arriva all'host (cambiamento deciso con l'utente il 2026-10-16)
void test_run_command_destructive_executed() {
    addTestAndShutdownUsages();
    setConfiguredDelays();
    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"shutdown.return\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(1, mockHost.executeCalls);
    TEST_ASSERT_EQUAL(2, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeStartup", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(30, mockHost.writes[0].second);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeShutdown", mockHost.writes[1].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(20, mockHost.writes[1].second);
}

// US-058: shutdown.default e i comandi con ritardo, prima rifiutati con 403, ora eseguiti
void test_run_command_shutdown_default_and_delay_executed() {
    addTestAndShutdownUsages();
    mockHost.usages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeReboot"));
    setConfiguredDelays();
    struct Case { const char* body; int executeCalls; };
    const Case cases[] = {
        { "{\"name\":\"shutdown.default\"}", 2 },  // shutdown.default, poi shutdown.return
        { "{\"name\":\"load.off.delay\"}", 1 },
        { "{\"name\":\"load.off.delay\",\"value\":\"30\"}", 1 },
    };
    for (const Case& c : cases) {
        mockHost.executeCalls = 0;
        mockHost.writes.clear();
        String response;
        TEST_ASSERT_EQUAL_MESSAGE(200, WebApiJson::runUpsCommand(&mockHost, c.body, response), c.body);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"success\":true}", response.c_str(), c.body);
        TEST_ASSERT_EQUAL_MESSAGE(c.executeCalls, mockHost.executeCalls, c.body);
        TEST_ASSERT_TRUE_MESSAGE(mockHost.writes.size() > 0, c.body);
    }
}

void test_run_command_load_off_delay_uses_configured_delay() {
    addTestAndShutdownUsages();
    setConfiguredDelays();
    String response;
    // "value" nel body e' ignorato: a executeCommand non arriva alcun parametro
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"load.off.delay\",\"value\":\"5\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeShutdown", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(20, mockHost.writes[0].second);
}

void test_run_command_shutdown_default_runs_fallback() {
    addTestAndShutdownUsages();
    setConfiguredDelays();
    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"shutdown.default\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(2, mockHost.executeCalls);  // shutdown.default, poi shutdown.return
    TEST_ASSERT_TRUE(mockHost.writes.size() > 0);
    TEST_ASSERT_EQUAL_STRING("shutdown.return", mockHost.lastDefaultShutdown().c_str());
}

void test_run_command_destructive_null_host() {
    // Prima US-068 il 403 arrivava prima del controllo sull'host: ora è un 503
    String response;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsCommand(nullptr, "{\"name\":\"load.off\"}", response));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", response.c_str());
}

// --- US-068: pagina "UPS Settings" ---

// Catalogo LIST RW nell'ordine di upsd (strcasecmp) e i valori correnti
static void addWritableVariables() {
    mockHost.rw = {
        { "battery.charge.low", 5, {}, false },
        { "outlet.1.desc", 20, {}, true },
        { "outlet.1.switchable", 3, { "yes", "no" }, false },
        { "outlet.3.desc", 20, {}, true },                 // senza VARDESC in cmdvartab
        { "ups.delay.shutdown", 10, {}, true },
        { "ups.shutdown", 8, { "enabled", "disabled" }, false },
        { "ups.test.aux", 0, {}, false },                   // aux < 1: insanity del driver
    };
    mockHost.data.set("battery.charge.low", "20");
    mockHost.data.set("outlet.1.desc", "Main Outlet");
    mockHost.data.set("outlet.1.switchable", "yes");
    mockHost.data.set("ups.delay.shutdown", "60");
    mockHost.data.set("ups.shutdown", "enabled");
    // outlet.3.desc e ups.test.aux senza valore nei dati
}

static JsonObject findVariable(JsonDocument& doc, const char* name) {
    for (JsonObject v : doc["variables"].as<JsonArray>()) {
        if (strcmp(v["name"].as<const char*>(), name) == 0) return v;
    }
    return JsonObject();
}

// ---- generateUpsVariables ----

void test_variables_catalog_payload() {
    addWritableVariables();
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, WebApiJson::generateUpsVariables(&mockHost)));
    TEST_ASSERT_TRUE(doc["connected"].as<bool>());
    JsonArray vars = doc["variables"].as<JsonArray>();
    TEST_ASSERT_EQUAL(7, vars.size());
    // Ordine di getWritableVariables()
    const char* order[] = { "battery.charge.low", "outlet.1.desc", "outlet.1.switchable", "outlet.3.desc",
                            "ups.delay.shutdown", "ups.shutdown", "ups.test.aux" };
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_STRING(order[i], vars[i]["name"].as<const char*>());
    }

    JsonObject v = vars[0];
    TEST_ASSERT_EQUAL(7, v.size());  // name, description, value, max_length, enum, driver_side, confirm
    TEST_ASSERT_EQUAL_STRING("Remaining battery level when UPS switches to LB (percent)", v["description"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("20", v["value"].as<const char*>());
    TEST_ASSERT_EQUAL(5, v["max_length"].as<int>());
    TEST_ASSERT_TRUE(v["enum"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, v["enum"].size());
    TEST_ASSERT_TRUE(v["driver_side"].is<bool>());
    TEST_ASSERT_FALSE(v["driver_side"].as<bool>());
    TEST_ASSERT_TRUE(v["confirm"].is<bool>());
    TEST_ASSERT_FALSE(v["confirm"].as<bool>());
}

void test_variables_description_fallback_and_missing_value() {
    addWritableVariables();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsVariables(&mockHost));
    JsonObject known = findVariable(doc, "outlet.1.desc");
    TEST_ASSERT_EQUAL_STRING("Outlet description", known["description"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Main Outlet", known["value"].as<const char*>());
    // Come il server NUT: "Description unavailable" senza VARDESC; valore vuoto senza chiave
    JsonObject unknown = findVariable(doc, "outlet.3.desc");
    TEST_ASSERT_EQUAL_STRING("Description unavailable", unknown["description"].as<const char*>());
    TEST_ASSERT_TRUE(unknown["value"].is<const char*>());
    TEST_ASSERT_EQUAL_STRING("", unknown["value"].as<const char*>());
}

void test_variables_enum_max_length_driver_side() {
    addWritableVariables();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsVariables(&mockHost));
    JsonObject sw = findVariable(doc, "outlet.1.switchable");
    TEST_ASSERT_EQUAL(3, sw["max_length"].as<int>());
    TEST_ASSERT_EQUAL(2, sw["enum"].size());
    TEST_ASSERT_EQUAL_STRING("yes", sw["enum"][0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("no", sw["enum"][1].as<const char*>());
    TEST_ASSERT_FALSE(sw["driver_side"].as<bool>());

    JsonObject delay = findVariable(doc, "ups.delay.shutdown");
    TEST_ASSERT_EQUAL(10, delay["max_length"].as<int>());
    TEST_ASSERT_TRUE(delay["driver_side"].as<bool>());
    TEST_ASSERT_TRUE(findVariable(doc, "outlet.1.desc")["driver_side"].as<bool>());
    TEST_ASSERT_EQUAL(0, findVariable(doc, "ups.test.aux")["max_length"].as<int>());
}

void test_variables_confirm_flag() {
    addWritableVariables();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsVariables(&mockHost));
    TEST_ASSERT_TRUE(findVariable(doc, "ups.shutdown")["confirm"].as<bool>());
    TEST_ASSERT_TRUE(findVariable(doc, "outlet.1.switchable")["confirm"].as<bool>());
    TEST_ASSERT_FALSE(findVariable(doc, "outlet.1.desc")["confirm"].as<bool>());
    TEST_ASSERT_FALSE(findVariable(doc, "ups.delay.shutdown")["confirm"].as<bool>());
}

void test_variables_disconnected_or_null_host() {
    addWritableVariables();
    mockHost.connected = false;
    IUSBHostUPS* hosts[] = { &mockHost, nullptr };
    for (IUSBHostUPS* host : hosts) {
        JsonDocument doc;
        TEST_ASSERT_FALSE(deserializeJson(doc, WebApiJson::generateUpsVariables(host)));
        TEST_ASSERT_TRUE(doc["connected"].is<bool>());
        TEST_ASSERT_FALSE(doc["connected"].as<bool>());
        TEST_ASSERT_TRUE(doc["variables"].is<JsonArray>());
        TEST_ASSERT_EQUAL(0, doc["variables"].size());
    }
}

void test_variables_empty_catalog() {
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsVariables(&mockHost));
    TEST_ASSERT_TRUE(doc["connected"].as<bool>());
    TEST_ASSERT_TRUE(doc["variables"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, doc["variables"].size());
}

// ---- runUpsVariable ----

// Esegue runUpsVariable e verifica status e risposta; restituisce host_called
static bool expectSet(const char* body, int status, const char* response) {
    String out;
    bool host_called = true;
    TEST_ASSERT_EQUAL_MESSAGE(status, WebApiJson::runUpsVariable(&mockHost, body, out, &host_called), body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(response, out.c_str(), body);
    return host_called;
}

// 400 di forma: nessuna chiamata all'host
void test_set_variable_invalid_request() {
    addWritableVariables();
    std::string tooLong = "{\"name\":\"battery.charge.low\",\"value\":\"30\",\"pad\":\"";
    tooLong.append(WebApiJson::MAX_COMMAND_BODY_LENGTH, 'x');
    tooLong += "\"}";
    const char* bodies[] = {
        "not json",
        "",
        "{\"name\":\"battery.charge.low\",\"value\":30}",     // value numerico
        "{\"name\":\"battery.charge.low\",\"value\":true}",
        "{\"name\":\"battery.charge.low\",\"value\":null}",
        "{\"name\":\"battery.charge.low\"}",                  // value mancante
        "{\"value\":\"30\"}",                                 // name mancante
        "{\"name\":\"\",\"value\":\"30\"}",
        "{\"name\":5,\"value\":\"30\"}",
        tooLong.c_str(),
    };
    for (const char* body : bodies) {
        TEST_ASSERT_FALSE_MESSAGE(expectSet(body, 400, "{\"error\":\"Invalid request\"}"), body);
    }
    TEST_ASSERT_EQUAL(0, mockHost.hostCalls);
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_invalid_request_null_host() {
    String out;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsVariable(nullptr, "{\"name\":\"battery.charge.low\",\"value\":30}", out));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", out.c_str());
}

// Come il lettore di riga del server NUT: fuori da 0x20-0x7E il valore non arriva all'host
void test_set_variable_control_chars_rejected() {
    addWritableVariables();
    const char* bodies[] = {
        "{\"name\":\"battery.charge.low\",\"value\":\"3\\n0\"}",
        "{\"name\":\"battery.charge.low\",\"value\":\"3\\r\"}",
        "{\"name\":\"battery.charge.low\",\"value\":\"\\t30\"}",
        "{\"name\":\"battery.charge.low\",\"value\":\"\\u0001\"}",
        "{\"name\":\"battery.charge.low\",\"value\":\"\\u007f\"}",
        "{\"name\":\"battery.charge.low\",\"value\":\"\\u00e0\"}",   // non ASCII (UTF-8)
    };
    for (const char* body : bodies) {
        TEST_ASSERT_FALSE_MESSAGE(expectSet(body, 400, "{\"error\":\"Invalid value\"}"), body);
    }
    TEST_ASSERT_EQUAL(0, mockHost.hostCalls);
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_not_writable() {
    addWritableVariables();
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"ups.mfr\",\"value\":\"x\"}", 400, "{\"error\":\"Variable not writable\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_invalid_aux() {
    addWritableVariables();
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"ups.test.aux\",\"value\":\"1\"}", 400, "{\"error\":\"Variable not writable\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_too_long() {
    addWritableVariables();
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"123456\"}", 400,
                                "{\"error\":\"Value too long (max 5)\"}"));
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"outlet.1.desc\",\"value\":\"123456789012345678901\"}", 400,
                                "{\"error\":\"Value too long (max 20)\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_invalid_enum() {
    addWritableVariables();
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"ups.shutdown\",\"value\":\"maybe\"}", 400, "{\"error\":\"Invalid value\"}"));
    // ENUM confrontati con strcmp, come upsd
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"outlet.1.switchable\",\"value\":\"YES\"}", 400, "{\"error\":\"Invalid value\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_conversion_failed() {
    addWritableVariables();
    mockHost.setResult = NutTable::SetStatus::CONVERSION_FAILED;
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"abc\"}", 400, "{\"error\":\"Invalid value\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("20", mockHost.data.get("battery.charge.low").c_str());
}

void test_set_variable_unknown() {
    addWritableVariables();
    mockHost.setResult = NutTable::SetStatus::UNKNOWN;
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 400, "{\"error\":\"Variable not supported\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.sets.size());
}

void test_set_variable_disconnected() {
    addWritableVariables();
    mockHost.connected = false;
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 503, "{\"error\":\"UPS not connected\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

void test_set_variable_null_host() {
    String out;
    bool host_called = true;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsVariable(nullptr, "{\"name\":\"battery.charge.low\",\"value\":\"30\"}", out, &host_called));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", out.c_str());
    TEST_ASSERT_FALSE(host_called);
}

void test_set_variable_stale() {
    addWritableVariables();
    mockHost.stale = true;
    TEST_ASSERT_FALSE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 503, "{\"error\":\"UPS data stale\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.sets.size());
}

// Host scollegato tra i controlli e la scrittura
void test_set_variable_not_connected_from_host() {
    addWritableVariables();
    mockHost.setResult = NutTable::SetStatus::NOT_CONNECTED;
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 503, "{\"error\":\"UPS not connected\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.sets.size());
}

void test_set_variable_failed() {
    addWritableVariables();
    mockHost.setResult = NutTable::SetStatus::FAILED;
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 500, "{\"error\":\"Rejected by the UPS\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("20", mockHost.data.get("battery.charge.low").c_str());
}

void test_set_variable_ok() {
    addWritableVariables();
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 200, "{\"success\":true,\"value\":\"30\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", mockHost.sets[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("30", mockHost.sets[0].second.c_str());
    // Valore di lunghezza esattamente max_length e valore ENUM valido
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"12345\"}", 200, "{\"success\":true,\"value\":\"12345\"}"));
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"ups.shutdown\",\"value\":\"disabled\"}", 200, "{\"success\":true,\"value\":\"disabled\"}"));
}

// Il valore della risposta è quello dei dati dopo la scrittura, non quello inviato
void test_set_variable_ok_returns_stored_value() {
    addWritableVariables();
    mockHost.storedValue = "35";
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"33\"}", 200, "{\"success\":true,\"value\":\"35\"}"));
    TEST_ASSERT_EQUAL_STRING("33", mockHost.sets[0].second.c_str());
}

// Il nome arriva a setVariable() nella forma del catalogo
void test_set_variable_canonical_name() {
    addWritableVariables();
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"Battery.Charge.LOW\",\"value\":\"30\"}", 200, "{\"success\":true,\"value\":\"30\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", mockHost.sets[0].first.c_str());
}

// Un valore con virgolette resta JSON valido nella risposta
void test_set_variable_ok_escapes_value() {
    addWritableVariables();
    String out;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsVariable(&mockHost, "{\"name\":\"outlet.1.desc\",\"value\":\"My \\\"UPS\\\"\"}", out));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, out));
    TEST_ASSERT_TRUE(doc["success"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("My \"UPS\"", doc["value"].as<const char*>());
}

void test_set_variable_extra_fields_ignored() {
    addWritableVariables();
    TEST_ASSERT_TRUE(expectSet("{\"x\":[1,2],\"name\":\"battery.charge.low\",\"value\":\"30\"}", 200,
                               "{\"success\":true,\"value\":\"30\"}"));
}

// ---- resetUpsVariable ----

static bool expectReset(const char* body, int status, const char* response) {
    String out;
    bool host_called = true;
    TEST_ASSERT_EQUAL_MESSAGE(status, WebApiJson::resetUpsVariable(&mockHost, body, out, &host_called), body);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(response, out.c_str(), body);
    return host_called;
}

void test_reset_variable_invalid_request() {
    addWritableVariables();
    const char* bodies[] = { "not json", "{}", "{\"name\":\"\"}", "{\"name\":1}" };
    for (const char* body : bodies) {
        TEST_ASSERT_FALSE_MESSAGE(expectReset(body, 400, "{\"error\":\"Invalid request\"}"), body);
    }
    TEST_ASSERT_EQUAL(0, mockHost.hostCalls);
    TEST_ASSERT_EQUAL(0, mockHost.resets.size());
}

void test_reset_variable_not_driver_side() {
    addWritableVariables();
    TEST_ASSERT_FALSE(expectReset("{\"name\":\"battery.charge.low\"}", 400, "{\"error\":\"Not a driver-side variable\"}"));
    TEST_ASSERT_FALSE(expectReset("{\"name\":\"ups.mfr\"}", 400, "{\"error\":\"Not a driver-side variable\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.resets.size());
}

void test_reset_variable_unknown_from_host() {
    addWritableVariables();
    mockHost.resetResult = NutTable::SetStatus::UNKNOWN;
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 400, "{\"error\":\"Not a driver-side variable\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.resets.size());
}

void test_reset_variable_disconnected_and_stale() {
    addWritableVariables();
    mockHost.connected = false;
    TEST_ASSERT_FALSE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 503, "{\"error\":\"UPS not connected\"}"));
    mockHost.connected = true;
    mockHost.stale = true;
    TEST_ASSERT_FALSE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 503, "{\"error\":\"UPS data stale\"}"));
    TEST_ASSERT_EQUAL(0, mockHost.resets.size());

    String out;
    TEST_ASSERT_EQUAL(503, WebApiJson::resetUpsVariable(nullptr, "{\"name\":\"ups.delay.shutdown\"}", out));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", out.c_str());
}

void test_reset_variable_not_connected_from_host() {
    addWritableVariables();
    mockHost.resetResult = NutTable::SetStatus::NOT_CONNECTED;
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 503, "{\"error\":\"UPS not connected\"}"));
}

void test_reset_variable_failed() {
    addWritableVariables();
    mockHost.resetResult = NutTable::SetStatus::FAILED;
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 500, "{\"error\":\"Could not save the setting\"}"));
    TEST_ASSERT_EQUAL_STRING("60", mockHost.data.get("ups.delay.shutdown").c_str());
}

void test_reset_variable_ok() {
    addWritableVariables();
    mockHost.resetDefault = "20";
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 200, "{\"success\":true,\"value\":\"20\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.resets.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", mockHost.resets[0].c_str());

    mockHost.resetDefault = "Main Outlet";
    mockHost.data.set("outlet.1.desc", "Server rack");
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"outlet.1.desc\"}", 200, "{\"success\":true,\"value\":\"Main Outlet\"}"));
}

// UPS scollegato dopo l'OK dell'host: value null, il valore vale dalla connessione successiva
void test_set_and_reset_variable_ok_then_disconnected() {
    addWritableVariables();
    mockHost.disconnectOnWrite = true;
    TEST_ASSERT_TRUE(expectSet("{\"name\":\"battery.charge.low\",\"value\":\"30\"}", 200, "{\"success\":true,\"value\":null}"));
    mockHost.connected = true;
    mockHost.resetDefault = "20";
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"ups.delay.shutdown\"}", 200, "{\"success\":true,\"value\":null}"));
}

void test_reset_variable_canonical_name() {
    addWritableVariables();
    mockHost.resetDefault = "20";
    TEST_ASSERT_TRUE(expectReset("{\"name\":\"UPS.Delay.Shutdown\"}", 200, "{\"success\":true,\"value\":\"20\"}"));
    TEST_ASSERT_EQUAL(1, mockHost.resets.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", mockHost.resets[0].c_str());
}

// ---- needsConfirmation ----

// ---- UPS name of the NUT configuration (US-082) ----

static String nameFromConfig(const char* json, bool& defaulted) {
    JsonDocument doc;
    deserializeJson(doc, json);
    return WebApiJson::upsNameFromConfig(doc["nut"]["ups_name"], &defaulted);
}

void test_ups_name_from_config() {
    bool defaulted = false;
    // Missing key: as<String>() would give "null", the default is used instead
    TEST_ASSERT_EQUAL_STRING("ups", nameFromConfig("{\"nut\":{\"username\":\"\"}}", defaulted).c_str());
    TEST_ASSERT_TRUE(defaulted);
    TEST_ASSERT_EQUAL_STRING("ups", nameFromConfig("{\"nut\":{\"ups_name\":\"\"}}", defaulted).c_str());
    TEST_ASSERT_TRUE(defaulted);
    TEST_ASSERT_EQUAL_STRING("ups", nameFromConfig("{\"nut\":{\"ups_name\":null}}", defaulted).c_str());
    TEST_ASSERT_TRUE(defaulted);
    TEST_ASSERT_EQUAL_STRING("ups", nameFromConfig("{\"nut\":{\"ups_name\":5}}", defaulted).c_str());
    TEST_ASSERT_TRUE(defaulted);
    TEST_ASSERT_EQUAL_STRING("eaton", nameFromConfig("{\"nut\":{\"ups_name\":\"eaton\"}}", defaulted).c_str());
    TEST_ASSERT_FALSE(defaulted);
    // defaulted is optional
    JsonDocument doc;
    TEST_ASSERT_EQUAL_STRING("ups", WebApiJson::upsNameFromConfig(doc["missing"]).c_str());
}

static int nutConfigStatus(const char* json, String& out) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    out = "";
    return WebApiJson::checkNutConfigBody(doc, out);
}

void test_nut_config_body_check() {
    String out;
    // Without ups_name (or null) the saved name stays: no error
    TEST_ASSERT_EQUAL(0, nutConfigStatus("{\"username\":\"admin\",\"password\":\"x\"}", out));
    TEST_ASSERT_EQUAL(0, nutConfigStatus("{\"ups_name\":null}", out));
    TEST_ASSERT_EQUAL(0, nutConfigStatus("{\"ups_name\":\"ups\"}", out));
    TEST_ASSERT_EQUAL(0, nutConfigStatus("{\"ups_name\":\"Back-UPS_1\"}", out));
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    const char* invalid[] = {
        "{\"ups_name\":\"\"}", "{\"ups_name\":\"my ups\"}", "{\"ups_name\":\"a#b\"}",
        "{\"ups_name\":\"a@b\"}", "{\"ups_name\":\"citt\\u00e0\"}", "{\"ups_name\":123}",
        "{\"ups_name\":true}", "{\"ups_name\":[\"ups\"]}",
    };
    for (const char* json : invalid) {
        TEST_ASSERT_EQUAL_MESSAGE(400, nutConfigStatus(json, out), json);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"error\":\"Invalid UPS name\"}", out.c_str(), json);
    }
}

void test_needs_confirmation_spec_names() {
    const char* names[] = {
        "ups.shutdown",
        "outlet.switchable",
        "outlet.1.switchable",
        "outlet.2.switchable",
        "outlet.12.switchable",
        "input.bypass.switchable",
        "input.bypass.switch.on",
        "input.eco.switchable",
        "input.transfer.bypass.forced",
    };
    for (const char* n : names) TEST_ASSERT_TRUE_MESSAGE(WebApiJson::needsConfirmation(n), n);
}

void test_needs_confirmation_other_names() {
    const char* names[] = {
        "outlet.1.desc",
        "battery.charge.low",
        "input.transfer.low",
        "input.transfer.bypass.high",
        "input.bypass.",
        "outlet..switchable",
        "outlet.x.switchable",
        "outlet.1.switchable.x",
        "outlet.1.delay.shutdown",
        "ups.shutdown.x",
        "ups.delay.shutdown",
        "",
    };
    for (const char* n : names) TEST_ASSERT_FALSE_MESSAGE(WebApiJson::needsConfirmation(n), n);
    TEST_ASSERT_FALSE(WebApiJson::needsConfirmation(nullptr));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_api_null_host);
    RUN_TEST(test_api_disconnected_host);
    RUN_TEST(test_api_connected_host_with_data);
    RUN_TEST(test_commands_null_host);
    RUN_TEST(test_commands_disconnected_host);
    RUN_TEST(test_commands_catalog);
    RUN_TEST(test_commands_catalog_without_locked);
    RUN_TEST(test_run_command_ok);
    RUN_TEST(test_run_command_invalid_json);
    RUN_TEST(test_run_command_missing_name);
    RUN_TEST(test_run_command_unknown);
    RUN_TEST(test_run_command_destructive_executed);
    RUN_TEST(test_run_command_shutdown_default_and_delay_executed);
    RUN_TEST(test_run_command_disconnected);
    RUN_TEST(test_run_command_write_failed);
    RUN_TEST(test_run_command_null_host);
    RUN_TEST(test_run_command_not_supported_by_ups);
    RUN_TEST(test_run_command_body_too_long);
    RUN_TEST(test_run_command_extra_fields_ignored);
    RUN_TEST(test_run_command_load_off_delay_uses_configured_delay);
    RUN_TEST(test_run_command_shutdown_default_runs_fallback);
    RUN_TEST(test_run_command_destructive_null_host);
    RUN_TEST(test_variables_catalog_payload);
    RUN_TEST(test_variables_description_fallback_and_missing_value);
    RUN_TEST(test_variables_enum_max_length_driver_side);
    RUN_TEST(test_variables_confirm_flag);
    RUN_TEST(test_variables_disconnected_or_null_host);
    RUN_TEST(test_variables_empty_catalog);
    RUN_TEST(test_set_variable_invalid_request);
    RUN_TEST(test_set_variable_invalid_request_null_host);
    RUN_TEST(test_set_variable_control_chars_rejected);
    RUN_TEST(test_set_variable_not_writable);
    RUN_TEST(test_set_variable_invalid_aux);
    RUN_TEST(test_set_variable_too_long);
    RUN_TEST(test_set_variable_invalid_enum);
    RUN_TEST(test_set_variable_conversion_failed);
    RUN_TEST(test_set_variable_unknown);
    RUN_TEST(test_set_variable_disconnected);
    RUN_TEST(test_set_variable_null_host);
    RUN_TEST(test_set_variable_stale);
    RUN_TEST(test_set_variable_not_connected_from_host);
    RUN_TEST(test_set_variable_failed);
    RUN_TEST(test_set_variable_ok);
    RUN_TEST(test_set_variable_ok_returns_stored_value);
    RUN_TEST(test_set_variable_canonical_name);
    RUN_TEST(test_set_variable_ok_escapes_value);
    RUN_TEST(test_set_variable_extra_fields_ignored);
    RUN_TEST(test_reset_variable_invalid_request);
    RUN_TEST(test_reset_variable_not_driver_side);
    RUN_TEST(test_reset_variable_unknown_from_host);
    RUN_TEST(test_reset_variable_disconnected_and_stale);
    RUN_TEST(test_reset_variable_not_connected_from_host);
    RUN_TEST(test_reset_variable_failed);
    RUN_TEST(test_reset_variable_ok);
    RUN_TEST(test_reset_variable_canonical_name);
    RUN_TEST(test_set_and_reset_variable_ok_then_disconnected);
    RUN_TEST(test_ups_name_from_config);
    RUN_TEST(test_nut_config_body_check);
    RUN_TEST(test_needs_confirmation_spec_names);
    RUN_TEST(test_needs_confirmation_other_names);
    return UNITY_END();
}
