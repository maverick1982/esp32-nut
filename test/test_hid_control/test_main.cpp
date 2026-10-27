// US-105: commands and SET VAR of the HID drivers (HidControl) on a fake port
#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include "HidControl.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "EcoFlowDriver.h"
#include "LegrandDriver.h"
#include "LiebertDriver.h"
#include "OpenUPSDriver.h"
#include "PowercomDriver.h"
#include "IUSBHostUPS.h"

using NutTable::SetPlan;
using NutTable::SetStatus;

// The host as HidControl sees it: lock depth, writes, beeper and SET VAR steps recorded
class FakePort : public IHidControlPort {
public:
    UPSData data;
    std::vector<HIDUsageDef> usages;
    HIDParser parser;
    bool connected = true;
    bool beeperToggle = true;
    bool shutdownCmds = true;
    bool muteAllowed = true;
    const char* const* allowList = nullptr;

    mutable int depth = 0;
    mutable int lockCalls = 0;
    void lock() const override {
        depth++;
        lockCalls++;
    }
    void unlock() const override { depth--; }

    // Device I/O: {what, value, lock depth at the call}
    struct Io {
        std::string what;
        int32_t value;
        int depth;
    };
    std::vector<Io> io;
    bool beeperOk = true;
    std::vector<std::string> failPaths;
    bool setBeeper(bool enable) override {
        io.push_back({ "beeper", enable ? 1 : 0, depth });
        return beeperOk;
    }
    bool writeUsage(const HIDUsageDef& def, uint32_t value) override {
        io.push_back({ def.path, (int32_t)value, depth });
        return std::find(failPaths.begin(), failPaths.end(), def.path) == failPaths.end();
    }

