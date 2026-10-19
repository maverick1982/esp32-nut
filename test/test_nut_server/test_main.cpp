#include <unity.h>
#include "NUTServer.h"
#include "IUSBHostUPS.h"
#include "BeeperLogic.h"
#include <sstream>
#include <algorithm>

// Mock per catturare l'output di Print
class MemoryPrinter : public Print {
public:
    std::string buffer;

    size_t write(uint8_t c) override {
        buffer += (char)c;
        return 1;
    }

    size_t write(const uint8_t *b, size_t size) override {
        if (b && size > 0) {
            buffer.append((const char*)b, size);
        }
        return size;
    }

    void clear() {
        buffer.clear();
    }

    std::string getOutput() const {
        return buffer;
    }
};

// Mock di IUSBHostUPS
class MockUSBHost : public IUSBHostUPS {
public:
    UPSData data;
    String statusString = "OL";
    bool beeperState = true;
    bool beeperToggleSupported = true;
    bool connected = true;

    void end() override {}

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override {
        return UPSDataLock(data, this);
    }

    String getUPSStatusString() const override {
        return statusString;
    }

    bool setBeeperOk = true;
    bool setBeeper(bool enable) override {
        if (!setBeeperOk) return false;
        beeperState = enable;
        data.set("ups.beeper.status", enable ? "enabled" : "disabled");
        return true;
    }

    // Writes requested by executeCommand(): {path, value}, failed ones included.
    // failPaths: fields whose write fails (US-058)
    bool writeOk = true;
    std::vector<std::string> failPaths;
    std::vector<std::pair<std::string, uint32_t>> writes;
    bool writeUsage(const HIDUsageDef& def, uint32_t value) override {
        writes.push_back({def.path, value});
        if (std::find(failPaths.begin(), failPaths.end(), def.path) != failPaths.end()) return false;
        return writeOk;
    }

    bool isConnected() const override {
        return connected;
    }

    bool stale = false;
    bool isDataStale() const override { return stale; }

    // Writable variables reported to LIST RW / GET TYPE / LIST ENUM (US-066)
    std::vector<NutTable::RWVariable> rw;
    std::vector<NutTable::RWVariable> getWritableVariables() const override { return rw; }
    bool supportsBeeperToggle() const override { return beeperToggleSupported; }

    // SET VAR requests that passed the checks of the server, {name, value}, and the
    // outcome returned for them (US-067)
    NutTable::SetStatus setResult = NutTable::SetStatus::OK;
    std::vector<std::pair<std::string, std::string>> sets;
    // Opt-in: an OK write lands in data, as the real host does after the read-back
    bool storeOnSet = false;
    NutTable::SetStatus setVariable(const char* name, const char* value) override {
        sets.push_back({name, value});
        if (storeOnSet && setResult == NutTable::SetStatus::OK) data.set(name, value);
        return setResult;
    }

    std::vector<HIDUsageDef> _mockUsages;
    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _mockUsages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

static NUTServer server;
static MockUSBHost mockHost;
static MemoryPrinter printer;

// System Log lines of the server, clock and random source of TRACKING (US-067)
static std::vector<std::string> logLines;
static void captureLog(const char* level, const char* msg) {
    logLines.push_back(std::string(level) + " " + msg);
}
static uint32_t fakeNow = 0;
static uint32_t fakeClock() { return fakeNow; }
static uint32_t fakeSeed = 1;
static uint32_t fakeRandom() {
    fakeSeed = fakeSeed * 1664525u + 1013904223u;
    return fakeSeed;
}

void setUp(void) {
    logLines.clear();
    fakeNow = 1000;
    server.setLogCallback(captureLog);
    server.setClock(fakeClock);
    server.setRandom(fakeRandom);
    server.clearTracking();
    server.resetSessions();
    printer.clear();
    mockHost.data = UPSData();
    mockHost.statusString = "OL";
    mockHost.beeperState = true;
    mockHost.beeperToggleSupported = true;
    mockHost.connected = true;
    mockHost.stale = false;
    mockHost._mockUsages.clear();
    mockHost.setBeeperOk = true;
    mockHost.writeOk = true;
    mockHost.writes.clear();
    mockHost.failPaths.clear();
    mockHost.rw.clear();
    mockHost.setResult = NutTable::SetStatus::OK;
    mockHost.sets.clear();
    mockHost.storeOnSet = false;

    NUTServerConfig config;
    config.username = "admin";
    config.password = "secret";
    config.ups_name = "testups";

    server.begin(config, &mockHost, 3493);
}

void tearDown(void) {}

void test_split_tokens(void) {
    std::vector<String> tokens1 = NUTServer::splitTokens("GET VAR testups battery.charge");
    TEST_ASSERT_EQUAL(4, tokens1.size());
    TEST_ASSERT_EQUAL_STRING("GET", tokens1[0].c_str());
    TEST_ASSERT_EQUAL_STRING("VAR", tokens1[1].c_str());
    TEST_ASSERT_EQUAL_STRING("testups", tokens1[2].c_str());
    TEST_ASSERT_EQUAL_STRING("battery.charge", tokens1[3].c_str());

    std::vector<String> tokens2 = NUTServer::splitTokens("GET VAR testups \"battery.charge\"");
    TEST_ASSERT_EQUAL(4, tokens2.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge", tokens2[3].c_str());
}

void test_auth_flow(void) {
    // 1. Username
    server.processCommand(printer, 0, "USERNAME admin");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    printer.clear();

    // 2. Wrong Password
    server.processCommand(printer, 0, "PASSWORD wrong");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", printer.getOutput().c_str());
    printer.clear();

    // 3. Correct Password
    server.processCommand(printer, 0, "PASSWORD secret");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    printer.clear();

    // 4. Login
    server.processCommand(printer, 0, "LOGIN testups");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    printer.clear();

    // 5. Login wrong ups
    server.processCommand(printer, 0, "LOGIN wrongups");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());
    printer.clear();
}

void test_list_ups(void) {
    server.processCommand(printer, 0, "LIST UPS");
    std::string out = printer.getOutput();
    TEST_ASSERT_TRUE(out.find("BEGIN LIST UPS\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("UPS testups \"ESP32-S3 UPS Bridge\"\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("END LIST UPS\n") != std::string::npos);
}

void test_get_var_compliance(void) {
    server.setAuthenticated(0, true);

    mockHost.data.set("battery.voltage", "13.6");
    mockHost.data.set("battery.temperature", "28.5");
    mockHost.data.set("battery.charge", "95");
    mockHost.data.set("battery.capacity", "100");
    mockHost.data.set("battery.capacity.full", "100");
    mockHost.data.set("battery.mfr.date", "2024/05/23");
    mockHost.data.set("ups.mfr.date", "2006/09/15");
    mockHost.data.set("battery.date", "2025/01/10");
    mockHost.data.set("output.voltage", "230.0");
    mockHost.data.set("ups.mfr", "APC");

    // Supported var: battery.voltage
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.voltage");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.voltage \"13.6\"\n", printer.getOutput().c_str());

    // Supported var: battery.mfr.date
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.mfr.date");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.mfr.date \"2024/05/23\"\n", printer.getOutput().c_str());

    // Supported var: ups.mfr.date
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups ups.mfr.date");
    TEST_ASSERT_EQUAL_STRING("VAR testups ups.mfr.date \"2006/09/15\"\n", printer.getOutput().c_str());

    // Supported var: battery.date
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.date");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.date \"2025/01/10\"\n", printer.getOutput().c_str());

    // Supported var: battery.temperature
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.temperature");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.temperature \"28.5\"\n", printer.getOutput().c_str());

    // Supported var: battery.charge
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.charge");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.charge \"95\"\n", printer.getOutput().c_str());

    // Supported var: battery.capacity.full
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.capacity.full");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.capacity.full \"100\"\n", printer.getOutput().c_str());

    // Supported var: output.voltage
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups output.voltage");
    TEST_ASSERT_EQUAL_STRING("VAR testups output.voltage \"230.0\"\n", printer.getOutput().c_str());

    // Supported var: ups.mfr
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups ups.mfr");
    TEST_ASSERT_EQUAL_STRING("VAR testups ups.mfr \"APC\"\n", printer.getOutput().c_str());

    // Unsupported var (has flag is false by default, e.g. input.voltage)
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups input.voltage");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", printer.getOutput().c_str());
}

void test_instcmd_beeper(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("ups.beeper.status", "enabled");

    // Toggle beeper
    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups beeper.disable");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_FALSE(mockHost.beeperState);

    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups beeper.enable");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_TRUE(mockHost.beeperState);

    // Invalid command
    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups invalid.cmd");
    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", printer.getOutput().c_str());
}

// A driver that ignores the beeper commands (e.g. EcoFlow) must not expose them,
// even though ups.beeper.status is still reported read-only.
void test_beeper_hidden_when_not_controllable(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("ups.beeper.status", "enabled");
    mockHost.beeperToggleSupported = false;

    printer.clear();
    server.processCommand(printer, 0, "LIST CMD testups");
    TEST_ASSERT_TRUE(printer.getOutput().find("beeper") == std::string::npos);

    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups beeper.enable");
    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", printer.getOutput().c_str());
    TEST_ASSERT_TRUE(mockHost.beeperState); // unchanged
}

// Issue #47: frozen values must not be served as current (upsd behaviour)
void test_data_stale(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("battery.charge", "95");

    mockHost.stale = true;
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.charge");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups ups.status");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "LIST VAR testups");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());

    mockHost.stale = false;
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.charge");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.charge \"95\"\n", printer.getOutput().c_str());
}

// Review A1: with the UPS unplugged (or re-enumerating) the host reports stale data.
// Clients must get ERR DATA-STALE, never an empty "Unknown" status, while the UPS
// itself stays listed.
void test_disconnected_is_stale(void) {
    server.setAuthenticated(0, true);
    mockHost.data = UPSData();
    mockHost.statusString = "UNKNOWN";
    mockHost.connected = false;
    mockHost.stale = true;

    printer.clear();
    server.processCommand(printer, 0, "LIST VAR testups");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups ups.status");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "LIST UPS");
    TEST_ASSERT_TRUE(printer.getOutput().find("UPS testups ") != std::string::npos);
}

