// US-104: the UPS drivers depend on IHidTransport only, never on the USB host.
#include <unity.h>
#include <dirent.h>
#include <fstream>
#include <string.h>
#include <sstream>
#include <string>
#include <vector>
#include "IHidTransport.h"
#include "IUPSDriver.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "EcoFlowDriver.h"
#include "LegrandDriver.h"
#include "LiebertDriver.h"
#include "OpenUPSDriver.h"
#include "PowercomDriver.h"
#include "UPSData.h"
#include "IHidControlPort.h"
#include "IUpsControl.h"
#include "HidControl.h"

// The driver headers must not bring in the USB host (ADR 0010 step 4)
#ifdef I_USB_HOST_UPS_H
#error "IUSBHostUPS.h included by the driver headers (US-104)"
#endif
#ifdef USB_HOST_UPS_H
#error "USBHostUPS.h included by the driver headers (US-104)"
#endif
#ifdef I_UPS_SOURCE_H
#error "IUpsSource.h included by the driver headers (US-104)"
#endif

// A transport with no device behind it: usages set by the test, report requests counted
class MinimalTransport : public IHidTransport {
public:
    std::vector<HIDUsageDef> usages;
    HIDParser parser;
    int reportRequests = 0;
    mutable int depth = 0;
    mutable int lockCalls = 0;