    // Commands run through the port, as the host does for the shutdown.default attempts
    std::vector<std::string> executed;
    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        executed.push_back(name ? name : "(null)");
        return HidControl::instance().executeCommand(*this, name, param);
    }
    std::string defaultVia = "(none)";
    void noteDefaultShutdown(const char* via) override { defaultVia = via; }

    // SET VAR steps
    SetStatus planResult = SetStatus::OK;
    SetPlan::Kind planKind = SetPlan::Kind::HID;
    int planCalls = 0, saveCalls = 0, writeCalls = 0, resetCalls = 0;
    SetStatus saveResult = SetStatus::OK;
    SetStatus writeResult = SetStatus::OK;
    SetStatus resetResult = SetStatus::OK;
    NutTable::SetStatus planSet(const char* name, const char* value, SetPlan& plan) const override {
        const_cast<FakePort*>(this)->planCalls++;
        if (planResult != SetStatus::OK) return planResult;
        plan.kind = planKind;
        plan.name = "ups.delay.shutdown";
        plan.value = value;
        return SetStatus::OK;
    }
    SetStatus saveDriverSideValue(const SetPlan&) override {
        saveCalls++;
        return saveResult;
    }
    SetStatus writePlannedValue(const SetPlan&) override {
        writeCalls++;
        return writeResult;
    }
    SetStatus resetDriverSideValue(const char*, String& value) override {
        resetCalls++;
        value = "20";
        return resetResult;
    }

    bool isConnected() const override { return connected; }
    // Read only under the data lock: the parser and UPSData are replaced at reconnection
    const UPSData& upsData() const override {
        TEST_ASSERT_TRUE_MESSAGE(depth > 0, "upsData() without the lock");
        return data;
    }
    bool supportsBeeperToggle() const override { return beeperToggle; }
    bool shutdownCommandsSupported() const override { return shutdownCmds; }
    bool beeperMuteSupported() const override { return muteAllowed; }
    const char* const* commandAllowList() const override { return allowList; }
    const std::vector<HIDUsageDef>& getUsages() const override {
        TEST_ASSERT_TRUE_MESSAGE(depth > 0, "getUsages() without the lock");
        return usages;
    }
    const HIDParser* getHIDParser() const override { return &parser; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

static const HidControl& control() { return HidControl::instance(); }

static HIDUsageDef featureUsage(const char* path, uint32_t usage) {
    HIDUsageDef d;
    d.usage = usage;
    d.report_type = 0x03;
    d.found = true;
    strncpy(d.path, path, sizeof(d.path) - 1);
    return d;
}

#define P_SD "UPS.PowerSummary.DelayBeforeShutdown"
#define P_ST "UPS.PowerSummary.DelayBeforeStartup"
#define P_RB "UPS.PowerSummary.DelayBeforeReboot"
#define P_TEST "UPS.BatterySystem.Battery.Test"
#define P_BEEPER "UPS.PowerSummary.AudibleAlarmControl"

// Delay fields 16 bits wide like the Eaton 3S, ups.delay.* with the NUT defaults 20/30
static void addDelayUsages(FakePort& port, bool shutdown, bool startup, bool reboot) {
    auto add = [&](const char* path, uint32_t usage) {
        HIDUsageDef d = featureUsage(path, usage);
        d.report_id = 0x11;
        d.bit_size = 16;
        port.usages.push_back(d);
    };
    if (shutdown) add(P_SD, 0x00840057);
    if (startup) add(P_ST, 0x00840056);
    if (reboot) add(P_RB, 0x00840055);
    CommandCatalog::applyDefaultDelays(port.data, port.usages, 20, 30, true);
}

static void addBeeper(FakePort& port, const char* status) {
    HIDUsageDef b = featureUsage(P_BEEPER, 0x0084005a);
    b.bit_size = 8;
    b.logical_min = 1;
    b.logical_max = 3;
    port.usages.push_back(b);
    port.data.set("ups.beeper.status", status);
}

static std::vector<std::string> names(const std::vector<const UPSCommandInfo*>& cmds) {
    std::vector<std::string> out;
    for (const auto* c : cmds) out.push_back(c->name);
    return out;
}

static std::string ioStr(const FakePort& port) {
    std::string out;
    for (const auto& w : port.io) {
        if (!out.empty()) out += " ";
        out += w.what + "=" + std::to_string(w.value);
    }
    return out;
}

void setUp(void) { mock_delay_total_ms() = 0; }
void tearDown(void) {}

// The list is CommandCatalog::build() with the flags of the port
void test_supported_commands_match_catalog(void) {
    FakePort port;
    addDelayUsages(port, true, true, true);
    addBeeper(port, "enabled");
    port.usages.push_back(featureUsage(P_TEST, 0x00840058));

    auto expected = names(CommandCatalog::build(port.usages, true, true, true, nullptr));
    TEST_ASSERT_TRUE(expected.size() > 5);
    TEST_ASSERT_TRUE(expected == names(control().supportedCommands(port)));

    static const char* const allow[] = { "test.battery.start", "beeper.toggle", nullptr };
    port.muteAllowed = false;
    port.shutdownCmds = false;
    port.allowList = allow;
    expected = names(CommandCatalog::build(port.usages, true, false, false, allow));
    TEST_ASSERT_TRUE(expected == names(control().supportedCommands(port)));

    // No beeper toggle, or no ups.beeper.status: no beeper commands
    port.allowList = nullptr;
    port.beeperToggle = false;
    expected = names(CommandCatalog::build(port.usages, false, false, false, nullptr));
    TEST_ASSERT_TRUE(expected == names(control().supportedCommands(port)));
    TEST_ASSERT_EQUAL(0, port.depth);
}

void test_no_commands_when_disconnected(void) {
    FakePort port;
    addDelayUsages(port, true, true, false);
    port.connected = false;
    TEST_ASSERT_EQUAL(0, control().supportedCommands(port).size());
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)control().executeCommand(port, "load.off", nullptr));
    TEST_ASSERT_EQUAL(0, port.io.size());
}

