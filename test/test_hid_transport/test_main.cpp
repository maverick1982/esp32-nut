// US-104: the UPS drivers depend on IHidTransport only, never on the USB host.
#include <unity.h>
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

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_alone_drives_a_driver);
    RUN_TEST(test_driver_sources_include_no_usb_host);
    return UNITY_END();
}
