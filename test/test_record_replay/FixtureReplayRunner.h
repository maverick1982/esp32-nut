#ifndef FIXTURE_REPLAY_RUNNER_H
#define FIXTURE_REPLAY_RUNNER_H

#include <unity.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ArduinoJson.h>

#include "IUSBHostUPS.h"
#include "HIDParser.h"
#include "Quirks.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "PowercomDriver.h"
#include "GenericDriver.h"
#include "OpenUPSDriver.h"
#include "DriverRegistry.h"
#include "CommandCatalog.h"
#include "BeeperLogic.h"

class ReplayMockHost : public IUSBHostUPS {
public:
    UPSData _data;
    HIDParser _parser;
    uint32_t _quirks = 0;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }
    String getUPSStatusString() const override { return UPSData::computeUPSStatusString(_data); }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _parser.getUsageDef(usage); }
    String getActiveBeeperPath() const override {
        for (const auto& u : _parser.getUsages()) {
            if (strcmp(u.path, "UPS.PowerSummary.AudibleAlarmControl") == 0 ||
                strcmp(u.path, "UPS.BatterySystem.Battery.AudibleAlarmControl") == 0 ||
                strcmp(u.path, "UPS.AudibleAlarmControl") == 0) {
                return u.path;
            }
        }
        return "";
    }
    uint32_t getQuirks() const override { return _quirks; }
    bool isPollingPaused() const override { return false; }
    bool supportsBeeperToggle() const override {
        if (_quirks & QUIRK_NO_BEEPER_CONTROL) return false;
        return _parser.hasFeatureBeeperControl();
    }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t) override {
        _requestedReports.push_back({report_id, report_type});
        return true;
    }
    bool requestStringDescriptor(uint8_t index) override {
        _requestedStrings.push_back(index);
        return true;
    }
};

// One report of the fixture, as replayed (US-067)
struct ReplayedReport {
    uint8_t id;
    uint8_t type;
    std::vector<uint8_t> data;
};

// A fixture after the replay: the driver with its state, the data and the reports in
// replay order (US-067)
struct ReplaySession {
    ReplayMockHost host;
    UPSData ups_data;
    IUPSDriver* driver = nullptr;
    std::vector<ReplayedReport> reports;

    ReplaySession() {}
    ~ReplaySession() { delete driver; }
    ReplaySession(const ReplaySession&) = delete;
    ReplaySession& operator=(const ReplaySession&) = delete;
};

class FixtureReplayRunner {
public:
    static std::vector<uint8_t> encodeUtf16Descriptor(const std::string& str) {
        std::vector<uint8_t> desc;
        uint8_t len = (uint8_t)(2 + (str.length() * 2));
        desc.push_back(len);
        desc.push_back(0x03); // String Descriptor Type
        for (char c : str) {
            desc.push_back((uint8_t)c);
            desc.push_back(0x00);
        }
        return desc;
    }

    // LIST RW as the NUT server builds it: "name:aux" in the order of writableVariables()
    // (strcasecmp, as upsd), space separated (US-067)
    static std::string listRW(const IUPSDriver* driver, const UPSData& data) {
        std::vector<NutTable::RWVariable> vars;
        driver->writableVariables(data, vars);
        std::string out;
        for (const auto& v : vars) {
            char aux[8];
            snprintf(aux, sizeof(aux), "%u", (unsigned)v.aux);
            if (!out.empty()) out += " ";
            out += v.name;
            out += ":";
            out += aux;
        }
        return out;
    }

