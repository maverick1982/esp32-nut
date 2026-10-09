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
    bool supportsBeeperToggle() const override { return beeperToggleSupported; }

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

void setUp(void) {
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

#ifdef PIO_UNIT_TESTING
void test_list_client_terminates(void) {
    // LIST CLIENT must be answered with a BEGIN/END pair. A bare "ERR" is what
    // breaks go.nut: it treats every "LIST " command as multi-line and only ends
    // its read loop on "END LIST ...", so a single error line blocks the client
    // until i/o timeout instead of failing fast.
    server.processCommand(printer, 0, "LIST CLIENT testups");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CLIENT testups\nEND LIST CLIENT testups\n",
                             printer.getOutput().c_str());

    // Unknown UPS names still fall through to the shared handler, not an error.
    printer.clear();
    server.processCommand(printer, 0, "LIST CLIENT");
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CLIENT testups\nEND LIST CLIENT testups\n",
                             printer.getOutput().c_str());
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
    TEST_ASSERT_EQUAL_STRING("CMDDESC testups foo.bar \"Unavailable\"\n", printer.getOutput().c_str());
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
    server.setAuthenticated(0, false);
    mockHost._mockUsages.push_back(featureUsage("UPS.BatterySystem.Battery.Test", 0x00840058));

    server.processCommand(printer, 0, "INSTCMD testups test.battery.start.quick");
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", printer.getOutput().c_str());
    TEST_ASSERT_EQUAL(0, mockHost.writes.size());
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
    server.setAuthenticated(0, false);
    addDelayUsages(true, true, true);

    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", instcmd("INSTCMD testups shutdown.return").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", instcmd("INSTCMD testups shutdown.default").c_str());
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED\n", instcmd("INSTCMD testups load.off.delay 30").c_str());
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

void test_get_desc_and_type(void) {
    server.processCommand(printer, 0, "GET DESC testups ups.status");
    TEST_ASSERT_EQUAL_STRING("DESC testups ups.status \"Unavailable\"\n",
                             printer.getOutput().c_str());

    printer.clear();
    server.processCommand(printer, 0, "GET TYPE testups ups.status");
    std::string typeOut = printer.getOutput();
    TEST_ASSERT_EQUAL_STRING("TYPE testups ups.status STRING:64\n", typeOut.c_str());
    // Clients read the token after "RW" as the type, so a bare RW would crash
    // them. Nothing this bridge exposes is writeable.
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
    RUN_TEST(test_ver_and_netver);
    RUN_TEST(test_gonut_newups_sequence);
    RUN_TEST(test_data_stale);
    RUN_TEST(test_disconnected_is_stale);
    RUN_TEST(test_device_aliases);
    RUN_TEST(test_list_cmd_from_descriptor);
    RUN_TEST(test_list_cmd_empty_when_disconnected);
    RUN_TEST(test_list_cmd_without_controllable_beeper);
    RUN_TEST(test_cmddesc_known_and_unknown);
    RUN_TEST(test_instcmd_battery_test_writes_value);
    RUN_TEST(test_instcmd_not_in_catalog);
    RUN_TEST(test_instcmd_driver_not_connected);
    RUN_TEST(test_instcmd_write_failed);
    RUN_TEST(test_instcmd_requires_login);
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
    RUN_TEST(test_ver_and_netver);
    RUN_TEST(test_gonut_newups_sequence);
    RUN_TEST(test_data_stale);
    RUN_TEST(test_disconnected_is_stale);
    RUN_TEST(test_device_aliases);
    RUN_TEST(test_list_cmd_from_descriptor);
    RUN_TEST(test_list_cmd_empty_when_disconnected);
    RUN_TEST(test_list_cmd_without_controllable_beeper);
    RUN_TEST(test_cmddesc_known_and_unknown);
    RUN_TEST(test_instcmd_battery_test_writes_value);
    RUN_TEST(test_instcmd_not_in_catalog);
    RUN_TEST(test_instcmd_driver_not_connected);
    RUN_TEST(test_instcmd_write_failed);
    RUN_TEST(test_instcmd_requires_login);
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
    UNITY_END();
}
void loop() {}
#endif
#endif