// Issue #60: OMV and Android clients read device.model, which NUT drivers copy from
// ups.model (main.c), with device.type "ups"
void test_device_aliases(void) {
    server.setAuthenticated(0, true);
    mockHost.data = UPSData();
    mockHost.data.set("ups.mfr", "American Power Conversion");
    mockHost.data.set("ups.model", "Back-UPS BX750MI");
    mockHost.stale = false;

    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups device.model");
    TEST_ASSERT_EQUAL_STRING("VAR testups device.model \"Back-UPS BX750MI\"\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups device.type");
    TEST_ASSERT_EQUAL_STRING("VAR testups device.type \"ups\"\n", printer.getOutput().c_str());
    // No ups.serial: no device.serial either
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups device.serial");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "LIST VAR testups");
    std::string out = printer.getOutput();
    TEST_ASSERT_TRUE(out.find("VAR testups device.type \"ups\"\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("VAR testups device.mfr \"American Power Conversion\"\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("VAR testups device.model \"Back-UPS BX750MI\"\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("device.serial") == std::string::npos);
    TEST_ASSERT_TRUE(out.find("VAR testups ups.model \"Back-UPS BX750MI\"\n") != std::string::npos);
}

// US-081: get_var() of upsd finds the variable ignoring case (state_tree_find(),
// strcasecmp) and repeats the name as the client sent it
void test_get_var_echoes_client_var_name(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("battery.charge", "100");
    mockHost.data.set("ups.model", "Back-UPS BX750MI");
    mockHost.statusString = "OL";

    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups Battery.Charge");
    TEST_ASSERT_EQUAL_STRING("VAR testups Battery.Charge \"100\"\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups Device.Model");
    TEST_ASSERT_EQUAL_STRING("VAR testups Device.Model \"Back-UPS BX750MI\"\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups UPS.Status");
    TEST_ASSERT_EQUAL_STRING("VAR testups UPS.Status \"OL\"\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups Device.Type");
    TEST_ASSERT_EQUAL_STRING("VAR testups Device.Type \"ups\"\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "GET VAR TestUPS Battery.Charge");
    TEST_ASSERT_EQUAL_STRING("VAR TestUPS Battery.Charge \"100\"\n", printer.getOutput().c_str());

    // Lower case: unchanged
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.charge");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.charge \"100\"\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups No.Such.Var");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", printer.getOutput().c_str());
    // Alias without its source (no ups.serial)
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups Device.Serial");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", printer.getOutput().c_str());
}

#ifdef PIO_UNIT_TESTING
void test_list_client_terminates(void) {
    // LIST CLIENT <ups> must be answered with a BEGIN/END pair. A bare "ERR" is what
    // breaks go.nut: it treats every "LIST " command as multi-line and only ends
    // its read loop on "END LIST ...", so a single error line blocks the client
    // until i/o timeout instead of failing fast. The same happens against upsd; go.nut
    // always sends the UPS name, so the ERR without it below does not reach it.
    server.processCommand(printer, 0, "LIST CLIENT testups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CLIENT testups\nEND LIST CLIENT testups\n",
                             printer.getOutput().c_str());

    // Without the UPS name INVALID-ARGUMENT, as net_list() of upsd (US-082): the UPS name
    // is never empty, so clients always send it
    printer.clear();
    server.processCommand(printer, 0, "LIST CLIENT");
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", printer.getOutput().c_str());
}

void test_list_cmd_unaffected_by_client(void) {
    // Regression: CLIENT shares a branch with CMD/RW, so the beeper listing must
    // stay behind the CMD guard and must not leak into CLIENT.
    mockHost.data.set("ups.beeper.status", "enabled");

    printer.clear();
    server.processCommand(printer, 0, "LIST CMD testups");
    std::string cmdOut = printer.getOutput();
    TEST_ASSERT_TRUE(cmdOut.find("BEGIN LIST CMD testups\n") != std::string::npos);
    TEST_ASSERT_TRUE(cmdOut.find("CMD testups beeper.enable\n") != std::string::npos);
    TEST_ASSERT_TRUE(cmdOut.find("END LIST CMD testups\n") != std::string::npos);

    printer.clear();
    server.processCommand(printer, 0, "LIST CLIENT testups");
    TEST_ASSERT_TRUE(printer.getOutput().find("beeper") == std::string::npos);
}

static HIDUsageDef featureUsage(const char* path, uint32_t usage) {
    HIDUsageDef d;
    d.usage = usage;
    d.report_type = 0x03;
    d.found = true;
    strncpy(d.path, path, sizeof(d.path) - 1);
    return d;
}

// US-056: LIST CMD publishes the commands derived from the HID descriptor
void test_list_cmd_from_descriptor(void) {
    mockHost.data.set("ups.beeper.status", "enabled");
    mockHost._mockUsages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeShutdown", 0x00840057));
    mockHost._mockUsages.push_back(featureUsage("UPS.PowerSummary.DelayBeforeStartup", 0x00840056));

    server.processCommand(printer, 0, "LIST CMD testups");
    std::string out = printer.getOutput();
    TEST_ASSERT_TRUE(out.find("BEGIN LIST CMD testups\n") == 0);
    TEST_ASSERT_TRUE(out.find("CMD testups beeper.toggle\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("CMD testups shutdown.return\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("CMD testups load.off\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("test.battery") == std::string::npos);
    TEST_ASSERT_TRUE(out.find("END LIST CMD testups\n") != std::string::npos);
}

void test_list_cmd_empty_when_disconnected(void) {
    mockHost.data.set("ups.beeper.status", "enabled");
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));
    mockHost.connected = false;

    server.processCommand(printer, 0, "LIST CMD testups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CMD testups\nEND LIST CMD testups\n", printer.getOutput().c_str());
}

// Beeper not controllable (e.g. EcoFlow): the other commands are still listed
void test_list_cmd_without_controllable_beeper(void) {
    mockHost.data.set("ups.beeper.status", "enabled");
    mockHost.beeperToggleSupported = false;
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));

    server.processCommand(printer, 0, "LIST CMD testups");
    std::string out = printer.getOutput();
    TEST_ASSERT_TRUE(out.find("beeper") == std::string::npos);
    TEST_ASSERT_TRUE(out.find("CMD testups test.battery.start.quick\n") != std::string::npos);
}

void test_cmddesc_known_and_unknown(void) {
    server.processCommand(printer, 0, "GET CMDDESC testups test.battery.start.quick");
    TEST_ASSERT_EQUAL_STRING("CMDDESC testups test.battery.start.quick \"Start a quick battery test\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET CMDDESC testups foo.bar");
    // upsd's fallback text (netget.c:130), US-066
    TEST_ASSERT_EQUAL_STRING("CMDDESC testups foo.bar \"Description unavailable\"\n", printer.getOutput().c_str());
}

// US-057: test commands go through executeCommand() and write the Test usage
void test_instcmd_battery_test_writes_value(void) {
    server.setAuthenticated(0, true);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));

    server.processCommand(printer, 0, "INSTCMD testups test.battery.start.quick");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.BatterySystem.Battery.Test", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, mockHost.writes[0].second);

    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups test.battery.stop");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL_UINT32(3, mockHost.writes.back().second);
}

void test_instcmd_not_in_catalog(void) {
    server.setAuthenticated(0, true);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));

    server.processCommand(printer, 0, "INSTCMD testups test.panel.start");
    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_instcmd_driver_not_connected(void) {
    server.setAuthenticated(0, true);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));
    mockHost.connected = false;

    server.processCommand(printer, 0, "INSTCMD testups test.battery.start.quick");
    TEST_ASSERT_EQUAL_STRING("ERR DRIVER-NOT-CONNECTED\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_instcmd_write_failed(void) {
    server.setAuthenticated(0, true);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));
    mockHost.writeOk = false;

    server.processCommand(printer, 0, "INSTCMD testups test.battery.start.deep");
    TEST_ASSERT_EQUAL_STRING("ERR INSTCMD-FAILED\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL_UINT32(2, mockHost.writes.back().second);
}

void test_instcmd_requires_login(void) {
    server.setClientUsername(0, "");
    server.setAuthenticated(0, false);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));

    // check_command(), FLAG_USER: USERNAME, then PASSWORD, before the arguments
    server.processCommand(printer, 0, "INSTCMD testups test.battery.start.quick");
    TEST_ASSERT_EQUAL_STRING("ERR USERNAME-REQUIRED\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "USERNAME admin");
    printer.clear();
    server.processCommand(printer, 0, "INSTCMD");
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", printer.getOutput().c_str());
    printer.clear();
    // Sent but wrong: user_checkinstcmd()
    server.processCommand(printer, 0, "PASSWORD wrong");
    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups test.battery.start.quick");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

static std::string request(int slot, const char* line);

// LOGIN is FLAG_USER too; net_login() checks the UPS name, then the credentials
void test_login_requires_credentials(void) {
    server.setClientUsername(0, "");
    server.setAuthenticated(0, false);
    TEST_ASSERT_EQUAL_STRING("ERR USERNAME-REQUIRED\n", request(0, "LOGIN testups").c_str());
    request(0, "USERNAME admin");
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", request(0, "LOGIN wrongups").c_str());
    request(0, "PASSWORD wrong");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", request(0, "LOGIN wrongups").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", request(0, "LOGIN testups").c_str());
    request(0, "PASSWORD secret");
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "LOGIN testups").c_str());

    // Without NUT credentials configured nothing is required, as before
    NUTServer open;
    NUTServerConfig config;
    config.ups_name = "testups";
    open.begin(config, &mockHost, 3493);
    printer.clear();
    open.processCommand(printer, 0, "LOGIN testups");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
}

// US-058: delay fields of the mock, 16 bits wide like the Eaton 3S
#define P_SD "UPS.PowerSummary.DelayBeforeShutdown"
#define P_ST "UPS.PowerSummary.DelayBeforeStartup"
#define P_RB "UPS.PowerSummary.DelayBeforeReboot"

static void addDelayUsages(bool shutdown, bool startup, bool reboot) {
    auto add = [](const char* path, uint32_t usage) {
        HIDUsageDef d = featureUsage(path, usage);
        d.report_id = 0x11;
        d.bit_size = 16;
        mockHost._mockUsages.push_back(d);
    };
    if (shutdown) add(P_SD, 0x00840057);
    if (startup) add(P_ST, 0x00840056);
    if (reboot) add(P_RB, 0x00840055);
    // As USBHostUPS at connection: ups.delay.* with the NUT defaults
    CommandCatalog::applyDefaultDelays(mockHost.data, mockHost._mockUsages, 20, 30, true);
}

// Writes recorded by the mock as "path=value" joined by spaces, values as signed
static std::string writesStr() {
    std::string out;
    for (const auto& w : mockHost.writes) {
        if (!out.empty()) out += " ";
        out += w.first + "=" + std::to_string((int32_t)w.second);
    }
    return out;
}

static std::string instcmd(const char* line) {
    printer.clear();
    mockHost.writes.clear();
    server.processCommand(printer, 0, line);
    return printer.getOutput();
}

// US-058: each load.* / shutdown.* command writes the usbhid-ups sequence, in order.
// ups.delay.* are not set in the mock data: the NUT defaults 20/30 apply.
void test_instcmd_shutdown_sequences(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, true);
    struct Case { const char* line; const char* writes; };
    const Case cases[] = {
        { "INSTCMD testups load.off",            P_SD "=0" },
        { "INSTCMD testups load.on",             P_ST "=0" },
        { "INSTCMD testups load.off.delay",      P_SD "=20" },
        { "INSTCMD testups load.off.delay 30",   P_SD "=30" },
        { "INSTCMD testups load.on.delay",       P_ST "=30" },
        { "INSTCMD testups load.on.delay 5",     P_ST "=5" },
        { "INSTCMD testups shutdown.return",     P_ST "=30 " P_SD "=20" },
        { "INSTCMD testups shutdown.stayoff",    P_ST "=-1 " P_SD "=20" },
        { "INSTCMD testups shutdown.stop",       P_SD "=-1" },
        { "INSTCMD testups shutdown.reboot",     P_RB "=10" },
        { "INSTCMD testups shutdown.reboot 5",   P_RB "=5" },
        // The value is ignored by the commands that take none
        { "INSTCMD testups load.off 99",         P_SD "=0" },
    };
    for (const auto& c : cases) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("OK\n", instcmd(c.line).c_str(), c.line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.writes, writesStr().c_str(), c.line);
    }
}

