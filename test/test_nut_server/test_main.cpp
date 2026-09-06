#include <unity.h>
#include "NUTServer.h"
#include "IUSBHostUPS.h"
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

    bool setBeeper(bool enable) override {
        beeperState = enable;
        data.set("ups.beeper.status", enable ? "enabled" : "disabled");
        return true;
    }

    bool isConnected() const override {
        return connected;
    }

    std::vector<HIDUsageDef> _mockUsages;
    const std::vector<HIDUsageDef>& getUsages() const override { return _mockUsages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
    uint32_t getQuirks() const override { return 0; }
    bool isControlPending() const override { return false; }
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
    mockHost.connected = true;

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
    TEST_ASSERT_EQUAL_STRING("CMDDESC testups beeper.enable \"Unavailable\"\n",
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
    RUN_TEST(test_list_client_terminates);
    RUN_TEST(test_list_cmd_unaffected_by_client);
    RUN_TEST(test_get_upsdesc_and_numlogins);
    RUN_TEST(test_get_desc_and_type);
    RUN_TEST(test_ver_and_netver);
    RUN_TEST(test_gonut_newups_sequence);
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
    RUN_TEST(test_list_client_terminates);
    RUN_TEST(test_list_cmd_unaffected_by_client);
    RUN_TEST(test_get_upsdesc_and_numlogins);
    RUN_TEST(test_get_desc_and_type);
    RUN_TEST(test_ver_and_netver);
    RUN_TEST(test_gonut_newups_sequence);
    UNITY_END();
}
void loop() {}
#endif
#endif

