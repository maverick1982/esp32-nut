// US-103: the NUT server and the web API work with a source that implements only
// IUpsSource, and their headers do not bring in any HID type.
#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "NUTServer.h"
#include "WebApiJson.h"

// The consumer headers must stay free of HID types (ADR 0010 step 4)
#ifdef HID_PARSER_H
#error "HIDParser.h included by the consumer headers (US-103)"
#endif
#ifdef HID_USAGES_H
#error "HIDUsages.h included by the consumer headers (US-103)"
#endif
#ifdef NUT_TABLE_H
#error "NutTable.h included by the consumer headers (US-103)"
#endif
#ifdef COMMAND_CATALOG_H
#error "CommandCatalog.h included by the consumer headers (US-103)"
#endif
#ifdef I_USB_HOST_UPS_H
#error "IUSBHostUPS.h included by the consumer headers (US-103)"
#endif

class MemoryPrinter : public Print {
public:
    std::string buffer;
    size_t write(uint8_t c) override {
        buffer += (char)c;
        return 1;
    }
    size_t write(const uint8_t* b, size_t size) override {
        if (b && size > 0) buffer.append((const char*)b, size);
        return size;
    }
};

// A UPS source with no HID behind it, as a Q* driver would be
class FakeSource : public IUpsSource {
public:
    UPSData data;
    std::vector<std::string> commandsRun;
    std::vector<std::pair<std::string, std::string>> sets;
    std::vector<std::string> resets;
    // Data lock depth, and its value at each call that talks to the device: commands and
    // SET VAR run with no data lock held (ADR 0008)
    mutable int depth = 0;
    std::vector<int> depthAtDeviceCall;

    void lock() const override { depth++; }
    void unlock() const override { depth--; }
    UPSDataLock getUPSData() const override { return UPSDataLock(data, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool isConnected() const override { return true; }

    std::vector<const UPSCommandInfo*> getSupportedCommands() const override {
        return { CommandCatalog::find("beeper.enable"), CommandCatalog::find("shutdown.stop") };
    }
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        for (const auto* c : getSupportedCommands()) {
            if (strcmp(c->name, name) == 0) {
                depthAtDeviceCall.push_back(depth);
                commandsRun.push_back(name);
                return CommandResult::OK;
            }
        }
        return CommandResult::NOT_SUPPORTED;
    }
    std::vector<NutTable::RWVariable> getWritableVariables() const override {
        return { { "battery.charge.low", 10, {}, false }, { "ups.delay.shutdown", 10, {}, true } };
    }
    NutTable::SetStatus setVariable(const char* name, const char* value) override {
        depthAtDeviceCall.push_back(depth);
        sets.push_back({ name, value });
        data.set(name, value);
        return NutTable::SetStatus::OK;
    }
    NutTable::SetStatus resetVariable(const char* name) override {
        depthAtDeviceCall.push_back(depth);
        resets.push_back(name);
        data.set(name, "20");
        return NutTable::SetStatus::OK;
    }
};

static FakeSource source;
static NUTServer server;
static MemoryPrinter printer;

static std::string request(const char* line) {
    printer.buffer.clear();
    server.processCommand(printer, 0, line);
    return printer.buffer;
}

void setUp(void) {
    source.data = UPSData();
    source.data.set("battery.charge", "87");
    source.data.set("battery.charge.low", "20");
    source.data.set("ups.beeper.status", "enabled");
    source.data.set("ups.delay.shutdown", "60");
    source.commandsRun.clear();
    source.sets.clear();
    source.resets.clear();
    source.depth = 0;
    source.depthAtDeviceCall.clear();

    NUTServerConfig config;
    config.username = "admin";
    config.password = "secret";
    config.ups_name = "testups";
    server.resetSessions();
    server.clearTracking();
    server.begin(config, &source, 3493);
    server.setAuthenticated(0, true);
    server.setClientUsername(0, "admin");
}

void tearDown(void) {
    // Every data lock taken by the server and the web API was released, and no call that
    // talks to the device ran under it
    TEST_ASSERT_EQUAL(0, source.depth);
    for (int d : source.depthAtDeviceCall) TEST_ASSERT_EQUAL(0, d);
}

void test_nut_get_and_list_var(void) {
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.charge \"87\"\n", request("GET VAR testups battery.charge").c_str());
    std::string out = request("LIST VAR testups");
    TEST_ASSERT_TRUE(out.find("BEGIN LIST VAR testups\n") == 0);
    TEST_ASSERT_TRUE(out.find("VAR testups battery.charge \"87\"\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("VAR testups ups.status \"OL\"\n") != std::string::npos);
}

