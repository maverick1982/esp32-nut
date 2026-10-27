// IUpsControl with a host of another protocol (US-105): a control and its port built from
// IUpsControl.h alone, without the HID transport, the usages or the HID SET VAR plan. The
// #error checks below are the main proof: this file does not compile if the hook brings in
// a HID header.
#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include "IUpsControl.h"

// The control hook must not bring in the HID side (review of US-105)
#ifdef I_HID_TRANSPORT_H
#error "IUpsControl.h must not include IHidTransport.h"
#endif
#ifdef I_HID_CONTROL_PORT_H
#error "IUpsControl.h must not include IHidControlPort.h"
#endif
#ifdef HID_CONTROL_H
#error "IUpsControl.h must not include HidControl.h"
#endif
#ifdef HID_USAGES_H
#error "IUpsControl.h must not include HIDUsages.h"
#endif
#ifdef HID_PARSER_H
#error "IUpsControl.h must not include HIDParser.h"
#endif
#ifdef NUT_TABLE_H
#error "IUpsControl.h must not include NutTable.h"
#endif
#ifdef COMMAND_CATALOG_H
#error "IUpsControl.h must not include CommandCatalog.h"
#endif

using NutTable::SetStatus;

// A host of another protocol: records what the control asks
class NeutralPort : public IUpsControlPort {
public:
    bool connected = true;
    UPSData data;
    int beeperCalls = 0;
    std::vector<std::string> resetNames;
    SetStatus resetResult = SetStatus::OK;
    String resetValue = "20";

    void lock() const override {}
    void unlock() const override {}
    bool isConnected() const override { return connected; }
    const UPSData& upsData() const override { return data; }
    bool setBeeper(bool) override {
        beeperCalls++;
        return true;
    }
    CommandResult executeCommand(const char*, const char*) override { return CommandResult::OK; }
    SetStatus resetDriverSideValue(const char* name, String& value) override {
        resetNames.push_back(name);
        value = resetValue;
        return resetResult;
    }
};

// The smallest control of another protocol: written against the neutral port only
class QxLikeControl : public IUpsControl {
public:
    std::vector<const UPSCommandInfo*> supportedCommands(const IUpsControlPort& port) const override {
        if (!port.isConnected()) return {};
        return { CommandCatalog::find("beeper.enable") };
    }
    CommandResult executeCommand(IUpsControlPort& port, const char*, const char*) const override {
        return port.setBeeper(true) ? CommandResult::OK : CommandResult::FAILED;
    }
    SetStatus setVariable(IUpsControlPort&, const char*, const char*) const override { return SetStatus::UNKNOWN; }
};

void setUp(void) {}
void tearDown(void) {}

void test_neutral_port_has_no_hid_part(void) {
    NeutralPort port;
    const NeutralPort& cport = port;
    TEST_ASSERT_NULL(port.hidPort());
    TEST_ASSERT_NULL(cport.hidPort());
}

// Compile proof: a non-HID control and port plug into the hook and are called through it
void test_control_of_another_protocol_plugs_into_the_hook(void) {
    NeutralPort port;
    QxLikeControl qx;
    const IUpsControl& control = qx;
    IUpsControlPort& neutral = port;
    TEST_ASSERT_EQUAL(1, (int)control.supportedCommands(neutral).size());
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control.executeCommand(neutral, "beeper.enable", nullptr));
    TEST_ASSERT_EQUAL(1, port.beeperCalls);
}

void test_default_reset_goes_through_the_neutral_port(void) {
    NeutralPort port;
    QxLikeControl control;
    String value = "stale";
    TEST_ASSERT_EQUAL((int)SetStatus::OK, (int)control.resetVariable(port, "ups.delay.shutdown", value));
    TEST_ASSERT_EQUAL_STRING("20", value.c_str());
    TEST_ASSERT_EQUAL(1, (int)port.resetNames.size());
    TEST_ASSERT_EQUAL_STRING("ups.delay.shutdown", port.resetNames[0].c_str());

    port.resetResult = SetStatus::FAILED;
    TEST_ASSERT_EQUAL((int)SetStatus::FAILED, (int)control.resetVariable(port, "ups.delay.start", value));
}

void test_default_reset_checks_before_the_port(void) {
    NeutralPort port;
    QxLikeControl control;
    String value = "stale";
    TEST_ASSERT_EQUAL((int)SetStatus::UNKNOWN, (int)control.resetVariable(port, nullptr, value));
    TEST_ASSERT_EQUAL_STRING("", value.c_str());

    port.connected = false;
    value = "stale";
    TEST_ASSERT_EQUAL((int)SetStatus::NOT_CONNECTED, (int)control.resetVariable(port, "ups.delay.shutdown", value));
    TEST_ASSERT_EQUAL_STRING("", value.c_str());
    TEST_ASSERT_EQUAL(0, (int)port.resetNames.size());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_port_has_no_hid_part);
    RUN_TEST(test_control_of_another_protocol_plugs_into_the_hook);
    RUN_TEST(test_default_reset_goes_through_the_neutral_port);
    RUN_TEST(test_default_reset_checks_before_the_port);
    return UNITY_END();
}
