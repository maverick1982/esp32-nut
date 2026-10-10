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

    void lock() const override {}
    void unlock() const override {}
    void end() override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(data, this); }
    bool isConnected() const override { return connected; }
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

    // Chiamate a executeCommand(), per verificare che il 403 arrivi prima dell'host (US-058)
    int executeCalls = 0;
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        executeCalls++;
        return IUSBHostUPS::executeCommand(name, param);
    }
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
    deserializeJson(doc, WebApiJson::generateUpsCommands(nullptr, false));
    TEST_ASSERT_FALSE(doc["connected"].as<bool>());
    TEST_ASSERT_TRUE(doc["commands"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, doc["commands"].size());
}

void test_commands_disconnected_host() {
    addTestAndShutdownUsages();
    mockHost.connected = false;
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost, false));
    TEST_ASSERT_FALSE(doc["connected"].as<bool>());
    TEST_ASSERT_TRUE(doc["commands"].is<JsonArray>());
    TEST_ASSERT_EQUAL(0, doc["commands"].size());
}

void test_commands_catalog() {
    addTestAndShutdownUsages();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost, false));
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

// US-062: senza permesso (AP) i comandi distruttivi sono locked, gli altri no
void test_commands_catalog_locked_when_not_allowed() {
    addTestAndShutdownUsages();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost, false));
    JsonArray commands = doc["commands"].as<JsonArray>();
    TEST_ASSERT_TRUE(commands.size() > 0);
    bool anyDestructive = false, anyOther = false;
    for (JsonObject c : commands) {
        TEST_ASSERT_TRUE_MESSAGE(c["locked"].is<bool>(), c["name"].as<const char*>());
        bool destructive = c["destructive"].as<bool>();
        if (destructive) anyDestructive = true; else anyOther = true;
        TEST_ASSERT_EQUAL_MESSAGE(destructive, c["locked"].as<bool>(), c["name"].as<const char*>());
    }
    TEST_ASSERT_TRUE(anyDestructive);
    TEST_ASSERT_TRUE(anyOther);
}

// US-062: con permesso (STA autenticata) nessun comando e' locked
void test_commands_catalog_unlocked_when_allowed() {
    addTestAndShutdownUsages();
    JsonDocument doc;
    deserializeJson(doc, WebApiJson::generateUpsCommands(&mockHost, true));
    JsonArray commands = doc["commands"].as<JsonArray>();
    TEST_ASSERT_TRUE(commands.size() > 0);
    for (JsonObject c : commands) {
        TEST_ASSERT_TRUE_MESSAGE(c["locked"].is<bool>(), c["name"].as<const char*>());
        TEST_ASSERT_FALSE_MESSAGE(c["locked"].as<bool>(), c["name"].as<const char*>());
    }
}

// --- US-059: runUpsCommand ---

void test_run_command_ok() {
    addTestAndShutdownUsages();
    String response;
    int status = WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response, true);
    TEST_ASSERT_EQUAL(200, status);
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.Test", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, mockHost.writes[0].second);
}

void test_run_command_invalid_json() {
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "not json", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", response.c_str());
}

void test_run_command_missing_name() {
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "{}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", response.c_str());
}

void test_run_command_unknown() {
    addTestAndShutdownUsages();
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"foo\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Command not supported\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_destructive_forbidden() {
    addTestAndShutdownUsages();
    String response;
    TEST_ASSERT_EQUAL(403, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"shutdown.return\"}", response, false));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Available via NUT only in Access Point mode\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