void test_nut_list_cmd_and_instcmd(void) {
    std::string out = request("LIST CMD testups");
    TEST_ASSERT_TRUE(out.find("CMD testups beeper.enable\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("CMD testups shutdown.stop\n") != std::string::npos);

    TEST_ASSERT_EQUAL_STRING("OK\n", request("INSTCMD testups shutdown.stop").c_str());
    TEST_ASSERT_EQUAL(1, (int)source.commandsRun.size());
    TEST_ASSERT_EQUAL_STRING("shutdown.stop", source.commandsRun[0].c_str());
    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", request("INSTCMD testups load.off").c_str());
}

void test_nut_list_rw_and_set_var(void) {
    std::string out = request("LIST RW testups");
    TEST_ASSERT_TRUE(out.find("RW testups battery.charge.low \"20\"\n") != std::string::npos);

    TEST_ASSERT_EQUAL_STRING("OK\n", request("SET VAR testups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL(1, (int)source.sets.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", source.sets[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("30", source.sets[0].second.c_str());
}

void test_web_ups_vars(void) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, WebApiJson::generateUpsVars(&source)));
    TEST_ASSERT_EQUAL(87, doc["battery.charge"].as<int>());
    TEST_ASSERT_EQUAL_STRING("OL", doc["ups.status"].as<const char*>());
}

void test_web_commands(void) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, WebApiJson::generateUpsCommands(&source)));
    TEST_ASSERT_TRUE(doc["connected"].as<bool>());
    TEST_ASSERT_EQUAL(2, doc["commands"].size());

    String response;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsCommand(&source, "{\"name\":\"beeper.enable\"}", response));
    TEST_ASSERT_EQUAL(1, (int)source.commandsRun.size());
    TEST_ASSERT_EQUAL_STRING("beeper.enable", source.commandsRun[0].c_str());
}

void test_web_variables(void) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, WebApiJson::generateUpsVariables(&source)));
    TEST_ASSERT_TRUE(doc["connected"].as<bool>());
    JsonArray vars = doc["variables"].as<JsonArray>();
    TEST_ASSERT_EQUAL(2, vars.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", vars[0]["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("20", vars[0]["value"].as<const char*>());
}

void test_web_set_and_reset_variable(void) {
    String response;
    bool host_called = false;
    TEST_ASSERT_EQUAL(200, WebApiJson::runUpsVariable(&source, "{\"name\":\"battery.charge.low\",\"value\":\"35\"}",
                                                      response, &host_called));
    TEST_ASSERT_TRUE(host_called);
    TEST_ASSERT_EQUAL(1, (int)source.sets.size());
    TEST_ASSERT_EQUAL_STRING("35", source.sets[0].second.c_str());

    host_called = false;
    TEST_ASSERT_EQUAL(200, WebApiJson::resetUpsVariable(&source, "{\"name\":\"ups.delay.shutdown\"}", response,
                                                        &host_called));
    TEST_ASSERT_TRUE(host_called);
    TEST_ASSERT_EQUAL(1, (int)source.resets.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", source.resets[0].c_str());

    // Only driver-side variables can be reset
    TEST_ASSERT_EQUAL(400, WebApiJson::resetUpsVariable(&source, "{\"name\":\"battery.charge.low\"}", response));
}

// The #error checks above see only the headers; the sources of the consumers must not
// include the HID headers either (US-103)
static std::string projectPath(const char* rel) {
    std::string self = __FILE__;
    for (char& c : self) if (c == '\\') c = '/';
    size_t cut = self.rfind("test/test_ups_source/");
    return (cut == std::string::npos ? std::string() : self.substr(0, cut)) + rel;
}

void test_consumer_sources_include_no_hid_header(void) {
    const char* files[] = {
        "lib/NUTServer/src/NUTServer.h", "lib/NUTServer/src/NUTServer.cpp", "lib/NUTServer/src/NutTracking.h",
        "lib/WebApi/src/WebApiJson.h", "lib/WebApi/src/WebApiJson.cpp",
    };
    const char* forbidden[] = {
        "#include \"IUSBHostUPS.h\"", "#include \"HIDParser.h\"", "#include \"HIDUsages.h\"",
        "#include \"NutTable.h\"", "#include \"CommandCatalog.h\"",
    };
    for (const char* f : files) {
        std::ifstream in(projectPath(f));
        TEST_ASSERT_TRUE_MESSAGE(in.good(), f);
        std::stringstream text;
        text << in.rdbuf();
        for (const char* inc : forbidden) {
            TEST_ASSERT_TRUE_MESSAGE(text.str().find(inc) == std::string::npos, (std::string(f) + ": " + inc).c_str());
        }
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_nut_get_and_list_var);
    RUN_TEST(test_nut_list_cmd_and_instcmd);
    RUN_TEST(test_nut_list_rw_and_set_var);
    RUN_TEST(test_web_ups_vars);
    RUN_TEST(test_web_commands);
    RUN_TEST(test_web_variables);
    RUN_TEST(test_web_set_and_reset_variable);
    RUN_TEST(test_consumer_sources_include_no_hid_header);
    return UNITY_END();
}
