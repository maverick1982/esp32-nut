// US-131 (ADR 0013, section 1): the host reaches the HID hooks of a driver only through
// IUPSDriver::hid() (DriverDispatch.h). A driver without the HID extension is set up, polled
// and controlled without a single HID call, and the INPUT watchdog stays inert for it; an HID
// driver receives every call as before.
#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include <utility>
#include "DriverDispatch.h"
#include "LinkMonitor.h"
#include "IUpsControl.h"
#include "GenericDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "EcoFlowDriver.h"
#include "LegrandDriver.h"
#include "LiebertDriver.h"
#include "OpenUPSDriver.h"
#include "PowercomDriver.h"

// A control of another protocol: never called by these tests, only handed out
class NeutralControl : public IUpsControl {
public:
    std::vector<const UPSCommandInfo*> supportedCommands(const IUpsControlPort&) const override { return {}; }
    CommandResult executeCommand(IUpsControlPort&, const char*, const char*) const override {
        return CommandResult::INVALID_ARGUMENT;
    }
    NutTable::SetStatus setVariable(IUpsControlPort&, const char*, const char*) const override {
        return NutTable::SetStatus::UNKNOWN;
    }
};
static const NeutralControl kNeutralControl;

class FakeTransport : public IHidTransport {
public:
    std::vector<HIDUsageDef> usages;
    HIDParser parser;
    void lock() const override {}
    void unlock() const override {}
    const std::vector<HIDUsageDef>& getUsages() const override { return usages; }
    const HIDParser* getHIDParser() const override { return &parser; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isPollingPaused() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

static const NutTable::HidInfo kSpyRows[1] = {};

/**
 * Records every call, HID and core. With neutral set, hid() returns nullptr: the driver is
 * then, for the host, a driver of another family that happens to implement the HID methods,
 * so any HID call reaching it is a leak of the dispatch.
 */
class SpyHidDriver : public IHidDriver {
public:
    explicit SpyHidDriver(bool neutral_) : neutral(neutral_) {}
    bool neutral;
    bool delay_vars = true;
    mutable std::vector<std::string> hidCalls;
    mutable std::vector<std::string> coreCalls;
    // Last arguments seen by the HID methods
    mutable IHidTransport* lastHost = nullptr;
    mutable const IHidTransport* lastConstHost = nullptr;
    mutable uint8_t lastId = 0, lastType = 0;
    mutable const uint8_t* lastData = nullptr;
    mutable size_t lastLength = 0;
    mutable UPSData* lastUps = nullptr;

    IHidDriver* hid() override { return neutral ? nullptr : this; }
    const IHidDriver* hid() const override { return neutral ? nullptr : this; }

    // Core
    const char* getDriverName() const override { coreCalls.push_back("name"); return "Spy"; }
    void setup() override { coreCalls.push_back("setup"); }
    void poll(UPSData& data, uint32_t now) override {
        coreCalls.push_back("poll");
        IHidDriver::poll(data, now);
    }
    bool initialPollDone() const override { coreCalls.push_back("initialPollDone"); return true; }
    void formatDeviceStrings(UPSData&) override { coreCalls.push_back("formatDeviceStrings"); }
    const IUpsControl& control() const override { coreCalls.push_back("control"); return kNeutralControl; }
    bool delayVariablesSupported() const override { coreCalls.push_back("delayVariablesSupported"); return delay_vars; }
    // Not recorded: the order in which function arguments are evaluated is unspecified
    int32_t defaultOffDelay() const override { return 25; }
    int32_t defaultOnDelay() const override { return 35; }

    // HID
    void loop(IHidTransport* host, UPSData& data, uint32_t) override {
        hidCalls.push_back("loop");
        lastHost = host;
        lastUps = &data;
    }
    void decodeReport(IHidTransport* host, uint8_t id, uint8_t type, const uint8_t* data, size_t length,
                      UPSData& ups) override {
        hidCalls.push_back("decodeReport");
        record(host, id, type, data, length, ups);
    }
    void postDecode(IHidTransport* host, uint8_t id, uint8_t type, const uint8_t* data, size_t length,
                    UPSData& ups) override {
        hidCalls.push_back("postDecode");
        record(host, id, type, data, length, ups);
    }
    void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t* data, size_t length,
                               UPSData& ups) override {
        hidCalls.push_back("parseStringDescriptor");
        record(host, index, 0, data, length, ups);
    }
    NutTable::SetStatus planSet(const UPSData&, const std::vector<HIDUsageDef>&, const char* name, const char*,
                                NutTable::SetPlan& plan) const override {
        hidCalls.push_back(std::string("planSet ") + name);
        plan.name = "spy.var";
        return NutTable::SetStatus::OK;
    }
    uint16_t requestLength(uint8_t type, uint8_t id, uint16_t expected) const override {
        hidCalls.push_back("requestLength");
        lastType = type;
        lastId = id;
        return expected + 100;
    }
    bool acceptDiagReport(uint8_t type, uint8_t id) const override {
        hidCalls.push_back("acceptDiagReport");
        lastType = type;
        lastId = id;
        return true;
    }
    void invalidateReport(uint8_t type, uint8_t id) override {
        hidCalls.push_back("invalidateReport");
        lastType = type;
        lastId = id;
    }
    bool inputValuesPolled() const override { hidCalls.push_back("inputValuesPolled"); return false; }
    uint32_t inputSilenceFaultMs() const override { hidCalls.push_back("inputSilenceFaultMs"); return 15000; }
    uint8_t encodeBeeperValue(bool, uint16_t) const override { hidCalls.push_back("encodeBeeperValue"); return 9; }
    bool beeperControllable() const override { hidCalls.push_back("beeperControllable"); return false; }
    bool beeperMuteSupported() const override { hidCalls.push_back("beeperMuteSupported"); return false; }
    bool beeperFieldRead(const IHidTransport* host) const override {
        hidCalls.push_back("beeperFieldRead");
        lastConstHost = host;
        return true;
    }
    const NutTable::HidInfo* commandTable(size_t& n) const override {
        hidCalls.push_back("commandTable");
        n = 1;
        return kSpyRows;
    }
    const NutTable::DelayPaths* delayPaths() const override {
        hidCalls.push_back("delayPaths");
        return nullptr; // applyDefaultDelays() then sets nothing
    }

private:
    void record(IHidTransport* host, uint8_t id, uint8_t type, const uint8_t* data, size_t length, UPSData& ups) {
        lastHost = host;
        lastId = id;
        lastType = type;
        lastData = data;
        lastLength = length;
        lastUps = &ups;
    }
};

static std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ",") + s;
    return out;
}