    // Loads the fixture, dispatches the driver and replays every scenario, checking the
    // fixture's own expectations. False (test already failed) when the fixture is unusable.
    static bool replay(const char* filePath, ReplaySession& s) {
        std::ifstream f(filePath);
        if (!f.is_open()) {
            std::string msg = "Cannot open fixture file: ";
            msg += filePath;
            TEST_FAIL_MESSAGE(msg.c_str());
            return false;
        }

        std::stringstream buffer;
        buffer << f.rdbuf();
        std::string jsonContent = buffer.str();

        // Strip UTF-8 BOM if present
        if (jsonContent.size() >= 3 &&
            (unsigned char)jsonContent[0] == 0xEF &&
            (unsigned char)jsonContent[1] == 0xBB &&
            (unsigned char)jsonContent[2] == 0xBF) {
            jsonContent = jsonContent.substr(3);
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, jsonContent);
        if (err) {
            std::string msg = "JSON Parse error in fixture: ";
            msg += err.c_str();
            TEST_FAIL_MESSAGE(msg.c_str());
            return false;
        }

        // 1. Read Device Meta
        std::string vidStr = doc["vid"].as<std::string>();
        uint16_t vid = (uint16_t)strtol(vidStr.c_str(), nullptr, 16);
        std::string pidStr = doc["pid"] | "0x0000";
        uint16_t pid = (uint16_t)strtol(pidStr.c_str(), nullptr, 16);

        ReplayMockHost& host = s.host;
        UPSData& ups_data = s.ups_data;

        host._iManufacturer = doc["device_desc"]["iManufacturer"] | 0;
        host._iProduct = doc["device_desc"]["iProduct"] | 0;
        host._iSerialNumber = doc["device_desc"]["iSerialNumber"] | 0;

        if (doc["manufacturer"].is<const char*>()) {
            std::string mfr = doc["manufacturer"].as<const char*>();
            if (!mfr.empty()) ups_data.set("ups.mfr", mfr);
        }
        if (doc["product"].is<const char*>()) {
            std::string prod = doc["product"].as<const char*>();
            if (!prod.empty()) ups_data.set("ups.model", prod);
        }
        if (doc["serial_number"].is<const char*>()) {
            std::string ser = doc["serial_number"].as<const char*>();
            if (!ser.empty()) ups_data.set("ups.serial", ser);
        }

        // 2. Parse HID Report Descriptor
        std::vector<uint8_t> rawDesc;
        JsonArray descArray = doc["report_descriptor_hex"].as<JsonArray>();
        for (JsonVariant v : descArray) {
            std::string bStr = v.as<std::string>();
            uint8_t b = (uint8_t)strtol(bStr.c_str(), nullptr, 16);
            rawDesc.push_back(b);
        }

        TEST_ASSERT_GREATER_THAN_MESSAGE(0, rawDesc.size(), "Report descriptor cannot be empty");
        host._parser.parseReportDescriptor(rawDesc.data(), rawDesc.size());
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, host._parser.getUsages().size(), "Parser found 0 usages from descriptor");

        // 3. Dispatch Driver
        s.driver = DriverRegistry::create(vid, pid);
        IUPSDriver* driver = s.driver;

        // Match quirks
        host._quirks = 0;
        for (int q = 0; UPS_QUIRKS[q].vid != 0; q++) {
            if (UPS_QUIRKS[q].vid == vid && (UPS_QUIRKS[q].pid == 0xFFFF || UPS_QUIRKS[q].pid == pid)) {
                host._quirks |= UPS_QUIRKS[q].flags;
            }
        }

        // Validate supportsBeeperToggle: if Powercom (VID 0x0D9F, PID 0x0004), quirk suppresses toggle
        if (vid == 0x0D9F && pid == 0x0004) {
            TEST_ASSERT_FALSE_MESSAGE(host.supportsBeeperToggle(), "Powercom SPD-750U must have supportsBeeperToggle == false due to quirk");
        } else if (host._parser.hasFeatureBeeperControl()) {
            TEST_ASSERT_TRUE_MESSAGE(host.supportsBeeperToggle(), "Device with Feature beeper and no quirk must have supportsBeeperToggle == true");
        }

        TEST_ASSERT_NOT_NULL_MESSAGE(driver, "Failed to instantiate driver for fixture");
        driver->setup();

        // 4. Inject String Descriptors
        JsonObject stringsObj = doc["strings"].as<JsonObject>();
        for (JsonPair kv : stringsObj) {
            uint8_t idx = (uint8_t)atoi(kv.key().c_str());
            std::string sVal = kv.value().as<std::string>();
            std::vector<uint8_t> sDesc = encodeUtf16Descriptor(sVal);
            driver->parseStringDescriptor(&host, idx, sDesc.data(), sDesc.size(), ups_data);
        }
        // As USBHostUPS after the strings are read (issue #76)
        driver->formatDeviceStrings(ups_data);
        // ups.delay.* defaults, as USBHostUPS right after formatDeviceStrings() (US-058)
        CommandCatalog::applyDefaultDelays(ups_data, host._parser.getUsages(), driver->defaultOffDelay(),
                                           driver->defaultOnDelay(), driver->delayVariablesSupported(),
                                           driver->delayPaths());