// beeper.toggle enables a disabled beeper and disables an enabled or muted one
void test_beeper_toggle_reads_status(void) {
    struct Case { const char* status; int enable; };
    const Case cases[] = { { "disabled", 1 }, { "enabled", 0 }, { "muted", 0 } };
    for (const Case& c : cases) {
        FakePort port;
        addBeeper(port, c.status);
        TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "beeper.toggle", nullptr));
        TEST_ASSERT_EQUAL_STRING_MESSAGE((std::string("beeper=") + std::to_string(c.enable)).c_str(),
                                         ioStr(port).c_str(), c.status);
    }

    FakePort port;
    addBeeper(port, "enabled");
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "beeper.disable", nullptr));
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "beeper.enable", nullptr));
    TEST_ASSERT_EQUAL_STRING("beeper=0 beeper=1", ioStr(port).c_str());
    port.beeperOk = false;
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)control().executeCommand(port, "beeper.toggle", nullptr));
    // beeper.mute writes 3 on the beeper field
    port.io.clear();
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "beeper.mute", nullptr));
    TEST_ASSERT_EQUAL_STRING(P_BEEPER "=3", ioStr(port).c_str());
}

// usbhid-ups order of the writes, with 125 ms between two writes of a sequence
void test_shutdown_sequences_and_pause(void) {
    FakePort port;
    addDelayUsages(port, true, true, true);

    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "shutdown.return", nullptr));
    TEST_ASSERT_EQUAL_STRING(P_ST "=30 " P_SD "=20", ioStr(port).c_str());
    TEST_ASSERT_EQUAL_UINT32(125, mock_delay_total_ms());

    port.io.clear();
    mock_delay_total_ms() = 0;
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "shutdown.stayoff", nullptr));
    TEST_ASSERT_EQUAL_STRING(P_ST "=-1 " P_SD "=20", ioStr(port).c_str());
    TEST_ASSERT_EQUAL_UINT32(125, mock_delay_total_ms());

    // One write: no pause
    port.io.clear();
    mock_delay_total_ms() = 0;
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "load.off.delay", "45"));
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "shutdown.stop", nullptr));
    TEST_ASSERT_EQUAL_STRING(P_SD "=45 " P_SD "=-1", ioStr(port).c_str());
    TEST_ASSERT_EQUAL_UINT32(0, mock_delay_total_ms());

    // The first write fails: the second is not sent
    port.io.clear();
    port.failPaths = { P_ST };
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)control().executeCommand(port, "shutdown.return", nullptr));
    TEST_ASSERT_EQUAL_STRING(P_ST "=30", ioStr(port).c_str());
}

// shutdown.default tries the upsdrv_shutdown() commands in order through the port
void test_shutdown_default_fallback(void) {
    FakePort port;
    addDelayUsages(port, true, true, true);
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(port, "shutdown.default", nullptr));
    TEST_ASSERT_EQUAL(1, port.executed.size());
    TEST_ASSERT_EQUAL_STRING("shutdown.return", port.executed[0].c_str());
    TEST_ASSERT_EQUAL_STRING("shutdown.return", port.defaultVia.c_str());

    // shutdown.return and shutdown.reboot fail: load.off.delay is next
    FakePort p2;
    addDelayUsages(p2, true, true, true);
    p2.failPaths = { P_ST, P_RB };
    TEST_ASSERT_EQUAL((int)CommandResult::OK, (int)control().executeCommand(p2, "shutdown.default", nullptr));
    std::vector<std::string> expected = { "shutdown.return", "shutdown.reboot", "load.off.delay" };
    TEST_ASSERT_TRUE(expected == p2.executed);
    TEST_ASSERT_EQUAL_STRING("load.off.delay", p2.defaultVia.c_str());

    // Commands the device does not offer are skipped; all failing: FAILED, nothing noted
    FakePort p3;
    addDelayUsages(p3, true, false, false);
    p3.failPaths = { P_SD };
    TEST_ASSERT_EQUAL((int)CommandResult::FAILED, (int)control().executeCommand(p3, "shutdown.default", nullptr));
    expected = { "load.off.delay" };
    TEST_ASSERT_TRUE(expected == p3.executed);
    TEST_ASSERT_EQUAL_STRING("", p3.defaultVia.c_str());
}