void setUp(void) {}
void tearDown(void) {}

// The host's sequence (claimInterface(), poll task, consumers) and every other dispatch:
// not one HID call, and the neutral values
void test_neutral_driver_never_reaches_hid(void) {
    SpyHidDriver drv(true);
    IUPSDriver* d = &drv;
    FakeTransport t;
    UPSData ups;
    std::vector<HIDUsageDef> usages;
    const uint8_t buf[3] = { 1, 2, 3 };

    // Prepared
    DriverDispatch::attach(d, &t);
    d->setup();
    DriverDispatch::applyDefaultDelays(d, ups, usages, nullptr);
    d->formatDeviceStrings(ups);
    // Polled: poll() without a bound transport runs nothing HID
    for (uint32_t now = 0; now < 5000; now += 1000) d->poll(ups, now);
    TEST_ASSERT_TRUE(d->initialPollDone());
    // Controlled
    TEST_ASSERT_TRUE(&d->control() == &kNeutralControl);

    DriverDispatch::decode(d, &t, 1, 1, buf, sizeof(buf), ups);
    DriverDispatch::parseString(d, &t, 3, buf, sizeof(buf), ups);
    TEST_ASSERT_TRUE(DriverDispatch::inputValuesPolled(d));
    TEST_ASSERT_EQUAL_UINT32(0, DriverDispatch::inputSilenceFaultMs(d)); // issue #138: no silence fault
    TEST_ASSERT_FALSE(DriverDispatch::inputWatchdogActive(d, false));
    TEST_ASSERT_EQUAL_UINT16(8, DriverDispatch::requestLength(d, 3, 12, 8));
    TEST_ASSERT_FALSE(DriverDispatch::acceptDiagReport(d, 3, 12));
    DriverDispatch::invalidateReport(d, 3, 12);
    NutTable::SetPlan plan;
    TEST_ASSERT_TRUE(DriverDispatch::planSet(d, ups, usages, "ups.delay.start", "10", plan) ==
                     NutTable::SetStatus::UNKNOWN);
    TEST_ASSERT_NULL(plan.name);
    size_t n = 99;
    TEST_ASSERT_NULL(DriverDispatch::commandTable(d, n));
    TEST_ASSERT_EQUAL(0, (int)n);
    TEST_ASSERT_FALSE(DriverDispatch::beeperFieldRead(d, &t));
    TEST_ASSERT_TRUE(DriverDispatch::beeperControllable(d));
    TEST_ASSERT_TRUE(DriverDispatch::beeperMuteSupported(d));
    TEST_ASSERT_NULL(DriverDispatch::hidOf(d));

    TEST_ASSERT_EQUAL_STRING_MESSAGE("", join(drv.hidCalls).c_str(), "HID calls on a driver without hid()");
    TEST_ASSERT_EQUAL_STRING("setup,delayVariablesSupported,formatDeviceStrings,"
                             "poll,poll,poll,poll,poll,initialPollDone,control",
                             join(drv.coreCalls).c_str());
}