// -1 reaches writeUsage() as (uint32_t)-1 and becomes the field's two's complement
void test_instcmd_stayoff_writes_minus_one_as_twos_complement(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, false);

    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.stayoff").c_str());
    TEST_ASSERT_EQUAL(2, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING(P_ST, mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, mockHost.writes[0].second);

    // What USBHostUPS::writeUsageLocked() puts in the report: 16-bit field = 0xFFFF
    const HIDUsageDef& st = mockHost._mockUsages[1];
    uint8_t report[4] = { 0x11, 0x00, 0x00, 0xAA };
    TEST_ASSERT_EQUAL(3, BeeperLogic::writeField(st, mockHost.writes[0].second, report, 3));
    TEST_ASSERT_EQUAL_HEX8(0x11, report[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, report[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, report[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, report[3]);
}

// The configured ups.delay.* (set at connection) drive the sequences
void test_instcmd_uses_configured_delays(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, false);
    mockHost.data.set("ups.delay.shutdown", "60");
    mockHost.data.set("ups.delay.start", "120");

    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.return").c_str());
    TEST_ASSERT_EQUAL_STRING(P_ST "=120 " P_SD "=60", writesStr().c_str());
}

// Without configured ups.delay.* nothing is guessed: no write, the command is refused
void test_instcmd_without_configured_delays(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, false);
    mockHost.data.remove("ups.delay.shutdown");
    mockHost.data.set("ups.delay.start", "abc");

    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", instcmd("INSTCMD testups shutdown.return").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", instcmd("INSTCMD testups load.off.delay").c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups load.off.delay 45").c_str());
    TEST_ASSERT_EQUAL_STRING(P_SD "=45", writesStr().c_str());
}

// 65536 on a 16-bit field would be truncated to 0, an immediate shutdown
void test_instcmd_param_too_large_for_field(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, false);
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", instcmd("INSTCMD testups load.off.delay 65536").c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_instcmd_invalid_argument(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, true);

    const char* lines[] = {
        "INSTCMD testups load.off.delay abc",
        "INSTCMD testups load.on.delay -5",
        "INSTCMD testups shutdown.reboot 12x",
    };
    for (const char* line : lines) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ERR INVALID-ARGUMENT\n", instcmd(line).c_str(), line);
        TEST_ASSERT_EQUAL_MESSAGE(0, mockHost.writes.size(), line);
    }
}

// The second write of a sequence is not sent when the first fails
void test_instcmd_sequence_stops_at_first_failed_write(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, false);
    mockHost.failPaths.push_back(P_ST);

    TEST_ASSERT_EQUAL_STRING("ERR INSTCMD-FAILED\n", instcmd("INSTCMD testups shutdown.return").c_str());
    TEST_ASSERT_EQUAL_STRING(P_ST "=30", writesStr().c_str());
}

void test_instcmd_shutdown_requires_login(void) {
    server.setClientUsername(0, "admin");
    server.setAuthenticated(0, false);
    addDelayUsages(true, true, true);

    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", instcmd("INSTCMD testups shutdown.return").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", instcmd("INSTCMD testups load.off.delay 30").c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

// shutdown.default tries shutdown.return, shutdown.reboot, load.off.delay, shutdown.stayoff
// (upsdrv_shutdown() order) and stops at the first that succeeds
void test_instcmd_shutdown_default_fallback(void) {
    server.setAuthenticated(0, true);

    // All groups: shutdown.return
    addDelayUsages(true, true, true);
    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL_STRING(P_ST "=30 " P_SD "=20", writesStr().c_str());
    TEST_ASSERT_EQUAL_STRING("shutdown.return", mockHost.lastDefaultShutdown().c_str());

    // shutdown.return fails on its first write: shutdown.reboot
    mockHost.failPaths = { P_ST };
    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL_STRING(P_ST "=30 " P_RB "=10", writesStr().c_str());
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot", mockHost.lastDefaultShutdown().c_str());

    // No Startup (shutdown.return not supported): shutdown.reboot
    mockHost.failPaths.clear();
    mockHost._mockUsages.clear();
    addDelayUsages(true, false, true);
    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL_STRING(P_RB "=10", writesStr().c_str());
    TEST_ASSERT_EQUAL_STRING("shutdown.reboot", mockHost.lastDefaultShutdown().c_str());

    // Reboot fails and no Startup: load.off.delay
    mockHost.failPaths = { P_RB };
    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL_STRING(P_RB "=10 " P_SD "=20", writesStr().c_str());
    TEST_ASSERT_EQUAL_STRING("load.off.delay", mockHost.lastDefaultShutdown().c_str());

    // The value of INSTCMD is not passed on to the fallback commands
    mockHost.failPaths.clear();
    TEST_ASSERT_EQUAL_STRING("OK\n", instcmd("INSTCMD testups shutdown.default 5").c_str());
    TEST_ASSERT_EQUAL_STRING(P_RB "=10", writesStr().c_str());
}

void test_instcmd_shutdown_default_all_fail(void) {
    server.setAuthenticated(0, true);
    addDelayUsages(true, true, true);
    mockHost.failPaths = { P_SD, P_ST, P_RB };

    TEST_ASSERT_EQUAL_STRING("ERR INSTCMD-FAILED\n", instcmd("INSTCMD testups shutdown.default").c_str());
    // return (Startup), reboot, load.off.delay, stayoff (Startup): one failed write each
    TEST_ASSERT_EQUAL_STRING(P_ST "=30 " P_RB "=10 " P_SD "=20 " P_ST "=-1", writesStr().c_str());
    TEST_ASSERT_EQUAL_STRING("", mockHost.lastDefaultShutdown().c_str());
}

void test_instcmd_shutdown_default_without_groups(void) {
    server.setAuthenticated(0, true);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));

    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
}

void test_list_cmd_includes_shutdown_default(void) {
    addDelayUsages(false, false, true);
    std::string out = instcmd("LIST CMD testups");
    TEST_ASSERT_TRUE(out.find("CMD testups shutdown.reboot\n") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("CMD testups shutdown.default\n") != std::string::npos);

    mockHost._mockUsages.clear();
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));
    out = instcmd("LIST CMD testups");
    TEST_ASSERT_TRUE(out.find("shutdown.default") == std::string::npos);
}

void test_instcmd_beeper_failure_and_toggle(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("ups.beeper.status", "enabled");

    server.processCommand(printer, 0, "INSTCMD testups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_FALSE(mockHost.beeperState);

    mockHost.setBeeperOk = false;
    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups beeper.enable");
    TEST_ASSERT_EQUAL_STRING("ERR INSTCMD-FAILED\n", printer.getOutput().c_str());
    TEST_ASSERT_FALSE(mockHost.beeperState);
}

// beeper.mute writes 3 on the beeper field; toggling a muted beeper disables it
void test_instcmd_beeper_mute_and_toggle_from_muted(void) {
    server.setAuthenticated(0, true);
    HIDUsageDef beeper = featureUsage("UPS.PowerSummary.AudibleAlarmControl", 0x0084005a);
    beeper.bit_size = 8;
    beeper.logical_min = 1;
    beeper.logical_max = 3;
    mockHost._mockUsages.push_back(beeper);
    mockHost.data.set("ups.beeper.status", "enabled");

    server.processCommand(printer, 0, "INSTCMD testups beeper.mute");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(1, mockHost.writes.size());
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.AudibleAlarmControl", mockHost.writes[0].first.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, mockHost.writes[0].second);

    mockHost.data.set("ups.beeper.status", "muted");
    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_FALSE(mockHost.beeperState);

    printer.clear();
    server.processCommand(printer, 0, "INSTCMD testups beeper.toggle");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_TRUE(mockHost.beeperState);
}

void test_get_upsdesc_and_numlogins(void) {
    server.processCommand(printer, 0, "GET UPSDESC testups");
    TEST_ASSERT_EQUAL_STRING("UPSDESC testups \"ESP32-S3 UPS Bridge\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET NUMLOGINS testups");
    TEST_ASSERT_EQUAL_STRING("NUMLOGINS testups 0\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET UPSDESC wrongups");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET NUMLOGINS wrongups");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());
}

// --- Writable variables (US-066): netlist.c list_rw/list_enum, netget.c get_type/get_desc ---

static void setupWritable() {
    mockHost.data.set("battery.charge.low", "20");
    mockHost.data.set("input.sensitivity", "normal");
    mockHost.rw = {
        { "battery.charge.low", 10, {} },
        { "input.sensitivity", 10, { "low", "normal", "high" } },
    };
}

void test_list_rw_with_values(void) {
    setupWritable();
    server.processCommand(printer, 0, "LIST RW testups");
    TEST_ASSERT_EQUAL_STRING(
        "BEGIN LIST RW testups\n"
        "RW testups battery.charge.low \"20\"\n"
        "RW testups input.sensitivity \"normal\"\n"
        "END LIST RW testups\n",
        printer.getOutput().c_str());
}

void test_list_rw_errors_and_empty(void) {
    setupWritable();
    server.processCommand(printer, 0, "LIST RW wrongups");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());

    printer.clear();
    mockHost.stale = true;
    server.processCommand(printer, 0, "LIST RW testups");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());

    printer.clear();
    mockHost.stale = false;
    mockHost.rw.clear();
    server.processCommand(printer, 0, "LIST RW testups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST RW testups\nEND LIST RW testups\n", printer.getOutput().c_str());
}

void test_get_type_of_writable_variables(void) {
    setupWritable();
    server.processCommand(printer, 0, "GET TYPE testups battery.charge.low");
    TEST_ASSERT_EQUAL_STRING("TYPE testups battery.charge.low RW STRING:10\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET TYPE testups input.sensitivity");
    TEST_ASSERT_EQUAL_STRING("TYPE testups input.sensitivity RW ENUM STRING:10\n", printer.getOutput().c_str());

    // Read-only variables keep the previous answer (US-071 will align them)
    printer.clear();
    mockHost.data.set("battery.charge", "100");
    server.processCommand(printer, 0, "GET TYPE testups battery.charge");
    TEST_ASSERT_EQUAL_STRING("TYPE testups battery.charge STRING:64\n", printer.getOutput().c_str());
}

void test_list_enum_and_range(void) {
    setupWritable();
    server.processCommand(printer, 0, "LIST ENUM testups input.sensitivity");
    TEST_ASSERT_EQUAL_STRING(
        "BEGIN LIST ENUM testups input.sensitivity\n"
        "ENUM testups input.sensitivity \"low\"\n"
        "ENUM testups input.sensitivity \"normal\"\n"
        "ENUM testups input.sensitivity \"high\"\n"
        "END LIST ENUM testups input.sensitivity\n",
        printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "LIST ENUM testups battery.charge.low");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST ENUM testups battery.charge.low\nEND LIST ENUM testups battery.charge.low\n",
                             printer.getOutput().c_str());

    // usbhid-ups adds no ranges
    printer.clear();
    server.processCommand(printer, 0, "LIST RANGE testups battery.charge.low");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST RANGE testups battery.charge.low\nEND LIST RANGE testups battery.charge.low\n",
                             printer.getOutput().c_str());

    // An unknown variable is an error, as in list_enum()
    printer.clear();
    server.processCommand(printer, 0, "LIST ENUM testups no.such.var");
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "LIST ENUM wrongups input.sensitivity");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());

    printer.clear();
    mockHost.stale = true;
    server.processCommand(printer, 0, "LIST RANGE testups battery.charge.low");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());
}

void test_get_desc_of_table_variables(void) {
    // Texts from nut_repo/data/cmdvartab
    server.processCommand(printer, 0, "GET DESC testups battery.charge.low");
    TEST_ASSERT_EQUAL_STRING("DESC testups battery.charge.low \"Remaining battery level when UPS switches to LB (percent)\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET DESC testups ups.delay.start");
    TEST_ASSERT_EQUAL_STRING("DESC testups ups.delay.start \"Interval to wait before (re)starting the load (seconds)\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET DESC testups no.such.var");
    TEST_ASSERT_EQUAL_STRING("DESC testups no.such.var \"Description unavailable\"\n", printer.getOutput().c_str());
}