void test_invalid_and_unsupported_commands(void) {
    FakePort port;
    addDelayUsages(port, true, true, false);
    TEST_ASSERT_EQUAL((int)CommandResult::INVALID_ARGUMENT,
                      (int)control().executeCommand(port, "load.off.delay", "abc"));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, "test.panel.start", nullptr));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, "shutdown.reboot", nullptr));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, "no.such.cmd", nullptr));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, nullptr, nullptr));
    TEST_ASSERT_EQUAL(0, port.io.size());

    // A configured delay that is not a number: the commands that need it are not run
    port.data.set("ups.delay.shutdown", "soon");
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, "load.off.delay", nullptr));
    TEST_ASSERT_EQUAL(0, port.io.size());
}

// ADR 0008: the data lock is never held while the port talks to the device
void test_no_lock_held_during_device_io(void) {
    FakePort port;
    addDelayUsages(port, true, true, true);
    addBeeper(port, "disabled");
    port.usages.push_back(featureUsage(P_TEST, 0x00840058));
    const char* cmds[] = { "beeper.toggle", "beeper.mute", "shutdown.return", "shutdown.default",
                           "test.battery.start.quick", "load.on" };
    for (const char* c : cmds) {
        TEST_ASSERT_EQUAL_MESSAGE((int)CommandResult::OK, (int)control().executeCommand(port, c, nullptr), c);
    }
    TEST_ASSERT_TRUE(port.io.size() >= 8);
    for (const auto& w : port.io) TEST_ASSERT_EQUAL_MESSAGE(0, w.depth, w.what.c_str());
    TEST_ASSERT_TRUE(port.lockCalls > 0);
    TEST_ASSERT_EQUAL(0, port.depth);
}

// SET VAR: the plan of the driver, then the driver-side store or the HID write
void test_set_variable_routes_the_plan(void) {
    FakePort port;
    TEST_ASSERT_EQUAL((int)SetStatus::OK, (int)control().setVariable(port, "ups.delay.shutdown", "30"));
    TEST_ASSERT_EQUAL(1, port.writeCalls);
    TEST_ASSERT_EQUAL(0, port.saveCalls);

    port.planKind = SetPlan::Kind::DRIVER_SIDE;
    port.saveResult = SetStatus::FAILED;
    TEST_ASSERT_EQUAL((int)SetStatus::FAILED, (int)control().setVariable(port, "ups.delay.shutdown", "30"));
    TEST_ASSERT_EQUAL(1, port.saveCalls);
    TEST_ASSERT_EQUAL(1, port.writeCalls);

    port.planKind = SetPlan::Kind::HID;
    port.writeResult = SetStatus::FAILED;
    TEST_ASSERT_EQUAL((int)SetStatus::FAILED, (int)control().setVariable(port, "battery.charge.low", "30"));
    TEST_ASSERT_EQUAL(2, port.writeCalls);

    // A plan that is not OK is returned as is, with nothing written
    const SetStatus plans[] = { SetStatus::UNKNOWN, SetStatus::CONVERSION_FAILED, SetStatus::NOT_CONNECTED };
    for (SetStatus s : plans) {
        port.planResult = s;
        TEST_ASSERT_EQUAL((int)s, (int)control().setVariable(port, "x", "1"));
    }
    TEST_ASSERT_EQUAL(1, port.saveCalls);
    TEST_ASSERT_EQUAL(2, port.writeCalls);

    // Checks before the plan
    int plans_before = port.planCalls;
    TEST_ASSERT_EQUAL((int)SetStatus::CONVERSION_FAILED, (int)control().setVariable(port, "x", nullptr));
    TEST_ASSERT_EQUAL((int)SetStatus::UNKNOWN, (int)control().setVariable(port, nullptr, "1"));
    port.connected = false;
    TEST_ASSERT_EQUAL((int)SetStatus::NOT_CONNECTED, (int)control().setVariable(port, "x", "1"));
    TEST_ASSERT_EQUAL(plans_before, port.planCalls);
}