// A null driver (nothing attached) gets the same neutral values, as before for _driver == nullptr
void test_null_driver_neutral_values(void) {
    UPSData ups;
    std::vector<HIDUsageDef> usages;
    size_t n = 5;
    TEST_ASSERT_NULL(DriverDispatch::hidOf((IUPSDriver*)nullptr));
    TEST_ASSERT_NULL(DriverDispatch::commandTable(nullptr, n));
    TEST_ASSERT_EQUAL(0, (int)n);
    TEST_ASSERT_TRUE(DriverDispatch::beeperControllable(nullptr));
    TEST_ASSERT_TRUE(DriverDispatch::beeperMuteSupported(nullptr));
    TEST_ASSERT_FALSE(DriverDispatch::beeperFieldRead(nullptr, nullptr));
    TEST_ASSERT_FALSE(DriverDispatch::inputWatchdogActive(nullptr, false));
    TEST_ASSERT_EQUAL_UINT32(0, DriverDispatch::inputSilenceFaultMs(nullptr));
    DriverDispatch::applyDefaultDelays(nullptr, ups, usages, nullptr);
    TEST_ASSERT_FALSE(ups.hasKey("ups.delay.shutdown"));
}

// Silence of a periodic device: for a driver without hid() the watchdog never makes the data
// stale nor asks for a recovery or a restart
void test_neutral_driver_keeps_input_watchdog_inert(void) {
    SpyHidDriver drv(true);
    InputWatchdog wd;
    wd.reset(0);
    uint32_t t = 0;
    for (int i = 0; i < 10; i++, t += 2000) wd.onInput(t);
    TEST_ASSERT_TRUE(wd.isPeriodic());
    for (uint32_t now = t; now < t + 600000; now += 1000) {
        wd.setValuesPolled(DriverDispatch::inputValuesPolled(&drv));
        TEST_ASSERT_FALSE(wd.isStale(now));
        TEST_ASSERT_TRUE(wd.tick(now) == InputWatchdog::Action::NONE);
    }
    TEST_ASSERT_EQUAL_STRING("", join(drv.hidCalls).c_str());

    // Control: with an HID driver whose INPUT values are not polled the same silence acts
    SpyHidDriver hid(false);
    InputWatchdog wd2;
    wd2.reset(0);
    t = 0;
    for (int i = 0; i < 10; i++, t += 2000) wd2.onInput(t);
    wd2.setValuesPolled(DriverDispatch::inputValuesPolled(&hid));
    TEST_ASSERT_TRUE(wd2.tick(t + wd2.silenceThresholdMs()) == InputWatchdog::Action::RECOVER);
    TEST_ASSERT_TRUE(DriverDispatch::inputWatchdogActive(&hid, false));
    TEST_ASSERT_FALSE(DriverDispatch::inputWatchdogActive(&hid, true)); // QUIRK_NO_INTERRUPT_PIPE
}