    void lock() const override {
        depth++;
        lockCalls++;
    }
    void unlock() const override { depth--; }
    const std::vector<HIDUsageDef>& getUsages() const override { return usages; }
    const HIDParser* getHIDParser() const override { return &parser; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override {
        reportRequests++;
        return true;
    }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

void setUp(void) {}
void tearDown(void) {}

void test_transport_alone_drives_a_driver(void) {
    MinimalTransport transport;
    // One FEATURE report with the battery charge, as in test_poll_cycle
    HIDUsageDef charge;
    charge.usage = 0x00850066;
    charge.report_id = 12;
    charge.report_type = 3;
    charge.bit_size = 8;
    charge.found = true;
    strncpy(charge.path, "UPS.PowerSummary.RemainingCapacity", sizeof(charge.path) - 1);
    transport.usages.push_back(charge);
    GenericDriver driver;
    UPSData data;
    driver.setup();
    for (uint32_t now = 1000; now < 20000; now += 1000) {
        driver.loop(&transport, data, now);
    }
    TEST_ASSERT_EQUAL_STRING(driver.getDriverName(), data.get("driver.name").c_str());
    // The data lock is taken through the transport and always released (ADR 0008)
    TEST_ASSERT_TRUE(transport.lockCalls > 0);
    TEST_ASSERT_EQUAL(0, transport.depth);
    // The poll cycle requests the FEATURE report through the transport
    TEST_ASSERT_TRUE(transport.reportRequests > 0);
}

// The #error checks above see only the headers; the driver sources must not include the
// USB host either (US-104)
static std::string projectPath(const char* rel) {
    std::string self = __FILE__;
    for (char& c : self) if (c == '\\') c = '/';
    size_t cut = self.rfind("test/test_hid_transport/");
    return (cut == std::string::npos ? std::string() : self.substr(0, cut)) + rel;
}

void test_driver_sources_include_no_usb_host(void) {
    const char* drivers[] = { "Generic", "APC", "CyberPower", "Eaton", "EcoFlow", "Legrand", "Liebert", "OpenUPS",
                              "Powercom" };
    std::vector<std::string> files = { "lib/USBHostUPS/src/IUPSDriver.h" };
    for (const char* d : drivers) {
        files.push_back(std::string("lib/USBHostUPS/src/") + d + "Driver.h");
        files.push_back(std::string("lib/USBHostUPS/src/") + d + "Driver.cpp");
    }
    const char* forbidden[] = { "#include \"IUSBHostUPS.h\"", "#include \"USBHostUPS.h\"" };
    for (const std::string& f : files) {
        std::ifstream in(projectPath(f.c_str()));
        TEST_ASSERT_TRUE_MESSAGE(in.good(), f.c_str());
        std::stringstream text;
        text << in.rdbuf();
        for (const char* inc : forbidden) {
            TEST_ASSERT_TRUE_MESSAGE(text.str().find(inc) == std::string::npos, (f + ": " + inc).c_str());
        }
    }
}

static std::string readSource(const char* rel) {
    std::ifstream in(projectPath(rel));
    TEST_ASSERT_TRUE_MESSAGE(in.good(), rel);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// US-105: the HID side of the commands lives in HidControl, not in the USB host interface
void test_command_logic_left_the_usb_host_interface(void) {
    const char* moved[] = { "resolveShutdownSteps", "resolveWrite", "kFallback", "delay(125)" };
    std::string host = readSource("lib/USBHostUPS/src/IUSBHostUPS.h");
    std::string control = readSource("lib/USBHostUPS/src/HidControl.h");
    for (const char* m : moved) {
        TEST_ASSERT_TRUE_MESSAGE(host.find(m) == std::string::npos, m);
        TEST_ASSERT_TRUE_MESSAGE(control.find(m) != std::string::npos, m);
    }
}

// The control and its port depend on the transport, never on the host or the consumer side
void test_control_headers_include_no_usb_host(void) {
    const char* files[] = { "lib/USBHostUPS/src/HidControl.h", "lib/USBHostUPS/src/IHidControlPort.h",
                            "lib/USBHostUPS/src/IUpsControl.h" };
    const char* forbidden[] = { "#include \"IUSBHostUPS.h\"", "#include \"USBHostUPS.h\"", "#include \"IUpsSource.h\"" };
    for (const char* f : files) {
        std::string text = readSource(f);
        for (const char* inc : forbidden) {
            TEST_ASSERT_TRUE_MESSAGE(text.find(inc) == std::string::npos, (std::string(f) + ": " + inc).c_str());
        }
    }
}

// The hook of a driver's control is protocol-neutral (review of US-105): a Q* control gets
// no HID header through it, and the HID part is reached by hidPort(), never by a cast
void test_control_hook_is_protocol_neutral(void) {
    const char* neutral[] = { "lib/USBHostUPS/src/IUpsControl.h", "lib/USBHostUPS/src/IUpsControlPort.h" };
    const char* forbidden[] = { "#include \"IHidTransport.h\"", "#include \"IHidControlPort.h\"",
                                "#include \"HidControl.h\"", "#include \"HIDUsages.h\"", "#include \"HIDParser.h\"",
                                "#include \"NutTable.h\"", "#include \"CommandCatalog.h\"", "#include \"IUSBHostUPS.h\"",
                                "#include \"USBHostUPS.h\"", "#include \"IUpsSource.h\"" };
    for (const char* f : neutral) {
        std::string text = readSource(f);
        for (const char* inc : forbidden) {
            TEST_ASSERT_TRUE_MESSAGE(text.find(inc) == std::string::npos, (std::string(f) + ": " + inc).c_str());
        }
    }
    // Every source of the library (GCC 5 has no std::filesystem: dirent.h)
    std::vector<std::string> sources;
    DIR* dir = opendir(projectPath("lib/USBHostUPS/src").c_str());
    TEST_ASSERT_NOT_NULL_MESSAGE(dir, "lib/USBHostUPS/src");
    for (struct dirent* e = readdir(dir); e; e = readdir(dir)) {
        std::string name = e->d_name;
        size_t dot = name.rfind('.');
        std::string ext = dot == std::string::npos ? "" : name.substr(dot);
        if (ext == ".h" || ext == ".cpp") sources.push_back("lib/USBHostUPS/src/" + name);
    }
    closedir(dir);
    TEST_ASSERT_TRUE(sources.size() > 20);

    const char* casts[] = { "dynamic_cast", "typeid", "static_cast<IHidControlPort", "static_cast<const IHidControlPort",
                            "reinterpret_cast<IHidControlPort", "reinterpret_cast<const IHidControlPort" };
    const char* owners[] = { "lib/USBHostUPS/src/IUpsControlPort.h", "lib/USBHostUPS/src/IHidControlPort.h",
                             "lib/USBHostUPS/src/HidControl.h" };
    for (const std::string& f : sources) {
        std::string text = readSource(f.c_str());
        for (const char* c : casts) {
            TEST_ASSERT_TRUE_MESSAGE(text.find(c) == std::string::npos, (f + ": " + c).c_str());
        }
        // hidPort() is reserved to HidControl
        bool owner = false;
        for (const char* o : owners) owner = owner || f == o;
        if (!owner) TEST_ASSERT_TRUE_MESSAGE(text.find("hidPort(") == std::string::npos, (f + ": hidPort(").c_str());
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_alone_drives_a_driver);
    RUN_TEST(test_driver_sources_include_no_usb_host);
    RUN_TEST(test_command_logic_left_the_usb_host_interface);
    RUN_TEST(test_control_headers_include_no_usb_host);
    RUN_TEST(test_control_hook_is_protocol_neutral);
    return UNITY_END();
}