// Reset: the default of IUpsControl hands the driver-side variable to the port
void test_reset_variable_default(void) {
    FakePort port;
    String value = "old";
    TEST_ASSERT_EQUAL((int)SetStatus::OK, (int)control().resetVariable(port, "ups.delay.shutdown", value));
    TEST_ASSERT_EQUAL_STRING("20", value.c_str());
    TEST_ASSERT_EQUAL(1, port.resetCalls);

    port.resetResult = SetStatus::UNKNOWN;
    TEST_ASSERT_EQUAL((int)SetStatus::UNKNOWN, (int)control().resetVariable(port, "outlet.desc", value));

    value = "old";
    TEST_ASSERT_EQUAL((int)SetStatus::UNKNOWN, (int)control().resetVariable(port, nullptr, value));
    TEST_ASSERT_EQUAL_STRING("", value.c_str());
    port.connected = false;
    value = "old";
    TEST_ASSERT_EQUAL((int)SetStatus::NOT_CONNECTED, (int)control().resetVariable(port, "ups.delay.shutdown", value));
    TEST_ASSERT_EQUAL_STRING("", value.c_str());
    TEST_ASSERT_EQUAL(2, port.resetCalls);
}

// Every HID driver hands out the same static HidControl
void test_every_hid_driver_returns_hid_control(void) {
    GenericDriver generic;
    APCDriver apc;
    CyberPowerDriver cps;
    EatonDriver eaton;
    EcoFlowDriver ecoflow;
    LegrandDriver legrand;
    LiebertDriver liebert;
    OpenUPSDriver openups;
    PowercomDriver powercom;
    const IUPSDriver* drivers[] = { &generic, &apc, &cps, &eaton, &ecoflow, &legrand, &liebert, &openups, &powercom };
    for (const IUPSDriver* d : drivers) {
        TEST_ASSERT_TRUE_MESSAGE(&d->control() == &HidControl::instance(), d->getDriverName());
    }
}

// A control of another protocol: records what reaches it
class RecordingControl : public IUpsControl {
public:
    mutable std::vector<std::string> calls;
    std::vector<const UPSCommandInfo*> supportedCommands(const IUpsControlPort&) const override {
        calls.push_back("list");
        return { CommandCatalog::find("beeper.toggle") };
    }
    CommandResult executeCommand(IUpsControlPort&, const char* name, const char* param) const override {
        calls.push_back(std::string("cmd ") + name + (param ? std::string(" ") + param : ""));
        return CommandResult::INVALID_ARGUMENT;
    }
    SetStatus setVariable(IUpsControlPort&, const char* name, const char* value) const override {
        calls.push_back(std::string("set ") + name + "=" + value);
        return SetStatus::CONVERSION_FAILED;
    }
    SetStatus resetVariable(IUpsControlPort&, const char* name, String& value) const override {
        calls.push_back(std::string("reset ") + name);
        value = "7";
        return SetStatus::FAILED;
    }
};