// ups.delay.* without HID: both variables when the driver declares them, defaults or saved values
void test_neutral_driver_delays(void) {
    std::vector<HIDUsageDef> usages;
    {
        SpyHidDriver drv(true);
        UPSData ups;
        DriverDispatch::applyDefaultDelays(&drv, ups, usages, nullptr);
        TEST_ASSERT_EQUAL_STRING("25", ups.get("ups.delay.shutdown").c_str());
        TEST_ASSERT_EQUAL_STRING("35", ups.get("ups.delay.start").c_str());
    }
    {
        SpyHidDriver drv(true);
        UPSData ups;
        std::vector<std::pair<String, String>> saved = { { "ups.delay.start", "90" }, { "ups.delay.shutdown", "x" } };
        DriverDispatch::applyDefaultDelays(&drv, ups, usages, &saved);
        TEST_ASSERT_EQUAL_STRING("25", ups.get("ups.delay.shutdown").c_str()); // invalid saved value ignored
        TEST_ASSERT_EQUAL_STRING("90", ups.get("ups.delay.start").c_str());
    }
    {
        SpyHidDriver drv(true);
        drv.delay_vars = false;
        UPSData ups;
        DriverDispatch::applyDefaultDelays(&drv, ups, usages, nullptr);
        TEST_ASSERT_FALSE(ups.hasKey("ups.delay.shutdown"));
        TEST_ASSERT_FALSE(ups.hasKey("ups.delay.start"));
    }
}

// An HID driver receives every call once, with the same arguments, in the same order
void test_hid_driver_forwards_every_call(void) {
    SpyHidDriver drv(false);
    IUPSDriver* d = &drv;
    FakeTransport t;
    UPSData ups;
    std::vector<HIDUsageDef> usages;
    const uint8_t buf[3] = { 4, 5, 6 };

    // poll() before attach() runs nothing; after it loop() runs on the bound transport
    d->poll(ups, 0);
    TEST_ASSERT_EQUAL_STRING("", join(drv.hidCalls).c_str());
    DriverDispatch::attach(d, &t);
    d->poll(ups, 1000);
    TEST_ASSERT_EQUAL_STRING("loop", join(drv.hidCalls).c_str());
    TEST_ASSERT_TRUE(drv.lastHost == &t);
    TEST_ASSERT_TRUE(drv.lastUps == &ups);

    drv.hidCalls.clear();
    DriverDispatch::decode(d, &t, 7, 3, buf, sizeof(buf), ups);
    TEST_ASSERT_EQUAL_STRING("decodeReport,postDecode", join(drv.hidCalls).c_str());
    TEST_ASSERT_EQUAL_UINT8(7, drv.lastId);
    TEST_ASSERT_EQUAL_UINT8(3, drv.lastType);
    TEST_ASSERT_TRUE(drv.lastData == buf);
    TEST_ASSERT_EQUAL(3, (int)drv.lastLength);

    drv.hidCalls.clear();
    DriverDispatch::parseString(d, &t, 2, buf, 2, ups);
    TEST_ASSERT_EQUAL_STRING("parseStringDescriptor", join(drv.hidCalls).c_str());
    TEST_ASSERT_EQUAL_UINT8(2, drv.lastId);
    TEST_ASSERT_EQUAL(2, (int)drv.lastLength);

    drv.hidCalls.clear();
    TEST_ASSERT_FALSE(DriverDispatch::inputValuesPolled(d));
    TEST_ASSERT_EQUAL_UINT16(108, DriverDispatch::requestLength(d, 3, 12, 8));
    TEST_ASSERT_EQUAL_UINT8(12, drv.lastId);
    TEST_ASSERT_TRUE(DriverDispatch::acceptDiagReport(d, 1, 13));
    TEST_ASSERT_EQUAL_UINT8(13, drv.lastId);
    DriverDispatch::invalidateReport(d, 3, 14);
    TEST_ASSERT_EQUAL_UINT8(14, drv.lastId);
    NutTable::SetPlan plan;
    TEST_ASSERT_TRUE(DriverDispatch::planSet(d, ups, usages, "ups.delay.start", "10", plan) == NutTable::SetStatus::OK);
    TEST_ASSERT_EQUAL_STRING("spy.var", plan.name);
    size_t n = 0;
    TEST_ASSERT_TRUE(DriverDispatch::commandTable(d, n) == kSpyRows);
    TEST_ASSERT_EQUAL(1, (int)n);
    TEST_ASSERT_TRUE(DriverDispatch::beeperFieldRead(d, &t));
    TEST_ASSERT_TRUE(drv.lastConstHost == &t);
    TEST_ASSERT_FALSE(DriverDispatch::beeperControllable(d));
    TEST_ASSERT_FALSE(DriverDispatch::beeperMuteSupported(d));
    TEST_ASSERT_TRUE(DriverDispatch::hidOf(d) == &drv);
    TEST_ASSERT_EQUAL_STRING("inputValuesPolled,requestLength,acceptDiagReport,invalidateReport,planSet ups.delay.start,"
                             "commandTable,beeperFieldRead,beeperControllable,beeperMuteSupported",
                             join(drv.hidCalls).c_str());

    // ups.delay.* of an HID driver follow its paths, as NutTable::applyDefaultDelays()
    drv.hidCalls.clear();
    DriverDispatch::applyDefaultDelays(d, ups, usages, nullptr);
    TEST_ASSERT_EQUAL_STRING("delayPaths", join(drv.hidCalls).c_str());
    TEST_ASSERT_FALSE(ups.hasKey("ups.delay.shutdown")); // nullptr paths set nothing
}

