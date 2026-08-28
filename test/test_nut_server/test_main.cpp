#include <unity.h>
#include "NUTServer.h"
#include "IUSBHostUPS.h"
#include <sstream>

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

    const UPSData& getUPSData() const override {
        return data;
    }

    String getUPSStatusString() const override {
        return statusString;
    }

    bool setBeeper(bool enable) override {
        beeperState = enable;
        data.beeperEnabled = enable;
        data.has.beeperEnabled = true;
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

    // Setup UPS data
    mockHost.data.has.batteryVoltage = true;
    mockHost.data.batteryVoltage = 13.6f;
    mockHost.data.has.batteryTemperature = true;
    mockHost.data.batteryTemperature = 28.5f;
    mockHost.data.has.remainingCapacity = true;
    mockHost.data.remainingCapacity = 95;
    mockHost.data.has.designCapacity = true;
    mockHost.data.designCapacity = 100;
    mockHost.data.has.fullChargeCapacity = true;
    mockHost.data.fullChargeCapacity = 100;
    mockHost.data.has.batteryMfrDate = true;
    mockHost.data.batteryMfrDate = "2024/05/23";
    mockHost.data.has.batteryDate = true;
    mockHost.data.batteryDate = "2025/01/10";
    mockHost.data.has.outputVoltage = true;
    mockHost.data.outputVoltage = 230.0f;
    mockHost.data.has.manufacturer = true;
    mockHost.data.manufacturer = "APC";

    // Supported var: battery.voltage
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.voltage");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.voltage \"13.6\"\n", printer.getOutput().c_str());

    // Supported var: battery.mfr.date
    printer.clear();
    server.processCommand(printer, 0, "GET VAR testups battery.mfr.date");
    TEST_ASSERT_EQUAL_STRING("VAR testups battery.mfr.date \"2024/05/23\"\n", printer.getOutput().c_str());

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
    mockHost.data.has.beeperEnabled = true;

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
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_split_tokens);
    RUN_TEST(test_auth_flow);
    RUN_TEST(test_list_ups);
    RUN_TEST(test_get_var_compliance);
    RUN_TEST(test_instcmd_beeper);
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
    UNITY_END();
}
void loop() {}
#endif
#endif