// A host whose driver hands out its own control (activeControl() of USBHostUPS)
class HookHost : public IUSBHostUPS {
public:
    const IUpsControl* control = nullptr;
    UPSData data;
    std::vector<HIDUsageDef> usages;
    HIDParser parser;
    const IUpsControl& activeControl() const override { return *control; }
    void lock() const override {}
    void unlock() const override {}
    bool isConnected() const override { return true; }
    UPSDataLock getUPSData() const override { return UPSDataLock(data, this); }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    const std::vector<HIDUsageDef>& getUsages() const override { return usages; }
    const HIDParser* getHIDParser() const override { return &parser; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

// The consumer API of the host goes through the driver's control, whatever its protocol
void test_host_goes_through_the_driver_control(void) {
    RecordingControl rec;
    HookHost host;
    host.control = &rec;
    IUpsSource& source = host;

    auto cmds = source.getSupportedCommands();
    TEST_ASSERT_EQUAL(1, cmds.size());
    TEST_ASSERT_EQUAL_STRING("beeper.toggle", cmds[0]->name);
    TEST_ASSERT_EQUAL((int)CommandResult::INVALID_ARGUMENT, (int)source.executeCommand("load.off.delay", "5"));
    TEST_ASSERT_EQUAL((int)SetStatus::CONVERSION_FAILED, (int)source.setVariable("ups.delay.start", "9"));
    TEST_ASSERT_EQUAL((int)SetStatus::FAILED, (int)source.resetVariable("ups.delay.start"));
    std::vector<std::string> expected = { "list", "cmd load.off.delay 5", "set ups.delay.start=9", "reset ups.delay.start" };
    TEST_ASSERT_TRUE(expected == rec.calls);
}

// A host of another protocol: the neutral port only, no HID part
class NeutralOnlyPort : public IUpsControlPort {
public:
    bool connected = true;
    int beeperCalls = 0;
    void lock() const override {}
    void unlock() const override {}
    bool isConnected() const override { return connected; }
    const UPSData& upsData() const override { return data; }
    bool setBeeper(bool) override {
        beeperCalls++;
        return true;
    }
    CommandResult executeCommand(const char*, const char*) override { return CommandResult::OK; }
    UPSData data;
};

// HidControl paired with a port without HID part: nothing supported, nothing written
void test_hid_control_needs_the_hid_part(void) {
    NeutralOnlyPort port;
    port.data.set("ups.beeper.status", "disabled");
    TEST_ASSERT_EQUAL(0, control().supportedCommands(port).size());
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, "beeper.toggle", nullptr));
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_SUPPORTED, (int)control().executeCommand(port, "shutdown.default", nullptr));
    TEST_ASSERT_EQUAL((int)SetStatus::UNKNOWN, (int)control().setVariable(port, "ups.delay.shutdown", "30"));
    TEST_ASSERT_EQUAL(0, port.beeperCalls);

    // The connection is checked first, as with a HID port
    port.connected = false;
    TEST_ASSERT_EQUAL((int)CommandResult::NOT_CONNECTED, (int)control().executeCommand(port, "beeper.toggle", nullptr));
    TEST_ASSERT_EQUAL((int)SetStatus::NOT_CONNECTED, (int)control().setVariable(port, "ups.delay.shutdown", "30"));
}

// The HID port is its own HID part
void test_hid_port_is_its_hid_part(void) {
    FakePort port;
    const FakePort& cport = port;
    IUpsControlPort& neutral = port;
    TEST_ASSERT_TRUE(port.hidPort() == &port);
    TEST_ASSERT_TRUE(cport.hidPort() == &port);
    TEST_ASSERT_TRUE(neutral.hidPort() == &port);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_supported_commands_match_catalog);
    RUN_TEST(test_no_commands_when_disconnected);
    RUN_TEST(test_beeper_toggle_reads_status);
    RUN_TEST(test_shutdown_sequences_and_pause);
    RUN_TEST(test_shutdown_default_fallback);
    RUN_TEST(test_invalid_and_unsupported_commands);
    RUN_TEST(test_no_lock_held_during_device_io);
    RUN_TEST(test_set_variable_routes_the_plan);
    RUN_TEST(test_reset_variable_default);
    RUN_TEST(test_every_hid_driver_returns_hid_control);
    RUN_TEST(test_host_goes_through_the_driver_control);
    RUN_TEST(test_hid_control_needs_the_hid_part);
    RUN_TEST(test_hid_port_is_its_hid_part);
    return UNITY_END();
}