void test_get_desc_and_type(void) {
    server.processCommand(printer, 0, "GET DESC testups ups.status");
    // Not a variable of the NUT table, whose descriptions are the only cmdvartab texts the
    // bridge carries so far: upsd's fallback text (netget.c:98). upsd with the full
    // cmdvartab answers "UPS status" (US-071).
    TEST_ASSERT_EQUAL_STRING("DESC testups ups.status \"Description unavailable\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET TYPE testups ups.status");
    std::string typeOut = printer.getOutput();
    TEST_ASSERT_EQUAL_STRING("TYPE testups ups.status STRING:64\n", typeOut.c_str());
    // Clients read the token after "RW" as the type, so a bare RW would crash
    // them. ups.status is not writable.
    TEST_ASSERT_TRUE(typeOut.find("RW") == std::string::npos);

    printer.clear();
    server.processCommand(printer, 0, "GET CMDDESC testups beeper.enable");
    TEST_ASSERT_EQUAL_STRING("CMDDESC testups beeper.enable \"Enable the UPS beeper\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET DESC wrongups ups.status");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET TYPE");
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", printer.getOutput().c_str());
}

void test_ver_and_netver(void) {
    // go.nut sends both on every connect. It discards their errors, so these
    // were never fatal -- but answering them keeps client.Version and
    // client.ProtocolVersion meaningful instead of empty.
    server.processCommand(printer, 0, "VER");
    std::string ver = printer.getOutput();
    TEST_ASSERT_EQUAL_STRING("Network UPS Tools esp32-nut dev\n", ver.c_str());

    printer.clear();
    server.processCommand(printer, 0, "NETVER");
    TEST_ASSERT_EQUAL_STRING("1.3\n", printer.getOutput().c_str());

    // Both must answer on a single line: go.nut reads exactly one line for a
    // command that is not a LIST.
    TEST_ASSERT_EQUAL(1, (int)std::count(ver.begin(), ver.end(), '\n'));
}

void test_gonut_newups_sequence(void) {
    // go.nut's NewUPS() issues exactly these five commands, in this order, and
    // aborts on the first failure. nut_exporter reaches LIST VAR only if every
    // earlier call succeeds, so the whole sequence is the acceptance condition.
    mockHost.data.set("ups.beeper.status", "enabled");
    mockHost.data.set("battery.voltage", "13.6");

    const char* sequence[] = {
        "LIST CLIENT testups",
        "LIST CMD testups",
        "GET UPSDESC testups",
        "GET NUMLOGINS testups",
        "LIST VAR testups",
        // GetVariables() then asks these two for every variable it just listed.
        "GET DESC testups battery.voltage",
        "GET TYPE testups battery.voltage",
        // GetCommands() likewise asks for a description of every command.
        "GET CMDDESC testups beeper.enable",
    };

    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
        printer.clear();
        server.processCommand(printer, 0, sequence[i]);
        std::string out = printer.getOutput();
        TEST_ASSERT_TRUE_MESSAGE(out.size() > 0, sequence[i]);
        TEST_ASSERT_TRUE_MESSAGE(out.find("ERR ") == std::string::npos, sequence[i]);
    }
}

// --- SET VAR, SET/GET TRACKING (US-067): netset.c set_var/net_set, netget.c net_get,
// upsd.c tracking_get/nut_uuid_v4, clients/upsrw.c do_set ---

static std::string request(int slot, const char* line) {
    printer.clear();
    server.processCommand(printer, slot, line);
    return printer.getOutput();
}

static void setupSettable() {
    server.setAuthenticated(0, true);
    server.setClientUsername(0, "admin");
    mockHost.data.set("battery.charge.low", "20");
    mockHost.data.set("input.sensitivity", "normal");
    mockHost.data.set("ups.load.high", "90");
    mockHost.data.set("outlet.desc", "PowerSummary");
    mockHost.data.set("battery.charge", "100"); // read-only
    mockHost.rw = {
        { "battery.charge.low", 10, {} },
        { "input.sensitivity", 10, { "low", "normal", "high" } },
        { "ups.load.high", 5, {} },
        { "outlet.desc", 20, {} },
    };
}

// nut_uuid_v4(): "%02X" x 16 in 8-4-4-4-12, version nibble 4, variant 10xx
static bool isUuid4(const std::string& id) {
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); i++) {
        char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return id[14] == '4' && strchr("89AB", id[19]) != nullptr;
}

// "OK TRACKING <id>\n" -> <id>, empty if the line has another shape
static std::string trackingId(const std::string& reply) {
    const std::string prefix = "OK TRACKING ";
    if (reply.compare(0, prefix.size(), prefix) != 0 || reply.empty() || reply.back() != '\n') return "";
    return reply.substr(prefix.size(), reply.size() - prefix.size() - 1);
}

