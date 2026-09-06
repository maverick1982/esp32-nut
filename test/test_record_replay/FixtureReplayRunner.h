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
    String getActiveBeeperPath() const override { return "UPS.PowerSummary.AudibleAlarmControl"; }
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
    static std::vector<uint8_t> hexStringToBytes(const std::string& hexStr) {
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i < hexStr.length(); i += 2) {
            std::string byteString = hexStr.substr(i, 2);
            uint8_t byte = (uint8_t)strtol(byteString.c_str(), nullptr, 16);
            bytes.push_back(byte);
        }
        return bytes;
    }

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
                uint8_t r_id = rep["report_id"] | 0;
                uint8_t r_type = rep["report_type"] | 1;
                std::string dataHex = rep["data_hex"].as<std::string>();
                std::vector<uint8_t> rData = hexStringToBytes(dataHex);

                driver->decodeReport(&host, r_id, r_type, rData.data(), rData.size(), ups_data);
            }

            // Assert Expectations
            JsonObject exp = sc["expected_ups_data"].as<JsonObject>();
            if (exp["status"].is<std::string>()) {
                std::string expectedStatus = exp["status"].as<std::string>();
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedStatus.c_str(), UPSData::computeUPSStatusString(ups_data).c_str(), scName.c_str());
            }
            if (exp["acPresent"].is<bool>()) {
                bool expectedAC = exp["acPresent"].as<bool>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("ups.status.ac_present"), scName.c_str());
                TEST_ASSERT_EQUAL_MESSAGE(expectedAC, ups_data.getBool("ups.status.ac_present"), scName.c_str());
            }
            if (exp["discharging"].is<bool>()) {
                bool expectedDischarging = exp["discharging"].as<bool>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("ups.status.discharging"), scName.c_str());
                TEST_ASSERT_EQUAL_MESSAGE(expectedDischarging, ups_data.getBool("ups.status.discharging"), scName.c_str());
            }
            if (exp["remainingCapacity"].is<uint8_t>()) {
                uint8_t expCap = exp["remainingCapacity"].as<uint8_t>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("battery.charge"), scName.c_str());
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(expCap, (uint8_t)ups_data.getFloat("battery.charge"), scName.c_str());
            }
            if (exp["runTimeToEmpty"].is<uint32_t>()) {
                uint32_t expRuntime = exp["runTimeToEmpty"].as<uint32_t>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("battery.runtime"), scName.c_str());
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(expRuntime, (uint32_t)ups_data.getFloat("battery.runtime"), scName.c_str());
            }
            if (exp["load"].is<uint8_t>()) {
                uint8_t expLoad = exp["load"].as<uint8_t>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("ups.load"), scName.c_str());
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(expLoad, (uint8_t)ups_data.getFloat("ups.load"), scName.c_str());
            }
            if (exp["outputVoltage"].is<float>()) {
                float expVolt = exp["outputVoltage"].as<float>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("output.voltage"), scName.c_str());
                TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1, expVolt, ups_data.getFloat("output.voltage"), scName.c_str());
            }
            if (exp["manufacturer"].is<std::string>()) {
                std::string expMfr = exp["manufacturer"].as<std::string>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("ups.mfr"), scName.c_str());
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expMfr.c_str(), ups_data.get("ups.mfr").c_str(), scName.c_str());
            }
            if (exp["product"].is<std::string>()) {
                std::string expProd = exp["product"].as<std::string>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("ups.model"), scName.c_str());
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expProd.c_str(), ups_data.get("ups.model").c_str(), scName.c_str());
            }
            if (exp["batteryMfrDate"].is<std::string>()) {
                std::string expDate = exp["batteryMfrDate"].as<std::string>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("battery.mfr.date"), scName.c_str());
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expDate.c_str(), ups_data.get("battery.mfr.date").c_str(), scName.c_str());
            }
            if (exp["upsMfrDate"].is<std::string>()) {
                std::string expDate = exp["upsMfrDate"].as<std::string>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("ups.mfr.date"), scName.c_str());
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expDate.c_str(), ups_data.get("ups.mfr.date").c_str(), scName.c_str());
            }
            if (exp["batteryDate"].is<std::string>()) {
                std::string expDate = exp["batteryDate"].as<std::string>();
                TEST_ASSERT_TRUE_MESSAGE(ups_data.hasKey("battery.date"), scName.c_str());
                TEST_ASSERT_EQUAL_STRING_MESSAGE(expDate.c_str(), ups_data.get("battery.date").c_str(), scName.c_str());
            }
        }

        delete driver;
    }
};

#endif // FIXTURE_REPLAY_RUNNER_H