        // 5. Replay Scenarios
        JsonArray scenarios = doc["scenarios"].as<JsonArray>();
        for (JsonObject sc : scenarios) {
            std::string scName = sc["description"] | "Unnamed Scenario";

            // Send reports
            JsonArray reports = sc["reports"].as<JsonArray>();
            for (JsonObject rep : reports) {
                uint8_t r_id = rep["report_id"].is<uint8_t>() ? rep["report_id"].as<uint8_t>() : (rep["id"] | 0);
                uint8_t r_type = rep["report_type"].is<uint8_t>() ? rep["report_type"].as<uint8_t>() : (rep["type"] | 1);

                std::vector<uint8_t> rData;
                if (rep["data"].is<JsonArray>()) {
                    JsonArray dataArr = rep["data"].as<JsonArray>();
                    for (JsonVariant v : dataArr) {
                        rData.push_back(v.as<uint8_t>());
                    }
                }

                if (!rData.empty()) {
                    driver->decodeReport(&host, r_id, r_type, rData.data(), rData.size(), ups_data);
                    // As USBHostUPS: the NUT table writes its variables last (US-066)
                    driver->postDecode(&host, r_id, r_type, rData.data(), rData.size(), ups_data);
                    s.reports.push_back({ r_id, r_type, rData });
                }
            }

            // Set ups.type directly since loop() isn't called in the replay runner
            ups_data.set("ups.type", driver->getDriverName());

            // Assert Expectations
            JsonObject exp = sc["expected_ups_data"].as<JsonObject>();

            // Iterate all keys to support new direct NUT dictionary format
            for (JsonPair kv : exp) {
                std::string key = kv.key().c_str();

                // Status string is dynamically computed, handle it specially
                if (key == "ups.status") {
                    std::string expectedStatus = kv.value().as<std::string>();
                    TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedStatus.c_str(), UPSData::computeUPSStatusString(ups_data).c_str(), scName.c_str());
                    continue;
                }

                // Dynamic dictionary property assertion
                std::string expVal;
                if (kv.value().is<bool>()) {
                    expVal = kv.value().as<bool>() ? "1" : "0";
                } else {
                    expVal = kv.value().as<std::string>();
                }

                std::string actualVal = ups_data.get(key);
                std::string assertMsg = scName + " - key: " + key;
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey(key), assertMsg.c_str());
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expVal.c_str(), actualVal.c_str(), assertMsg.c_str());
            }
        }
        return true;
    }

    // expectedCommands: NUT commands the device must expose (LIST CMD order, space
    // separated); nullptr skips the check (US-056). checkUsages, when given, receives the
    // parsed usages for device-specific assertions (US-058). expectedRW: names of the NUT
    // table rows the descriptor binds, sorted and space separated (US-066); nullptr skips it
    static void runFixtureTest(const char* filePath, const char* expectedCommands = nullptr,
                               void (*checkUsages)(const std::vector<HIDUsageDef>&) = nullptr,
                               const char* expectedRW = nullptr,
                               // Values checked after the replay, on top of the fixture's own
                               // expectations (kept out of the fixtures, US-066)
                               const std::vector<std::pair<std::string, std::string>>& expectedValues = {},
                               // LIST RW ("name:aux", see listRW) right after the replay, and
                               // after the end of the first full poll (US-067); nullptr skips
                               const char* expectedListRW = nullptr,
                               const char* expectedListRWFinal = nullptr) {
        ReplaySession s;
        if (!replay(filePath, s)) return;
        ReplayMockHost& host = s.host;
        UPSData& ups_data = s.ups_data;
        IUPSDriver* driver = s.driver;

        if (expectedCommands) {
            // Same inputs as IUSBHostUPS::getSupportedCommands(); ups_data is the replayed data
            bool beeper = host.supportsBeeperToggle() && ups_data.hasKey("ups.beeper.status");
            std::string actual;
            for (const auto* c : CommandCatalog::build(host.getUsages(), beeper, driver->shutdownCommandsSupported(),
                                                               driver->beeperMuteSupported())) {
                if (!actual.empty()) actual += " ";
                actual += c->name;
            }
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedCommands, actual.c_str(), "supported NUT commands");
        }
        if (checkUsages) checkUsages(host.getUsages());
        if (expectedRW) {
            // Every driver derives from GenericDriver, which owns the NUT table
            size_t n = 0;
            const NutTable::HidInfo* table = static_cast<GenericDriver*>(driver)->hid2nut(n);
            std::vector<std::string> names;
            for (const auto& b : NutTable::bind(table, n, host.getUsages())) {
                if (std::find(names.begin(), names.end(), b.info->info_type) == names.end()) names.push_back(b.info->info_type);
            }
            std::sort(names.begin(), names.end());
            std::string actual;
            for (const auto& name : names) {
                if (!actual.empty()) actual += " ";
                actual += name;
            }
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedRW, actual.c_str(), "NUT table rows bound by the descriptor");
        }
        for (const auto& kv : expectedValues) {
            std::string msg = "after replay - key: " + kv.first;
            TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey(kv.first.c_str()), msg.c_str());
            TEST_ASSERT_EQUAL_STRING_MESSAGE(kv.second.c_str(), ups_data.get(kv.first.c_str()).c_str(), msg.c_str());
        }
        if (expectedListRW) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedListRW, listRW(driver, ups_data).c_str(), "LIST RW after the replay");
        }
        if (expectedListRWFinal) {
            // End of the first full poll, as loop() on the device; finishInit() drops the rows
            // never read, so the next row with the same name can take over
            static_cast<GenericDriver*>(driver)->completeInitialPoll();
            // completeInitialPoll() writes nothing to UPSData: a row it makes ACTIVE is published
            // by the next postDecode(), and writableVariables() lists it only then (it needs the
            // key). On the device that is the next poll; here the last replayed report stands
            // for it. A fixture without reports has no poll to replay.
            if (!s.reports.empty()) {
                const ReplayedReport& r = s.reports.back();
                driver->decodeReport(&host, r.id, r.type, r.data.data(), r.data.size(), ups_data);
                driver->postDecode(&host, r.id, r.type, r.data.data(), r.data.size(), ups_data);
            }
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedListRWFinal, listRW(driver, ups_data).c_str(), "LIST RW after the first full poll");
        }
    }

    /**
     * Write/read-back cycle of a SET VAR on the real reports of a fixture (US-067): the
     * driver's plan must be an HID write of expectedRaw; the field is written into the last
     * FEATURE report of the fixture with that report ID (expectedReport: the bytes sent), and
     * the report decoded again, as USBHostUPS::setVariable() reads it back, must publish
     * expectedValue.
     */
    static void runSetCycle(const char* filePath, const char* var, const char* value, int32_t expectedRaw,
                            const std::vector<uint8_t>& expectedReport, const char* expectedValue) {
        ReplaySession s;
        if (!replay(filePath, s)) return;

        NutTable::SetPlan plan;
        TEST_ASSERT_EQUAL_MESSAGE((int)NutTable::SetStatus::OK,
            (int)s.driver->planSet(s.ups_data, s.host.getUsages(), var, value, plan), "planSet");
        TEST_ASSERT_EQUAL_MESSAGE((int)NutTable::SetPlan::Kind::HID, (int)plan.kind, "plan kind");
        TEST_ASSERT_EQUAL_STRING(var, plan.name);
        TEST_ASSERT_EQUAL_MESSAGE(3, plan.def.report_type, "a FEATURE usage is written");
        TEST_ASSERT_EQUAL_INT32_MESSAGE(expectedRaw, plan.raw, "logical value");

        const ReplayedReport* last = nullptr;
        for (const auto& r : s.reports) {
            if (r.id == plan.def.report_id && r.type == 3) last = &r;
        }
        TEST_ASSERT_NOT_NULL_MESSAGE(last, "no FEATURE report with the field's report ID in the fixture");

        uint8_t buf[256] = {0};
        memcpy(buf, last->data.data(), last->data.size());
        size_t len = BeeperLogic::writeField(plan.def, (uint32_t)plan.raw, buf, last->data.size());
        TEST_ASSERT_EQUAL_MESSAGE(expectedReport.size(), len, "length of the report sent");
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expectedReport.data(), buf, expectedReport.size(), "report sent");

        // Read back: the UPS answers with the report as written
        s.driver->decodeReport(&s.host, plan.def.report_id, 3, buf, len, s.ups_data);
        s.driver->postDecode(&s.host, plan.def.report_id, 3, buf, len, s.ups_data);
        std::string msg = std::string("read back - key: ") + var;
        TEST_ASSERT_TRUE_MESSAGE(s.ups_data.hasKey(var), msg.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedValue, s.ups_data.get(var).c_str(), msg.c_str());
    }
};

#endif // FIXTURE_REPLAY_RUNNER_H
