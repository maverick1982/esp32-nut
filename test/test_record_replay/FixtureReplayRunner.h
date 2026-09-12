#ifndef FIXTURE_REPLAY_RUNNER_H
#define FIXTURE_REPLAY_RUNNER_H

#include <unity.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
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

    const std::vector<HIDUsageDef>& getUsages() const override { return _parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _parser.getUsageDef(usage); }
    String getActiveBeeperPath() const override {
        for (const auto& u : _parser.getUsages()) {
            if (u.path == "UPS.PowerSummary.AudibleAlarmControl" || 
                u.path == "UPS.BatterySystem.Battery.AudibleAlarmControl" || 
                u.path == "UPS.AudibleAlarmControl") {
                return u.path;
            }
        }
        return "";
    }
    uint32_t getQuirks() const override { return _quirks; }
    bool isControlPending() const override { return false; }
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

    static void runFixtureTest(const char* filePath) {
        std::ifstream f(filePath);
        if (!f.is_open()) {
            std::string msg = "Cannot open fixture file: ";
            msg += filePath;
            TEST_FAIL_MESSAGE(msg.c_str());
            return;
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
            return;
        }

        // 1. Read Device Meta
        std::string vidStr = doc["vid"].as<std::string>();
        uint16_t vid = (uint16_t)strtol(vidStr.c_str(), nullptr, 16);
        std::string pidStr = doc["pid"] | "0x0000";
        uint16_t pid = (uint16_t)strtol(pidStr.c_str(), nullptr, 16);

        ReplayMockHost host;
        UPSData ups_data;

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
        IUPSDriver* driver = nullptr;
        switch (vid) {
            case 0x0463:
                driver = new EatonDriver();
                break;
            case 0x051D:
                driver = new APCDriver();
                break;
            case 0x0764:
                driver = new CyberPowerDriver();
                break;
            case 0x0D9F:
                driver = new PowercomDriver();
                break;
            case 0x04D8:
                driver = new OpenUPSDriver();
                break;
            default:
                driver = new GenericDriver();
                break;
        }

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

        delete driver;
    }
};

#endif // FIXTURE_REPLAY_RUNNER_H