void test_set_malformed_is_invalid_argument(void) {
    setupSettable();
    // net_set(): fewer than 2 arguments, unknown subcommand, SET VAR with fewer than 4
    const char* lines[] = {
        "SET",
        "SET FOO",
        "SET FOO bar",
        "SET TRACKING",
        "SET VAR testups battery.charge.low",
    };
    for (const char* line : lines) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ERR INVALID-ARGUMENT\n", request(0, line).c_str(), line);
    }
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_set_var_unknown_ups(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", request(0, "SET VAR wrongups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_set_var_driver_not_connected(void) {
    setupSettable();
    // Unplugged: the host is also stale, but ups_available() tests the driver socket first
    mockHost.connected = false;
    mockHost.stale = true;
    TEST_ASSERT_EQUAL_STRING("ERR DRIVER-NOT-CONNECTED\n", request(0, "SET VAR testups battery.charge.low 30").c_str());

    // No host at all
    NUTServer bare;
    NUTServerConfig config;
    config.ups_name = "testups";
    bare.begin(config, nullptr, 3493);
    printer.clear();
    bare.processCommand(printer, 0, "SET VAR testups battery.charge.low 30");
    TEST_ASSERT_EQUAL_STRING("ERR DRIVER-NOT-CONNECTED\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_set_var_data_stale(void) {
    setupSettable();
    mockHost.stale = true;
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", request(0, "SET VAR testups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_set_var_access_denied(void) {
    setupSettable();
    // check_command(), FLAG_USER: USERNAME first, then PASSWORD
    server.setClientUsername(0, "");
    server.setAuthenticated(0, false);
    TEST_ASSERT_EQUAL_STRING("ERR USERNAME-REQUIRED\n", request(0, "SET VAR testups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "USERNAME admin").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", request(0, "SET VAR testups battery.charge.low 30").c_str());
    // Sent but wrong: user_checkaction() in set_var()
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", request(0, "PASSWORD wrong").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", request(0, "SET VAR testups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "PASSWORD secret").c_str());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL(1, (int)mockHost.sets.size());
    mockHost.sets.clear();

    // Without NUT credentials SET is allowed, as INSTCMD
    NUTServer open;
    NUTServerConfig config;
    config.ups_name = "testups";
    open.begin(config, &mockHost, 3493);
    printer.clear();
    open.processCommand(printer, 0, "SET VAR testups battery.charge.low 30");
    TEST_ASSERT_EQUAL_STRING("OK\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(1, (int)mockHost.sets.size());
}

void test_set_var_not_supported_and_readonly(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", request(0, "SET VAR testups no.such.var 1").c_str());
    // A writable variable the UPS does not report is not supported (sstate_getinfo() first)
    mockHost.rw.push_back({ "battery.runtime.low", 10, {} });
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED\n", request(0, "SET VAR testups battery.runtime.low 120").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR READONLY\n", request(0, "SET VAR testups battery.charge 50").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR READONLY\n", request(0, "SET VAR testups ups.status OB").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR READONLY\n", request(0, "SET VAR testups device.type ups").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_set_var_aux_zero_is_set_failed(void) {
    setupSettable();
    // input.sensitivity of cps-hid is declared with aux 0: upsd refuses to write it
    mockHost.rw = { { "input.sensitivity", 0, { "low", "normal", "high" } } };
    TEST_ASSERT_EQUAL_STRING("ERR SET-FAILED\n", request(0, "SET VAR testups input.sensitivity high").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL(2, (int)logLines.size());
    TEST_ASSERT_EQUAL_STRING("WARN auxdata for input.sensitivity is invalid", logLines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("WARN SET input.sensitivity \"high\" from admin: ERR SET-FAILED", logLines[1].c_str());
}

void test_set_var_too_long(void) {
    setupSettable();
    // aux < strlen(newval)
    TEST_ASSERT_EQUAL_STRING("ERR TOO-LONG\n", request(0, "SET VAR testups ups.load.high 123456").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups ups.load.high 12345").c_str());
    TEST_ASSERT_EQUAL(1, (int)mockHost.sets.size());
}

void test_set_var_invalid_enum_value(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-VALUE\n", request(0, "SET VAR testups input.sensitivity medium").c_str());
    // strcmp: the case counts
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-VALUE\n", request(0, "SET VAR testups input.sensitivity High").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups input.sensitivity high").c_str());
    TEST_ASSERT_EQUAL(1, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("input.sensitivity", mockHost.sets[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("high", mockHost.sets[0].second.c_str());
}

// When a request fails more than one check, the error is the first one of set_var()
void test_set_var_check_order(void) {
    setupSettable();
    mockHost.connected = false;
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", request(0, "SET VAR wrongups battery.charge.low 30").c_str());

    // No PASSWORD sent: check_command() refuses before net_set() reads the arguments
    server.setAuthenticated(0, false);
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", request(0, "SET VAR wrongups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", request(0, "SET").c_str());

    // Wrong PASSWORD: ACCESS-DENIED comes after the UPS checks, as in set_var()
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", request(0, "PASSWORD wrong").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", request(0, "SET VAR wrongups battery.charge.low 30").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR DRIVER-NOT-CONNECTED\n", request(0, "SET VAR testups battery.charge.low 30").c_str());

    mockHost.connected = true;
    mockHost.stale = true;
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", request(0, "SET VAR testups no.such.var 30").c_str());

    mockHost.stale = false;
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", request(0, "SET VAR testups no.such.var 30").c_str());

    server.setAuthenticated(0, true);
    // Read-only and too long: READONLY
    TEST_ASSERT_EQUAL_STRING("ERR READONLY\n", request(0, "SET VAR testups battery.charge 12345678901").c_str());
    // Too long and outside the ENUM: TOO-LONG
    TEST_ASSERT_EQUAL_STRING("ERR TOO-LONG\n", request(0, "SET VAR testups input.sensitivity veryveryhigh").c_str());
    // aux 0 and too long: SET-FAILED
    mockHost.rw = { { "battery.charge.low", 0, {} } };
    TEST_ASSERT_EQUAL_STRING("ERR SET-FAILED\n", request(0, "SET VAR testups battery.charge.low 123456789012").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_set_var_canonical_name(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR TESTUPS BATTERY.Charge.Low 30").c_str());
    TEST_ASSERT_EQUAL(1, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("battery.charge.low", mockHost.sets[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("30", mockHost.sets[0].second.c_str());
}

// Values as upsrw sends them: always quoted, with pconf_encode() escaping " \ and #
void test_set_var_quoted_values(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups outlet.desc \"Rack A\"").c_str());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups outlet.desc \"say \\\"hi\\\" \\\\ \\#1\"").c_str());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups outlet.desc \"\"").c_str());
    TEST_ASSERT_EQUAL(3, (int)mockHost.sets.size());
    TEST_ASSERT_EQUAL_STRING("Rack A", mockHost.sets[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("say \"hi\" \\ #1", mockHost.sets[1].second.c_str());
    TEST_ASSERT_EQUAL_STRING("", mockHost.sets[2].second.c_str());
}

// Without TRACKING the reply waits for the write: OK only when it succeeded
void test_set_var_synchronous_outcome(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups battery.charge.low 30").c_str());

    const NutTable::SetStatus failures[] = {
        NutTable::SetStatus::FAILED,
        NutTable::SetStatus::CONVERSION_FAILED,
        NutTable::SetStatus::UNKNOWN,
        NutTable::SetStatus::NOT_CONNECTED,
    };
    for (NutTable::SetStatus st : failures) {
        mockHost.setResult = st;
        TEST_ASSERT_EQUAL_STRING("ERR SET-FAILED\n", request(0, "SET VAR testups battery.charge.low 30").c_str());
    }
    TEST_ASSERT_EQUAL(5, (int)mockHost.sets.size());
    // Valid requests are logged by the host, not by the server
    TEST_ASSERT_EQUAL(0, (int)logLines.size());
}

// upsrw -w: SET TRACKING ON, SET VAR, then GET TRACKING <id> until it is not PENDING
void test_upsrw_tracking_sequence(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET TRACKING ON").c_str());

    std::string reply = request(0, "SET VAR testups battery.charge.low \"25\"");
    // upsrw.c:149, on the line without its newline: strlen("OK TRACKING ") + UUID4_LEN - 1
    TEST_ASSERT_EQUAL(48, (int)reply.size() - 1);
    std::string id = trackingId(reply);
    TEST_ASSERT_TRUE_MESSAGE(isUuid4(id), reply.c_str());
    TEST_ASSERT_EQUAL(1, (int)mockHost.sets.size());

    std::string get = "GET TRACKING " + id;
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, get.c_str()).c_str());

    // strcasecmp in tracking_get()
    std::string lower = id;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    get = "GET TRACKING " + lower;
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, get.c_str()).c_str());
}

void test_tracking_outcomes(void) {
    setupSettable();
    request(0, "SET TRACKING ON");
    const struct {
        NutTable::SetStatus st;
        const char* expected;
    } cases[] = {
        { NutTable::SetStatus::OK,                "SUCCESS\n" },
        { NutTable::SetStatus::CONVERSION_FAILED, "ERR INVALID-ARGUMENT\n" },
        { NutTable::SetStatus::FAILED,            "ERR FAILED\n" },
        { NutTable::SetStatus::NOT_CONNECTED,     "ERR FAILED\n" },
        { NutTable::SetStatus::UNKNOWN,           "ERR UNKNOWN\n" },
    };
    for (const auto& c : cases) {
        mockHost.setResult = c.st;
        std::string id = trackingId(request(0, "SET VAR testups battery.charge.low 30"));
        TEST_ASSERT_TRUE(isUuid4(id));
        std::string get = "GET TRACKING " + id;
        TEST_ASSERT_EQUAL_STRING(c.expected, request(0, get.c_str()).c_str());
    }
}

// Validation errors stay immediate with TRACKING on: no id
void test_tracking_validation_error_is_immediate(void) {
    setupSettable();
    request(0, "SET TRACKING ON");
    TEST_ASSERT_EQUAL_STRING("ERR READONLY\n", request(0, "SET VAR testups battery.charge 50").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-VALUE\n", request(0, "SET VAR testups input.sensitivity medium").c_str());
    TEST_ASSERT_EQUAL(0, (int)mockHost.sets.size());
}

void test_get_and_set_tracking(void) {
    setupSettable();
    TEST_ASSERT_EQUAL_STRING("OFF\n", request(0, "GET TRACKING").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR FEATURE-NOT-CONFIGURED\n",
                             request(0, "GET TRACKING 00000000-0000-4000-8000-000000000000").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", request(0, "SET TRACKING MAYBE").c_str());
    TEST_ASSERT_EQUAL_STRING("OFF\n", request(0, "GET TRACKING").c_str());

    // strcasecmp on ON/OFF
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "set tracking on").c_str());
    TEST_ASSERT_EQUAL_STRING("ON\n", request(0, "GET TRACKING").c_str());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET TRACKING off").c_str());
    TEST_ASSERT_EQUAL_STRING("OFF\n", request(0, "GET TRACKING").c_str());

    // SET is FLAG_USER in upsd: USERNAME and PASSWORD must be sent...
    server.setClientUsername(1, "");
    server.setAuthenticated(1, false);
    TEST_ASSERT_EQUAL_STRING("ERR USERNAME-REQUIRED\n", request(1, "SET TRACKING ON").c_str());
    request(1, "USERNAME admin");
    TEST_ASSERT_EQUAL_STRING("ERR PASSWORD-REQUIRED\n", request(1, "SET TRACKING ON").c_str());
    TEST_ASSERT_EQUAL_STRING("OFF\n", request(1, "GET TRACKING").c_str());
    // ...but net_set() does not check them for TRACKING (no user_checkaction())
    request(1, "PASSWORD wrong");
    TEST_ASSERT_EQUAL_STRING("OK\n", request(1, "SET TRACKING ON").c_str());
    TEST_ASSERT_EQUAL_STRING("ON\n", request(1, "GET TRACKING").c_str());
}

void test_tracking_unknown_and_expired_id(void) {
    setupSettable();
    request(0, "SET TRACKING ON");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, "GET TRACKING 00000000-0000-4000-8000-000000000000").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, "GET TRACKING not-an-id").c_str());

    fakeNow = 5000;
    std::string get = "GET TRACKING " + trackingId(request(0, "SET VAR testups battery.charge.low 30"));
    // tracking_cleanup() drops an entry when difftime() > tracking_delay (3600 s)
    fakeNow = 5000 + 3600000;
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, get.c_str()).c_str());
    fakeNow = 5000 + 3600001;
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, get.c_str()).c_str());
}

void test_tracking_age_across_millis_wrap(void) {
    setupSettable();
    request(0, "SET TRACKING ON");
    fakeNow = 0xFFFFF000u;
    std::string get = "GET TRACKING " + trackingId(request(0, "SET VAR testups battery.charge.low 30"));
    fakeNow = 0x00001000u; // 8.192 s later
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, get.c_str()).c_str());

    // The server loop drops the expired entry, so it cannot look young again once its
    // age wraps around (2^32 ms later)
    fakeNow = 0xFFFFF000u + 3600001u;
    server.loop();
    fakeNow = 0xFFFFF000u + 10u;
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, get.c_str()).c_str());
}

// 16 entries: the 17th outcome replaces the oldest one
void test_tracking_table_drops_oldest(void) {
    setupSettable();
    request(0, "SET TRACKING ON");
    std::vector<std::string> ids;
    for (int i = 0; i < 17; i++) {
        fakeNow = 1000 + (uint32_t)i * 1000;
        ids.push_back(trackingId(request(0, "SET VAR testups battery.charge.low 30")));
        TEST_ASSERT_TRUE(isUuid4(ids.back()));
    }
    for (size_t i = 0; i < ids.size(); i++) {
        for (size_t j = i + 1; j < ids.size(); j++) TEST_ASSERT_TRUE(ids[i] != ids[j]);
    }
    std::string first = "GET TRACKING " + ids.front();
    std::string second = "GET TRACKING " + ids[1];
    std::string last = "GET TRACKING " + ids.back();
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, first.c_str()).c_str());
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, second.c_str()).c_str());
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, last.c_str()).c_str());
}

// The flag is per session, the table is shared, as client->tracking in upsd
void test_tracking_is_per_session(void) {
    setupSettable();
    server.setAuthenticated(1, true);
    request(0, "SET TRACKING ON");
    TEST_ASSERT_EQUAL_STRING("OFF\n", request(1, "GET TRACKING").c_str());
    TEST_ASSERT_EQUAL_STRING("OK\n", request(1, "SET VAR testups battery.charge.low 30").c_str());

    std::string get = "GET TRACKING " + trackingId(request(0, "SET VAR testups battery.charge.low 30"));
    request(1, "SET TRACKING ON");
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(1, get.c_str()).c_str());

    // The end of the session clears the flag
    TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", request(0, "LOGOUT").c_str());
    TEST_ASSERT_EQUAL_STRING("OFF\n", request(0, "GET TRACKING").c_str());
}

void test_set_var_rejections_are_logged(void) {
    setupSettable();
    request(0, "SET VAR wrongups battery.charge.low 30");
    request(0, "SET VAR testups battery.charge \"5 0\"");
    request(0, "SET VAR testups input.sensitivity medium");
    server.setClientUsername(0, "");
    server.setAuthenticated(0, false);
    request(0, "SET VAR testups battery.charge.low 30");
    // Malformed lines are not SET VAR requests: nothing to log
    request(0, "SET VAR testups battery.charge.low");
    TEST_ASSERT_EQUAL(4, (int)logLines.size());
    TEST_ASSERT_EQUAL_STRING("WARN SET battery.charge.low \"30\" from admin: ERR UNKNOWN-UPS", logLines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("WARN SET battery.charge \"5 0\" from admin: ERR READONLY", logLines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("WARN SET input.sensitivity \"medium\" from admin: ERR INVALID-VALUE", logLines[2].c_str());
    TEST_ASSERT_EQUAL_STRING("WARN SET battery.charge.low \"30\" from -: ERR USERNAME-REQUIRED", logLines[3].c_str());
}

// Ids built with the bit operations of nut_uuid_v4() on known random bytes
void test_tracking_id_format(void) {
    NutTracking zeros([]() -> uint32_t { return 0; });
    char id[NutTracking::ID_LEN];
    zeros.newId(id);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000000", id);

    NutTracking ones([]() -> uint32_t { return 0xFFFFFFFFu; });
    ones.newId(id);
    TEST_ASSERT_EQUAL_STRING("FFFFFFFF-FFFF-4FFF-BFFF-FFFFFFFFFFFF", id);

    // Bytes in call order, most significant first
    NutTracking counting([]() -> uint32_t { static uint32_t n = 0; n += 0x11111111u; return n; });
    counting.newId(id);
    // b[0..15] = 11 11 11 11 22 22 22 22 33 33 33 33 44 44 44 44;
    // b[6] = (0x22 & 0x0F) | 0x40 = 0x42, b[8] = (0x33 & 0x3F) | 0x80 = 0xB3
    TEST_ASSERT_EQUAL_STRING("11111111-2222-4222-B333-333344444444", id);
}

// --- INSTCMD with TRACKING (US-069): netinstcmd.c send_instcmd/net_instcmd, upsd.c
// tracking_get, clients/upscmd.c do_cmd ---

// upscmd -w: SET TRACKING ON, INSTCMD, then GET TRACKING <id> until it is not PENDING
void test_upscmd_tracking_sequence(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("ups.beeper.status", "enabled");
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET TRACKING ON").c_str());

    std::string reply = request(0, "INSTCMD testups beeper.disable");
    // upscmd.c do_cmd(), on the line without its newline: strlen("OK TRACKING ") + UUID4_LEN - 1
    TEST_ASSERT_EQUAL(48, (int)reply.size() - 1);
    std::string id = trackingId(reply);
    TEST_ASSERT_TRUE_MESSAGE(isUuid4(id), reply.c_str());
    TEST_ASSERT_FALSE(mockHost.beeperState);

    std::string get = "GET TRACKING " + id;
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, get.c_str()).c_str());
}