// Issue #138: the silence limit of the BX guard comes from the HID extension
void test_hid_driver_forwards_input_silence_fault(void) {
    SpyHidDriver drv(false);
    TEST_ASSERT_EQUAL_UINT32(15000, DriverDispatch::inputSilenceFaultMs(&drv));
    TEST_ASSERT_EQUAL_STRING("inputSilenceFaultMs", join(drv.hidCalls).c_str());
}

// The same ups.delay.* as before through the dispatch, on a real driver and usages
void test_hid_driver_delays_unchanged(void) {
    GenericDriver gen;
    std::vector<HIDUsageDef> usages(1);
    strncpy(usages[0].path, "UPS.PowerSummary.DelayBeforeShutdown", sizeof(usages[0].path) - 1);
    UPSData viaDispatch, direct;
    DriverDispatch::applyDefaultDelays(&gen, viaDispatch, usages, nullptr);
    NutTable::applyDefaultDelays(direct, usages, gen.defaultOffDelay(), gen.defaultOnDelay(),
                                 gen.delayVariablesSupported(), gen.delayPaths(), nullptr);
    TEST_ASSERT_EQUAL_STRING(direct.get("ups.delay.shutdown").c_str(), viaDispatch.get("ups.delay.shutdown").c_str());
    TEST_ASSERT_EQUAL_STRING(direct.get("ups.delay.start").c_str(), viaDispatch.get("ups.delay.start").c_str());
    TEST_ASSERT_EQUAL(direct.hasKey("ups.delay.shutdown"), viaDispatch.hasKey("ups.delay.shutdown"));
}

void test_every_hid_driver_returns_its_extension(void) {
    GenericDriver generic;
    APCDriver apc;
    CyberPowerDriver cps;
    EatonDriver eaton;
    EcoFlowDriver ecoflow;
    LegrandDriver legrand;
    LiebertDriver liebert;
    OpenUPSDriver openups;
    PowercomDriver powercom;
    IUPSDriver* drivers[] = { &generic, &apc, &cps, &eaton, &ecoflow, &legrand, &liebert, &openups, &powercom };
    for (IUPSDriver* d : drivers) {
        TEST_ASSERT_NOT_NULL_MESSAGE(d->hid(), d->getDriverName());
        TEST_ASSERT_TRUE_MESSAGE((void*)d->hid() == (void*)d, d->getDriverName());
        const IUPSDriver* c = d;
        TEST_ASSERT_TRUE_MESSAGE(c->hid() == d->hid(), d->getDriverName());
    }
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_driver_never_reaches_hid);
    RUN_TEST(test_null_driver_neutral_values);
    RUN_TEST(test_neutral_driver_keeps_input_watchdog_inert);
    RUN_TEST(test_neutral_driver_delays);
    RUN_TEST(test_hid_driver_forwards_every_call);
    RUN_TEST(test_hid_driver_forwards_input_silence_fault);
    RUN_TEST(test_hid_driver_delays_unchanged);
    RUN_TEST(test_every_hid_driver_returns_its_extension);
    return UNITY_END();
}
#endif
#endif