// US-058: shutdown.default e i comandi con ritardo restano solo NUT, senza toccare l'host
void test_run_command_shutdown_default_and_delay_forbidden() {
    addTestAndShutdownUsages();
    mockHost.usages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeReboot"));
    const char* bodies[] = {
        "{\"name\":\"shutdown.default\"}",
        "{\"name\":\"load.off.delay\"}",
        "{\"name\":\"load.off.delay\",\"value\":\"30\"}",
    };
    for (const char* body : bodies) {
        String response;
        TEST_ASSERT_EQUAL_MESSAGE(403, WebApiJson::runUpsCommand(&mockHost, body, response, false), body);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("{\"error\":\"Available via NUT only in Access Point mode\"}", response.c_str(), body);
    }
    TEST_ASSERT_EQUAL(0, mockHost.executeCalls);
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_disconnected() {
    addTestAndShutdownUsages();
    mockHost.connected = false;
    String response;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_write_failed() {
    addTestAndShutdownUsages();
    mockHost.writeOk = false;
    String response;
    TEST_ASSERT_EQUAL(500, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Command rejected by the UPS\"}", response.c_str());
}

void test_run_command_not_supported_by_ups() {
    // Comando noto al catalogo ma senza l'usage Test nel descriptor
    mockHost.usages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeShutdown"));
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"test.battery.start.quick\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Command not supported\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_destructive_forbidden_null_host() {
    // Il 403 arriva prima del controllo sull'host
    String response;
    TEST_ASSERT_EQUAL(403, WebApiJson::runUpsCommand(nullptr, "{\"name\":\"load.off\"}", response, false));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Available via NUT only in Access Point mode\"}", response.c_str());
}

void test_run_command_body_too_long() {
    addTestAndShutdownUsages();
    std::string padded = "{\"name\":\"test.battery.start.quick\",\"pad\":\"";
    padded.append(WebApiJson::MAX_COMMAND_BODY_LENGTH, 'x');
    padded += "\"}";
    String response;
    TEST_ASSERT_EQUAL(400, WebApiJson::runUpsCommand(&mockHost, String(padded.c_str()), response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"Invalid request\"}", response.c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_run_command_extra_fields_ignored() {
    addTestAndShutdownUsages();
    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"x\":[1,2,3],\"name\":\"test.battery.start.quick\"}", response, true));
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
}

void test_run_command_null_host() {
    String response;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsCommand(nullptr, "{\"name\":\"test.battery.start.quick\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", response.c_str());
}

// --- US-062: comandi distruttivi permessi (STA autenticata) ---

// Ritardi configurati come all'avvio del driver (ups.delay.*)
static void setConfiguredDelays() {
    mockHost.data.set("ups.delay.shutdown", "20");
    mockHost.data.set("ups.delay.start", "30");
}

void test_run_command_shutdown_return_allowed() {
    addTestAndShutdownUsages();
    setConfiguredDelays();
    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"shutdown.return\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(1, mockHost.executeCalls);
    TEST_ASSERT_EQUAL(2, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeStartup", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(30, mockHost.writes[0].second);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeShutdown", mockHost.writes[1].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(20, mockHost.writes[1].second);
}

void test_run_command_load_off_delay_allowed_uses_configured_delay() {
    addTestAndShutdownUsages();
    setConfiguredDelays();
    String response;
    // "value" nel body e' ignorato: a executeCommand non arriva alcun parametro
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"load.off.delay\",\"value\":\"5\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeShutdown", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(20, mockHost.writes[0].second);
}

void test_run_command_shutdown_default_allowed() {
    addTestAndShutdownUsages();
    setConfiguredDelays();
    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&mockHost, "{\"name\":\"shutdown.default\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"success\":true}", response.c_str());
    TEST_ASSERT_EQUAL(2, mockHost.executeCalls);  // shutdown.default, poi shutdown.return
    TEST_ASSERT_TRUE(mockHost.writes.size() > 0);
    TEST_ASSERT_EQUAL_STRING("shutdown.return", mockHost.lastDefaultShutdown().c_str());
}

void test_run_command_destructive_allowed_null_host() {
    String response;
    TEST_ASSERT_EQUAL(503, WebApiJson::runUpsCommand(nullptr, "{\"name\":\"load.off\"}", response, true));
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"UPS not connected\"}", response.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_api_null_host);
    RUN_TEST(test_api_disconnected_host);
    RUN_TEST(test_api_connected_host_with_data);
    RUN_TEST(test_commands_null_host);
    RUN_TEST(test_commands_disconnected_host);
    RUN_TEST(test_commands_catalog);
    RUN_TEST(test_commands_catalog_locked_when_not_allowed);
    RUN_TEST(test_commands_catalog_unlocked_when_allowed);
    RUN_TEST(test_run_command_ok);
    RUN_TEST(test_run_command_invalid_json);
    RUN_TEST(test_run_command_missing_name);
    RUN_TEST(test_run_command_unknown);
    RUN_TEST(test_run_command_destructive_forbidden);
    RUN_TEST(test_run_command_shutdown_default_and_delay_forbidden);
    RUN_TEST(test_run_command_disconnected);
    RUN_TEST(test_run_command_write_failed);
    RUN_TEST(test_run_command_null_host);
    RUN_TEST(test_run_command_not_supported_by_ups);
    RUN_TEST(test_run_command_destructive_forbidden_null_host);
    RUN_TEST(test_run_command_body_too_long);
    RUN_TEST(test_run_command_extra_fields_ignored);
    RUN_TEST(test_run_command_shutdown_return_allowed);
    RUN_TEST(test_run_command_load_off_delay_allowed_uses_configured_delay);
    RUN_TEST(test_run_command_shutdown_default_allowed);
    RUN_TEST(test_run_command_destructive_allowed_null_host);
    return UNITY_END();
}