// STAT_INSTCMD_FAILED: the write reached the UPS and failed
void test_instcmd_tracking_failed(void) {
    server.setAuthenticated(0, true);
    request(0, "SET TRACKING ON");
    addDelayUsages(true, true, false);
    mockHost.failPaths.push_back(P_ST);

    std::string id = trackingId(request(0, "INSTCMD testups shutdown.return"));
    TEST_ASSERT_TRUE(isUuid4(id));
    TEST_ASSERT_EQUAL_STRING(P_ST "=30", writesStr().c_str());
    std::string get = "GET TRACKING " + id;
    TEST_ASSERT_EQUAL_STRING("ERR FAILED\n", request(0, get.c_str()).c_str());

    mockHost.data.set("ups.beeper.status", "enabled");
    mockHost.setBeeperOk = false;
    id = trackingId(request(0, "INSTCMD testups beeper.enable"));
    TEST_ASSERT_TRUE(isUuid4(id));
    get = "GET TRACKING " + id;
    TEST_ASSERT_EQUAL_STRING("ERR FAILED\n", request(0, get.c_str()).c_str());
}

// STAT_INSTCMD_CONVERSION_FAILED: the parameter is checked before anything is written
void test_instcmd_tracking_invalid_argument(void) {
    server.setAuthenticated(0, true);
    request(0, "SET TRACKING ON");
    addDelayUsages(true, true, true);

    const char* lines[] = {
        "INSTCMD testups load.off.delay abc",
        "INSTCMD testups load.off.delay 65536",
        "INSTCMD testups shutdown.reboot 12x",
    };
    for (const char* line : lines) {
        mockHost.writes.clear();
        std::string id = trackingId(request(0, line));
        TEST_ASSERT_TRUE_MESSAGE(isUuid4(id), line);
        std::string get = "GET TRACKING " + id;
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ERR INVALID-ARGUMENT\n", request(0, get.c_str()).c_str(), line);
        TEST_ASSERT_EQUAL_MESSAGE(0, mockHost.writes.size(), line);
    }
}

// Validation errors stay immediate with TRACKING on: no id, nothing in the table
void test_instcmd_tracking_validation_errors_are_immediate(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("ups.beeper.status", "enabled");
    request(0, "SET TRACKING ON");
    // A known entry: if an error below took a slot, filling the table would drop it
    std::string known = "GET TRACKING " + trackingId(request(0, "INSTCMD testups beeper.enable"));
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, known.c_str()).c_str());
    // Strictly the oldest: on equal ages add() drops another entry
    fakeNow += 1;

    TEST_ASSERT_EQUAL_STRING("ERR CMD-NOT-SUPPORTED\n", request(0, "INSTCMD testups invalid.cmd").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", request(0, "INSTCMD otherups beeper.enable").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", request(0, "INSTCMD testups").c_str());
    mockHost.connected = false;
    TEST_ASSERT_EQUAL_STRING("ERR DRIVER-NOT-CONNECTED\n", request(0, "INSTCMD testups beeper.enable").c_str());
    mockHost.connected = true;

    // Credentials sent but wrong: user_checkinstcmd()
    server.setClientUsername(0, "admin");
    server.setAuthenticated(0, false);
    request(0, "PASSWORD wrong");
    TEST_ASSERT_EQUAL_STRING("ON\n", request(0, "GET TRACKING").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", request(0, "INSTCMD testups beeper.enable").c_str());
    TEST_ASSERT_TRUE(mockHost.beeperState);

    // CAPACITY - 1 more entries fill the table exactly: the known one is still there
    server.setAuthenticated(0, true);
    for (size_t i = 0; i + 1 < NutTracking::CAPACITY; i++) {
        fakeNow += 1;
        TEST_ASSERT_TRUE(isUuid4(trackingId(request(0, "INSTCMD testups beeper.enable"))));
    }
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, known.c_str()).c_str());
}

// INSTCMD and SET VAR share the table: same capacity, same tracking_delay
void test_instcmd_tracking_shares_table_with_set_var(void) {
    setupSettable();
    mockHost.data.set("ups.beeper.status", "enabled");
    request(0, "SET TRACKING ON");

    std::string setGet = "GET TRACKING " + trackingId(request(0, "SET VAR testups battery.charge.low 30"));
    std::string cmdGet = "GET TRACKING " + trackingId(request(0, "INSTCMD testups beeper.disable"));
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, setGet.c_str()).c_str());
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, cmdGet.c_str()).c_str());

    // tracking_cleanup() drops an entry when difftime() > tracking_delay (3600 s)
    fakeNow += NutTracking::MAX_AGE_MS + 1;
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, cmdGet.c_str()).c_str());

    // A full table drops its oldest entry, whichever command created it
    server.clearTracking();
    request(0, "SET TRACKING ON");
    setGet = "GET TRACKING " + trackingId(request(0, "SET VAR testups battery.charge.low 30"));
    std::string last;
    for (size_t i = 0; i < NutTracking::CAPACITY; i++) {
        fakeNow += 1;
        last = "GET TRACKING " + trackingId(request(0, "INSTCMD testups beeper.enable"));
    }
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN\n", request(0, setGet.c_str()).c_str());
    TEST_ASSERT_EQUAL_STRING("SUCCESS\n", request(0, last.c_str()).c_str());
}

// The flag is per session, as client->tracking in upsd
void test_instcmd_tracking_is_per_session(void) {
    server.setAuthenticated(0, true);
    server.setAuthenticated(1, true);
    mockHost.data.set("ups.beeper.status", "enabled");
    request(0, "SET TRACKING ON");
    TEST_ASSERT_EQUAL_STRING("OK\n", request(1, "INSTCMD testups beeper.enable").c_str());
}

// Without TRACKING the reply waits for the command: OK or the synchronous error
void test_instcmd_without_tracking_unchanged(void) {
    server.setAuthenticated(0, true);
    mockHost.data.set("ups.beeper.status", "enabled");
    addDelayUsages(true, true, false);
    request(0, "SET TRACKING ON");
    request(0, "SET TRACKING OFF");

    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "INSTCMD testups beeper.disable").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", request(0, "INSTCMD testups load.off.delay abc").c_str());
    mockHost.setBeeperOk = false;
    TEST_ASSERT_EQUAL_STRING("ERR INSTCMD-FAILED\n", request(0, "INSTCMD testups beeper.enable").c_str());
}

void test_split_tokens_pconf(void) {
    // Lines of the existing tests: same words
    std::vector<String> t = NUTServer::splitTokens("INSTCMD testups load.off.delay 30");
    TEST_ASSERT_EQUAL(4, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("30", t[3].c_str());
    t = NUTServer::splitTokens("LIST ENUM testups input.sensitivity");
    TEST_ASSERT_EQUAL(4, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("input.sensitivity", t[3].c_str());
    t = NUTServer::splitTokens("GET  VAR   testups \"battery.charge\"  ");
    TEST_ASSERT_EQUAL(4, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("VAR", t[1].c_str());
    TEST_ASSERT_EQUAL_STRING("battery.charge", t[3].c_str());

    // Backslash: the next character is literal, in and out of quotes
    t = NUTServer::splitTokens("SET VAR ups v \"a \\\"b\\\" \\\\ \\#\"");
    TEST_ASSERT_EQUAL(5, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("a \"b\" \\ #", t[4].c_str());
    t = NUTServer::splitTokens("x a\\ b");
    TEST_ASSERT_EQUAL(2, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("a b", t[1].c_str());

    // A pair of quotes is a word, even when empty
    t = NUTServer::splitTokens("a \"\" b");
    TEST_ASSERT_EQUAL(3, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("", t[1].c_str());
    TEST_ASSERT_EQUAL_STRING("b", t[2].c_str());
    t = NUTServer::splitTokens("SET VAR testups outlet.desc \"\"");
    TEST_ASSERT_EQUAL(5, (int)t.size());
    TEST_ASSERT_EQUAL_STRING("", t[4].c_str());
}

// ---- Values escaped as pconf_encode() (US-080) ----

// PCONF_ESCAPE of common/parseconf.c: #, backslash and double quote
void test_encode_value(void) {
    TEST_ASSERT_EQUAL_STRING("", NUTServer::encodeValue("").c_str());
    TEST_ASSERT_EQUAL_STRING("", NUTServer::encodeValue(nullptr).c_str());
    TEST_ASSERT_EQUAL_STRING("Main Outlet 1.5 (A)", NUTServer::encodeValue("Main Outlet 1.5 (A)").c_str());
    TEST_ASSERT_EQUAL_STRING("\\\"", NUTServer::encodeValue("\"").c_str());
    TEST_ASSERT_EQUAL_STRING("\\\\", NUTServer::encodeValue("\\").c_str());
    TEST_ASSERT_EQUAL_STRING("\\#", NUTServer::encodeValue("#").c_str());
    TEST_ASSERT_EQUAL_STRING("\\\"\\\"\\\\\\\\\\#\\#", NUTServer::encodeValue("\"\"\\\\##").c_str());
    TEST_ASSERT_EQUAL_STRING("Rack \\\"A\\\" \\\\ 1", NUTServer::encodeValue("Rack \"A\" \\ 1").c_str());
}

static bool outputHas(const char* line) {
    return printer.getOutput().find(line) != std::string::npos;
}

void test_var_replies_are_escaped(void) {
    setupWritable();
    mockHost.data.set("outlet.desc", "Rack \"A\" \\ #1");
    mockHost.data.set("ups.model", "Back-UPS \"X\"");
    mockHost.rw.push_back({ "outlet.desc", 20, {} });

    server.processCommand(printer, 0, "GET VAR testups outlet.desc");
    TEST_ASSERT_EQUAL_STRING("VAR testups outlet.desc \"Rack \\\"A\\\" \\\\ \\#1\"\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups device.model");
    TEST_ASSERT_EQUAL_STRING("VAR testups device.model \"Back-UPS \\\"X\\\"\"\n", printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "LIST VAR testups");
    TEST_ASSERT_TRUE(outputHas("\nVAR testups outlet.desc \"Rack \\\"A\\\" \\\\ \\#1\"\n"));
    TEST_ASSERT_TRUE(outputHas("\nVAR testups device.model \"Back-UPS \\\"X\\\"\"\n"));
    TEST_ASSERT_TRUE(outputHas("\nVAR testups ups.model \"Back-UPS \\\"X\\\"\"\n"));
    // Plain values unchanged
    TEST_ASSERT_TRUE(outputHas("\nVAR testups battery.charge.low \"20\"\n"));

    printer.clear();
    server.processCommand(printer, 0, "LIST RW testups");
    TEST_ASSERT_EQUAL_STRING(
        "BEGIN LIST RW testups\n"
        "RW testups battery.charge.low \"20\"\n"
        "RW testups input.sensitivity \"normal\"\n"
        "RW testups outlet.desc \"Rack \\\"A\\\" \\\\ \\#1\"\n"
        "END LIST RW testups\n",
        printer.getOutput().c_str());
}

// state_addenum() stores the ENUM values already encoded
void test_enum_values_are_escaped(void) {
    mockHost.data.set("ups.test.mode", "a");
    mockHost.rw = { { "ups.test.mode", 10, { "a", "say \"hi\"", "#2" } } };
    server.processCommand(printer, 0, "LIST ENUM testups ups.test.mode");
    TEST_ASSERT_EQUAL_STRING(
        "BEGIN LIST ENUM testups ups.test.mode\n"
        "ENUM testups ups.test.mode \"a\"\n"
        "ENUM testups ups.test.mode \"say \\\"hi\\\"\"\n"
        "ENUM testups ups.test.mode \"\\#2\"\n"
        "END LIST ENUM testups ups.test.mode\n",
        printer.getOutput().c_str());
}

// Regression guard: get_desc() / get_cmddesc() of netget.c send the text without
// pconf_encode(). No text of cmdvartab has #, \ or ", so this pins the line format and
// cannot tell an escaped description from a raw one
void test_desc_and_cmddesc_lines_unchanged(void) {
    server.processCommand(printer, 0, "GET DESC testups battery.runtime.low");
    TEST_ASSERT_EQUAL_STRING("DESC testups battery.runtime.low \"Remaining battery runtime when UPS switches to LB (seconds)\"\n",
                             printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "GET CMDDESC testups load.off");
    TEST_ASSERT_EQUAL_STRING("CMDDESC testups load.off \"Turn off the load immediately\"\n", printer.getOutput().c_str());
}

// ---- UPS name never empty, no fallback without it (US-082) ----

void test_ups_name_rules(void) {
    TEST_ASSERT_EQUAL_STRING("ups", NUTServer::defaultUpsName().c_str());
    TEST_ASSERT_EQUAL_STRING("ups", NUTServer::normalizeUpsName("").c_str());
    TEST_ASSERT_EQUAL_STRING("eaton", NUTServer::normalizeUpsName("eaton").c_str());
    TEST_ASSERT_EQUAL_STRING("ups", NUTServer::normalizeUpsName("ups").c_str());

    TEST_ASSERT_TRUE(NUTServer::isValidUpsName("ups"));
    TEST_ASSERT_TRUE(NUTServer::isValidUpsName("eaton"));
    TEST_ASSERT_TRUE(NUTServer::isValidUpsName("Back-UPS_1"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName(""));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("my ups"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("a\tb"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("citt\xc3\xa0"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("a\x7f"));
    // Edges of the printable range
    TEST_ASSERT_TRUE(NUTServer::isValidUpsName("!"));
    TEST_ASSERT_TRUE(NUTServer::isValidUpsName("~"));
    TEST_ASSERT_TRUE(NUTServer::isValidUpsName("[ups]"));
    // pconf quote, escape and comment; @ splits "ups@host" in upscli_splitname()
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("a\"b"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("a\\b"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("a#b"));
    TEST_ASSERT_FALSE(NUTServer::isValidUpsName("a@b"));
}

// Malformed LIST/GET lines: INVALID-ARGUMENT as net_list() / net_get(), commands in any case
void test_malformed_list_and_get_are_invalid(void) {
    setupWritable();
    const char* lines[] = {
        "LIST", "LIST FOO", "LIST ENUM testups", "LIST RANGE testups",
        "GET", "GET FOO testups", "GET VAR testups", "list var", "Get Var battery.charge",
    };
    for (const char* line : lines) {
        printer.clear();
        server.processCommand(printer, 0, line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ERR INVALID-ARGUMENT\n", printer.getOutput().c_str(), line);
    }
    // Lowercase commands with the UPS name work as before
    printer.clear();
    server.processCommand(printer, 0, "list var testups");
    TEST_ASSERT_EQUAL_INT(0, (int)printer.getOutput().find("BEGIN LIST VAR testups\n"));
}

// list_cmd() / list_clients() of netlist.c: unknown UPS → UNKNOWN-UPS; list_cmd() also
// checks ups_available() (stale data → DATA-STALE)
void test_list_cmd_and_client_unknown_ups(void) {
    server.processCommand(printer, 0, "LIST CMD wrongups");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "LIST CLIENT wrongups");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "LIST CMD TestUPS");
    TEST_ASSERT_EQUAL_INT(0, (int)printer.getOutput().find("BEGIN LIST CMD TestUPS\n"));

    printer.clear();
    mockHost.stale = true;
    server.processCommand(printer, 0, "LIST CMD testups");
    TEST_ASSERT_EQUAL_STRING("ERR DATA-STALE\n", printer.getOutput().c_str());
    // list_clients() does not call ups_available()
    printer.clear();
    server.processCommand(printer, 0, "LIST CLIENT testups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CLIENT testups\nEND LIST CLIENT testups\n", printer.getOutput().c_str());
}

// A configuration with an empty name (first Wi-Fi setup before US-082) serves "ups"
void test_empty_ups_name_defaults_to_ups(void) {
    NUTServerConfig config;
    config.ups_name = "";
    NUTServer local;
    local.begin(config, &mockHost, 3493);

    local.processCommand(printer, 0, "LIST UPS");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST UPS\nUPS ups \"ESP32-S3 UPS Bridge\"\nEND LIST UPS\n",
                             printer.getOutput().c_str());
    printer.clear();
    local.processCommand(printer, 0, "LIST VAR ups");
    TEST_ASSERT_EQUAL_INT(0, (int)printer.getOutput().find("BEGIN LIST VAR ups\n"));
    // An empty name no longer matches, as get_ups_ptr("") of upsd
    printer.clear();
    local.processCommand(printer, 0, "LIST VAR \"\"");
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS\n", printer.getOutput().c_str());
}

void test_commands_without_ups_name_are_invalid(void) {
    setupWritable();
    const char* lines[] = {
        "LIST VAR", "LIST RW", "LIST CMD", "LIST CLIENT",
        "LIST ENUM input.sensitivity", "LIST RANGE battery.charge.low",
        "GET VAR battery.charge", "GET TYPE battery.charge", "GET DESC battery.charge",
        "GET CMDDESC load.off", "GET UPSDESC", "GET NUMLOGINS",
    };
    for (const char* line : lines) {
        printer.clear();
        server.processCommand(printer, 0, line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ERR INVALID-ARGUMENT\n", printer.getOutput().c_str(), line);
    }
}

void test_unknown_list_subcommand_is_invalid(void) {
    server.processCommand(printer, 0, "LIST FOO testups");
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT\n", printer.getOutput().c_str());
    printer.clear();
    server.processCommand(printer, 0, "LIST UPS");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST UPS\nUPS testups \"ESP32-S3 UPS Bridge\"\nEND LIST UPS\n",
                             printer.getOutput().c_str());
}

// upsrw sends the value escaped, the server stores it raw and sends it escaped again
void test_set_var_escaped_round_trip(void) {
    setupSettable();
    mockHost.storeOnSet = true;
    TEST_ASSERT_EQUAL_STRING("OK\n", request(0, "SET VAR testups outlet.desc \"Rack \\\"A\\\" \\\\ \\#1\"").c_str());
    TEST_ASSERT_EQUAL_STRING("Rack \"A\" \\ #1", mockHost.data.get("outlet.desc").c_str());
    TEST_ASSERT_EQUAL_STRING("VAR testups outlet.desc \"Rack \\\"A\\\" \\\\ \\#1\"\n",
                             request(0, "GET VAR testups outlet.desc").c_str());
}

// --- Slot dei client (US-084) ---

static void openClients(int n) {
    for (int i = 0; i < n; i++) {
        String ip = String("10.0.0.") + String(i + 1);
        TEST_ASSERT_EQUAL(i, server.openSession(ip, (uint16_t)(50000 + i)));
    }
}

static size_t countLogs(const char* needle) {
    size_t n = 0;
    for (const auto& l : logLines) {
        if (l.find(needle) != std::string::npos) n++;
    }
    return n;
}

void test_eight_clients_then_rejected(void) {
    openClients(8);
    TEST_ASSERT_EQUAL(-1, server.openSession("10.0.0.99", 50099));
    TEST_ASSERT_EQUAL(1, (int)logLines.size());
    TEST_ASSERT_EQUAL_STRING("WARN [NUT] Connection from 10.0.0.99 rejected: all 8 slots in use "
                             "(10.0.0.1, 10.0.0.2, 10.0.0.3, 10.0.0.4, 10.0.0.5, 10.0.0.6, 10.0.0.7, 10.0.0.8)",
                             logLines[0].c_str());
}

// Connessioni e chiusure normali non vanno nel System Log
void test_open_and_logout_do_not_log(void) {
    openClients(2);
    TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", request(1, "LOGOUT").c_str());
    TEST_ASSERT_EQUAL(0, (int)logLines.size());
}

void test_logout_frees_the_slot_for_reuse(void) {
    openClients(8);
    TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", request(3, "LOGOUT").c_str());
    TEST_ASSERT_EQUAL(3, server.openSession("10.0.0.50", 50050));
    TEST_ASSERT_EQUAL(-1, server.openSession("10.0.0.51", 50051));
}

// Un client che riprova di continuo: una riga ogni NUT_LOG_INTERVAL_MS, con il conteggio
void test_rejections_are_throttled(void) {
    openClients(8);
    TEST_ASSERT_EQUAL(-1, server.openSession("10.0.0.99", 50099));
    fakeNow += 10000;
    TEST_ASSERT_EQUAL(-1, server.openSession("10.0.0.99", 50100));
    fakeNow += 10000;
    TEST_ASSERT_EQUAL(-1, server.openSession("10.0.0.99", 50101));
    TEST_ASSERT_EQUAL(1, (int)countLogs("rejected"));
    fakeNow += NUT_LOG_INTERVAL_MS - 20000;
    TEST_ASSERT_EQUAL(-1, server.openSession("10.0.0.98", 50102));
    TEST_ASSERT_EQUAL(2, (int)countLogs("rejected"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          logLines.back().find("Connection from 10.0.0.98 rejected: all 8 slots in use ("));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, logLines.back().find(") (2 more rejected since the last report)"));
}

void test_clients_summary_only_on_change(void) {
    openClients(2);
    server.loop();
    TEST_ASSERT_EQUAL(0, (int)countLogs("Clients:"));  // prima dei 60 s
    fakeNow += NUT_LOG_INTERVAL_MS;
    server.loop();
    TEST_ASSERT_EQUAL(1, (int)countLogs("Clients:"));
    TEST_ASSERT_EQUAL_STRING("INFO [NUT] Clients: 2/8 in use - 10.0.0.1, 10.0.0.2", logLines.back().c_str());

    fakeNow += NUT_LOG_INTERVAL_MS;
    server.loop();
    TEST_ASSERT_EQUAL(1, (int)countLogs("Clients:"));  // nessun cambiamento

    TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", request(0, "LOGOUT").c_str());
    fakeNow += 1000;
    server.loop();
    TEST_ASSERT_EQUAL(1, (int)countLogs("Clients:"));  // cambiato, ma entro 60 s dall'ultimo controllo
    fakeNow += NUT_LOG_INTERVAL_MS;
    server.loop();
    TEST_ASSERT_EQUAL(2, (int)countLogs("Clients:"));
    TEST_ASSERT_EQUAL_STRING("INFO [NUT] Clients: 1/8 in use - 10.0.0.2", logLines.back().c_str());

    TEST_ASSERT_EQUAL_STRING("OK Goodbye\n", request(1, "LOGOUT").c_str());
    fakeNow += NUT_LOG_INTERVAL_MS;
    server.loop();
    TEST_ASSERT_EQUAL_STRING("INFO [NUT] Clients: 0/8 in use", logLines.back().c_str());
}

void test_inactivity_close_is_logged(void) {
    openClients(3);
    server.closeSession(2, NUTServer::CloseReason::Inactivity);
    TEST_ASSERT_EQUAL(1, (int)logLines.size());
    TEST_ASSERT_EQUAL_STRING("INFO [NUT] Client 10.0.0.3 on slot 2 disconnected after 300 s of inactivity",
                             logLines[0].c_str());
    server.closeSession(1, NUTServer::CloseReason::PeerClosed);
    TEST_ASSERT_EQUAL(1, (int)logLines.size());
    TEST_ASSERT_EQUAL(1, server.openSession("10.0.0.60", 50060));
}

#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_split_tokens);
    RUN_TEST(test_auth_flow);
    RUN_TEST(test_list_ups);
    RUN_TEST(test_get_var_compliance);
    RUN_TEST(test_instcmd_beeper);
    RUN_TEST(test_beeper_hidden_when_not_controllable);
    RUN_TEST(test_list_client_terminates);
    RUN_TEST(test_list_cmd_unaffected_by_client);
    RUN_TEST(test_get_upsdesc_and_numlogins);
    RUN_TEST(test_get_desc_and_type);
    RUN_TEST(test_list_rw_with_values);
    RUN_TEST(test_list_rw_errors_and_empty);
    RUN_TEST(test_get_type_of_writable_variables);
    RUN_TEST(test_list_enum_and_range);
    RUN_TEST(test_get_desc_of_table_variables);
    RUN_TEST(test_ver_and_netver);
    RUN_TEST(test_gonut_newups_sequence);
    RUN_TEST(test_data_stale);
    RUN_TEST(test_disconnected_is_stale);
    RUN_TEST(test_device_aliases);
    RUN_TEST(test_get_var_echoes_client_var_name);
    RUN_TEST(test_list_cmd_from_descriptor);
    RUN_TEST(test_list_cmd_empty_when_disconnected);
    RUN_TEST(test_list_cmd_without_controllable_beeper);
    RUN_TEST(test_cmddesc_known_and_unknown);
    RUN_TEST(test_instcmd_battery_test_writes_value);
    RUN_TEST(test_instcmd_not_in_catalog);
    RUN_TEST(test_instcmd_driver_not_connected);
    RUN_TEST(test_instcmd_write_failed);
    RUN_TEST(test_instcmd_requires_login);
    RUN_TEST(test_login_requires_credentials);
    RUN_TEST(test_instcmd_shutdown_sequences);
    RUN_TEST(test_instcmd_stayoff_writes_minus_one_as_twos_complement);
    RUN_TEST(test_instcmd_uses_configured_delays);
    RUN_TEST(test_instcmd_without_configured_delays);
    RUN_TEST(test_instcmd_param_too_large_for_field);
    RUN_TEST(test_instcmd_invalid_argument);
    RUN_TEST(test_instcmd_sequence_stops_at_first_failed_write);
    RUN_TEST(test_instcmd_shutdown_requires_login);
    RUN_TEST(test_instcmd_shutdown_default_fallback);
    RUN_TEST(test_instcmd_shutdown_default_all_fail);
    RUN_TEST(test_instcmd_shutdown_default_without_groups);
    RUN_TEST(test_list_cmd_includes_shutdown_default);
    RUN_TEST(test_instcmd_beeper_failure_and_toggle);
    RUN_TEST(test_instcmd_beeper_mute_and_toggle_from_muted);
    RUN_TEST(test_set_malformed_is_invalid_argument);
    RUN_TEST(test_set_var_unknown_ups);
    RUN_TEST(test_set_var_driver_not_connected);
    RUN_TEST(test_set_var_data_stale);
    RUN_TEST(test_set_var_access_denied);
    RUN_TEST(test_set_var_not_supported_and_readonly);
    RUN_TEST(test_set_var_aux_zero_is_set_failed);
    RUN_TEST(test_set_var_too_long);
    RUN_TEST(test_set_var_invalid_enum_value);
    RUN_TEST(test_set_var_check_order);
    RUN_TEST(test_set_var_canonical_name);
    RUN_TEST(test_set_var_quoted_values);
    RUN_TEST(test_set_var_synchronous_outcome);
    RUN_TEST(test_upsrw_tracking_sequence);
    RUN_TEST(test_tracking_outcomes);
    RUN_TEST(test_tracking_validation_error_is_immediate);
    RUN_TEST(test_get_and_set_tracking);
    RUN_TEST(test_tracking_unknown_and_expired_id);
    RUN_TEST(test_tracking_age_across_millis_wrap);
    RUN_TEST(test_tracking_table_drops_oldest);
    RUN_TEST(test_tracking_is_per_session);
    RUN_TEST(test_set_var_rejections_are_logged);
    RUN_TEST(test_tracking_id_format);
    RUN_TEST(test_upscmd_tracking_sequence);
    RUN_TEST(test_instcmd_tracking_failed);
    RUN_TEST(test_instcmd_tracking_invalid_argument);
    RUN_TEST(test_instcmd_tracking_validation_errors_are_immediate);
    RUN_TEST(test_instcmd_tracking_shares_table_with_set_var);
    RUN_TEST(test_instcmd_tracking_is_per_session);
    RUN_TEST(test_instcmd_without_tracking_unchanged);
    RUN_TEST(test_split_tokens_pconf);
    RUN_TEST(test_encode_value);
    RUN_TEST(test_var_replies_are_escaped);
    RUN_TEST(test_enum_values_are_escaped);
    RUN_TEST(test_desc_and_cmddesc_lines_unchanged);
    RUN_TEST(test_set_var_escaped_round_trip);
    RUN_TEST(test_ups_name_rules);
    RUN_TEST(test_empty_ups_name_defaults_to_ups);
    RUN_TEST(test_commands_without_ups_name_are_invalid);
    RUN_TEST(test_unknown_list_subcommand_is_invalid);
    RUN_TEST(test_malformed_list_and_get_are_invalid);
    RUN_TEST(test_list_cmd_and_client_unknown_ups);
    RUN_TEST(test_eight_clients_then_rejected);
    RUN_TEST(test_open_and_logout_do_not_log);
    RUN_TEST(test_logout_frees_the_slot_for_reuse);
    RUN_TEST(test_rejections_are_throttled);
    RUN_TEST(test_clients_summary_only_on_change);
    RUN_TEST(test_inactivity_close_is_logged);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_split_tokens);
    RUN_TEST(test_auth_flow);
    RUN_TEST(test_list_ups);
    RUN_TEST(test_get_var_compliance);
    RUN_TEST(test_instcmd_beeper);
    RUN_TEST(test_beeper_hidden_when_not_controllable);
    RUN_TEST(test_list_client_terminates);
    RUN_TEST(test_list_cmd_unaffected_by_client);
    RUN_TEST(test_get_upsdesc_and_numlogins);
    RUN_TEST(test_get_desc_and_type);
    RUN_TEST(test_list_rw_with_values);
    RUN_TEST(test_list_rw_errors_and_empty);
    RUN_TEST(test_get_type_of_writable_variables);
    RUN_TEST(test_list_enum_and_range);
    RUN_TEST(test_get_desc_of_table_variables);
    RUN_TEST(test_ver_and_netver);
    RUN_TEST(test_gonut_newups_sequence);
    RUN_TEST(test_data_stale);
    RUN_TEST(test_disconnected_is_stale);
    RUN_TEST(test_device_aliases);
    RUN_TEST(test_get_var_echoes_client_var_name);
    RUN_TEST(test_list_cmd_from_descriptor);
    RUN_TEST(test_list_cmd_empty_when_disconnected);
    RUN_TEST(test_list_cmd_without_controllable_beeper);
    RUN_TEST(test_cmddesc_known_and_unknown);
    RUN_TEST(test_instcmd_battery_test_writes_value);
    RUN_TEST(test_instcmd_not_in_catalog);
    RUN_TEST(test_instcmd_driver_not_connected);
    RUN_TEST(test_instcmd_write_failed);
    RUN_TEST(test_instcmd_requires_login);
    RUN_TEST(test_login_requires_credentials);
    RUN_TEST(test_instcmd_shutdown_sequences);
    RUN_TEST(test_instcmd_stayoff_writes_minus_one_as_twos_complement);
    RUN_TEST(test_instcmd_uses_configured_delays);
    RUN_TEST(test_instcmd_without_configured_delays);
    RUN_TEST(test_instcmd_param_too_large_for_field);
    RUN_TEST(test_instcmd_invalid_argument);
    RUN_TEST(test_instcmd_sequence_stops_at_first_failed_write);
    RUN_TEST(test_instcmd_shutdown_requires_login);
    RUN_TEST(test_instcmd_shutdown_default_fallback);
    RUN_TEST(test_instcmd_shutdown_default_all_fail);
    RUN_TEST(test_instcmd_shutdown_default_without_groups);
    RUN_TEST(test_list_cmd_includes_shutdown_default);
    RUN_TEST(test_instcmd_beeper_failure_and_toggle);
    RUN_TEST(test_instcmd_beeper_mute_and_toggle_from_muted);
    RUN_TEST(test_set_malformed_is_invalid_argument);
    RUN_TEST(test_set_var_unknown_ups);
    RUN_TEST(test_set_var_driver_not_connected);
    RUN_TEST(test_set_var_data_stale);
    RUN_TEST(test_set_var_access_denied);
    RUN_TEST(test_set_var_not_supported_and_readonly);
    RUN_TEST(test_set_var_aux_zero_is_set_failed);
    RUN_TEST(test_set_var_too_long);
    RUN_TEST(test_set_var_invalid_enum_value);
    RUN_TEST(test_set_var_check_order);
    RUN_TEST(test_set_var_canonical_name);
    RUN_TEST(test_set_var_quoted_values);
    RUN_TEST(test_set_var_synchronous_outcome);
    RUN_TEST(test_upsrw_tracking_sequence);
    RUN_TEST(test_tracking_outcomes);
    RUN_TEST(test_tracking_validation_error_is_immediate);
    RUN_TEST(test_get_and_set_tracking);
    RUN_TEST(test_tracking_unknown_and_expired_id);
    RUN_TEST(test_tracking_age_across_millis_wrap);
    RUN_TEST(test_tracking_table_drops_oldest);
    RUN_TEST(test_tracking_is_per_session);
    RUN_TEST(test_set_var_rejections_are_logged);
    RUN_TEST(test_tracking_id_format);
    RUN_TEST(test_upscmd_tracking_sequence);
    RUN_TEST(test_instcmd_tracking_failed);
    RUN_TEST(test_instcmd_tracking_invalid_argument);
    RUN_TEST(test_instcmd_tracking_validation_errors_are_immediate);
    RUN_TEST(test_instcmd_tracking_shares_table_with_set_var);
    RUN_TEST(test_instcmd_tracking_is_per_session);
    RUN_TEST(test_instcmd_without_tracking_unchanged);
    RUN_TEST(test_split_tokens_pconf);
    RUN_TEST(test_encode_value);
    RUN_TEST(test_var_replies_are_escaped);
    RUN_TEST(test_enum_values_are_escaped);
    RUN_TEST(test_desc_and_cmddesc_lines_unchanged);
    RUN_TEST(test_set_var_escaped_round_trip);
    RUN_TEST(test_ups_name_rules);
    RUN_TEST(test_empty_ups_name_defaults_to_ups);
    RUN_TEST(test_commands_without_ups_name_are_invalid);
    RUN_TEST(test_unknown_list_subcommand_is_invalid);
    RUN_TEST(test_malformed_list_and_get_are_invalid);
    RUN_TEST(test_list_cmd_and_client_unknown_ups);
    RUN_TEST(test_eight_clients_then_rejected);
    RUN_TEST(test_logout_frees_the_slot_for_reuse);
    RUN_TEST(test_rejections_are_throttled);
    RUN_TEST(test_clients_summary_only_on_change);
    UNITY_END();
}
void loop() {}
#endif
#endif

