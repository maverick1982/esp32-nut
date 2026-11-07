#ifndef FIXTURE_REPLAY_RUNNER_H
#define FIXTURE_REPLAY_RUNNER_H

#include <unity.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <ArduinoJson.h>

#include "IHidTransport.h"
#include "IHidDriver.h"
#include "UPSData.h"
#include "HIDParser.h"
#include "Quirks.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "EatonDriver.h"
#include "PowercomDriver.h"
#include "GenericDriver.h"
#include "OpenUPSDriver.h"
#include "LegrandDriver.h"
#include "LiebertDriver.h"
#include "DriverRegistry.h"
#include "HidControl.h"
#include "BeeperLogic.h"

class ReplayMockHost : public IHidTransport {
public:
    UPSData _data;
    HIDParser _parser;
    uint32_t _quirks = 0;
    std::vector<uint8_t> _requestedStrings;
    std::vector<std::pair<uint8_t, uint8_t>> _requestedReports;

    void lock() const override {}
    void unlock() const override {}

    HIDParser _hid_parser;
    const HIDParser* getHIDParser() const override { return &_hid_parser; }
    const std::vector<HIDUsageDef>& getUsages() const override { return _parser.getUsages(); }
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
    uint16_t _vid = 0;
    uint16_t _pid = 0;
    uint16_t getVID() const override { return _vid; }
    uint16_t getPID() const override { return _pid; }
    bool isPollingPaused() const override { return false; }
    bool supportsBeeperToggle() const {
        return _parser.hasFeatureBeeperControl();
    }
    // US-108: every GET_REPORT is answered with zeros of the declared length (ID first),
    // decoded by _answer_driver into *_answer_data as USBHostUPS does, for fixtures with only
    // the descriptor
    bool _answer_all = false;
    IHidDriver* _answer_driver = nullptr;
    UPSData* _answer_data = nullptr;
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t) override {
        _requestedReports.push_back({report_id, report_type});
        if (_answer_all && _answer_driver && _answer_data) {
            std::vector<uint8_t> buf(_parser.getExpectedLength(report_id, report_type), 0);
            if (buf.empty()) buf.resize(2, 0);
            buf[0] = report_id;
            _answer_driver->decodeReport(this, report_id, report_type, buf.data(), buf.size(), *_answer_data);
            _answer_driver->postDecode(this, report_id, report_type, buf.data(), buf.size(), *_answer_data);
        }
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
    IHidDriver* driver = nullptr;
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

    // LIST CMD of the replayed device, space separated: same inputs as
    // IUSBHostUPS::getSupportedCommands(), with the replayed data
    static std::string listCmd(ReplaySession& s) {
        // As HidControl::supportedCommands() (US-109): the beeper field read keeps
        // beeper.enable / beeper.disable when ups.beeper.status is missing
        bool known = s.ups_data.hasKey("ups.beeper.status");
        bool beeper = s.host.supportsBeeperToggle() && (known || s.driver->beeperFieldRead(&s.host));
        std::string actual;
        size_t n_cmd = 0;
        const NutTable::HidInfo* cmd_table = s.driver->commandTable(n_cmd); // US-074: the driver's command rows
        for (const auto* c : HidControl::buildCommands(s.host.getUsages(), beeper, s.driver->shutdownCommandsSupported(),
                                                       s.driver->beeperMuteSupported(), cmd_table, n_cmd, known)) {
            if (!actual.empty()) actual += " ";
            actual += c->name;
        }
        return actual;
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

    // One report of the fixture, decoded as USBHostUPS does. With QUIRK_NO_INTERRUPT_PIPE an
    // INPUT report is dropped, as USBHostUPS::processInputReport() (US-108). True when decoded.
    static bool replayReport(ReplaySession& s, uint8_t id, uint8_t type, const std::vector<uint8_t>& data) {
        if (type == 1 && (s.host._quirks & QUIRK_NO_INTERRUPT_PIPE)) return false;
        s.driver->decodeReport(&s.host, id, type, data.data(), data.size(), s.ups_data);
        // As USBHostUPS: the NUT table writes its variables last (US-066)
        s.driver->postDecode(&s.host, id, type, data.data(), data.size(), s.ups_data);
        s.reports.push_back({ id, type, data });
        return true;
    }

    // Loads the fixture, dispatches the driver and replays every scenario, checking the
    // fixture's own expectations. False (test already failed) when the fixture is unusable.
    // forceGeneric: GenericDriver instead of the registry's driver, as for a brand the
    // registry does not know (US-079); the fixture's expectations, written for its own
    // driver, are not checked then.
    static bool replay(const char* filePath, ReplaySession& s, bool forceGeneric = false) {
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
        host._vid = vid;
        host._pid = pid;

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
        // The strings tell apart the subdrivers of a shared VID/PID, as on the device (US-097)
        IUPSDriver* created = forceGeneric ? new GenericDriver()
                                           : DriverRegistry::create(vid, pid, doc["manufacturer"] | "", doc["product"] | "");
        s.driver = created ? created->hid() : nullptr; // every registry driver is HID (US-131)
        IHidDriver* driver = s.driver;

        // Match quirks
        host._quirks = 0;
        for (int q = 0; UPS_QUIRKS[q].vid != 0; q++) {
            if (UPS_QUIRKS[q].vid == vid && (UPS_QUIRKS[q].pid == 0xFFFF || UPS_QUIRKS[q].pid == pid)) {
                host._quirks |= UPS_QUIRKS[q].flags;
            }
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
        NutTable::applyDefaultDelays(ups_data, host._parser.getUsages(), driver->defaultOffDelay(),
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

                if (!rData.empty()) replayReport(s, r_id, r_type, rData);
            }

            // The strings again after the reports, as the device asks for the table strings once
            // a report has given their index (stringid_conversion, US-073): before the reports
            // no StringId row knows its index yet (issue #108)
            for (JsonPair kv : stringsObj) {
                uint8_t idx = (uint8_t)atoi(kv.key().c_str());
                std::vector<uint8_t> sDesc = encodeUtf16Descriptor(kv.value().as<std::string>());
                driver->parseStringDescriptor(&host, idx, sDesc.data(), sDesc.size(), ups_data);
            }

            // Set driver.name directly since loop() isn't called in the replay runner
            ups_data.set("driver.name", driver->getDriverName());

            if (forceGeneric) continue;

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
        IHidDriver* driver = s.driver;

        if (expectedCommands) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedCommands, listCmd(s).c_str(), "supported NUT commands");
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
     * Everything served at the end of the replay (US-075): every key of UPSData sorted, one
     * "key=value" per line, then ups.status. Empty when the fixture is unusable.
     */
    static std::string snapshot(const char* filePath) {
        ReplaySession s;
        if (!replay(filePath, s)) return "";
        return snapshotOf(s.ups_data);
    }

    static std::string snapshotOf(const UPSData& data) {
        std::vector<std::string> lines;
        for (const auto& p : data.getAll()) lines.push_back(std::string(p.key.c_str()) + "=" + p.value.c_str());
        std::sort(lines.begin(), lines.end());
        std::string out;
        for (const auto& l : lines) out += l + "\n";
        out += "ups.status=" + std::string(UPSData::computeUPSStatusString(data).c_str()) + "\n";
        return out;
    }

    /**
     * Everything a fixture gives (US-079 baseline): the snapshot after the replay, LIST CMD,
     * LIST RW, then, after the end of the first full poll and the last report again (as
     * runFixtureTest), LIST RW and the snapshot, then the control requests of the first 60 s
     * of loop() (two full polls and the quick polls between them, also with Powercom's 800 ms
     * steps). The replay has already decoded the reports and ended the first full poll, so
     * these are full polls at steady state, without the semi-static reports (US-072):
     * "S<index>" for a string,
     * "<id>/<type>" for a report. forceGeneric: replayed with GenericDriver.
     */
    static std::string goldenText(const char* filePath, bool forceGeneric) {
        ReplaySession s;
        if (!replay(filePath, s, forceGeneric)) return "";
        std::string out = "[snapshot]\n" + snapshotOf(s.ups_data);
        out += "[cmd]\n" + listCmd(s) + "\n";
        out += "[rw]\n" + listRW(s.driver, s.ups_data) + "\n";
        static_cast<GenericDriver*>(s.driver)->completeInitialPoll();
        if (!s.reports.empty()) {
            const ReplayedReport& r = s.reports.back();
            s.driver->decodeReport(&s.host, r.id, r.type, r.data.data(), r.data.size(), s.ups_data);
            s.driver->postDecode(&s.host, r.id, r.type, r.data.data(), r.data.size(), s.ups_data);
        }
        out += "[rw.final]\n" + listRW(s.driver, s.ups_data) + "\n";
        out += "[snapshot.final]\n" + snapshotOf(s.ups_data);
        s.host._requestedReports.clear();
        s.host._requestedStrings.clear();
        // The mock records reports and strings apart: one loop() makes at most one request,
        // so a request is the one whose list grew in that call
        std::string requests;
        for (uint32_t now = 1; now <= 60000; now += 25) {
            size_t nr = s.host._requestedReports.size(), ns = s.host._requestedStrings.size();
            s.driver->loop(&s.host, s.ups_data, now);
            if (s.host._requestedStrings.size() > ns) {
                requests += " S" + std::to_string(s.host._requestedStrings.back());
            } else if (s.host._requestedReports.size() > nr) {
                const auto& r = s.host._requestedReports.back();
                requests += " " + std::to_string(r.first) + "/" + std::to_string(r.second);
            }
        }
        out += "[requests]\n" + requests + "\n";
        return out;
    }

    // The golden file of a fixture: test/test_record_replay/golden/<fixture>.<registry|generic>.txt
    static std::string goldenPath(const char* filePath, bool forceGeneric) {
        std::string name = filePath;
        size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        size_t dot = name.rfind(".json");
        if (dot != std::string::npos) name = name.substr(0, dot);
        return "test/test_record_replay/golden/" + name + (forceGeneric ? ".generic.txt" : ".registry.txt");
    }

    /**
     * goldenText() against its golden file (US-079). With the environment variable
     * ESP32NUT_WRITE_GOLDEN set the file is written instead. A difference fails with the
     * first differing lines.
     */
    static void checkGolden(const char* filePath, bool forceGeneric) {
        std::string actual = goldenText(filePath, forceGeneric);
        std::string path = goldenPath(filePath, forceGeneric);
        if (getenv("ESP32NUT_WRITE_GOLDEN")) {
            std::ofstream o(path, std::ios::binary);
            TEST_ASSERT_TRUE_MESSAGE(o.is_open(), path.c_str());
            o << actual;
            return;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            TEST_FAIL_MESSAGE(("missing golden file " + path).c_str());
            return;
        }
        std::stringstream buffer;
        buffer << f.rdbuf();
        std::string expected = buffer.str();
        if (expected == actual) return;
        std::istringstream e(expected), a(actual);
        std::string le, la, msg = path + ":";
        int line = 0, shown = 0;
        while (shown < 6) {
            bool he = (bool)std::getline(e, le), ha = (bool)std::getline(a, la);
            if (!he && !ha) break;
            line++;
            if (he && ha && le == la) continue;
            msg += " [line " + std::to_string(line) + "] -" + (he ? le : "<eof>") + " +" + (ha ? la : "<eof>");
            shown++;
        }
        TEST_FAIL_MESSAGE(msg.c_str());
    }

    // GenericDriver::buildPollLists() of any driver: a pointer to the protected member taken
    // through a derived class calls it on a GenericDriver (US-072)
    struct PollListAccess : GenericDriver {
        static void lists(const GenericDriver& d, IHidTransport* host, std::vector<PollItem>& quick, std::vector<PollItem>& full) {
            (d.*(&PollListAccess::buildPollLists))(host, quick, full);
        }
    };

    typedef std::set<std::pair<uint8_t, uint8_t>> ReportSet; // (type, id)

    /**
     * US-072: the reports the full poll leaves out once the semi-static reports are known,
     * against AC 1 computed here from the bound rows and not from the driver's bitmap. A report
     * (type, id) replayed by the fixture is semi-static when a row reads a field of it (the
     * row's usage or its twin) and every such row has HU_FLAG_STATIC or HU_FLAG_SEMI_STATIC;
     * command, HU_FLAG_ABSENT and DEAD rows do not count, an undecided row and the active
     * beeper read outside the rows make their report dynamic. The loop runs 60 s as goldenText() (D0, no change), then
     * 60 s after noteDataChanged() (D1). D1 - D0 must be the semi-static reports of D1 the
     * quick poll does not read. Returns the size of D1 - D0.
     */
    static size_t checkSemiStaticReports(const char* filePath, bool forceGeneric) {
        ReplaySession s;
        if (!replay(filePath, s, forceGeneric)) return 0;
        GenericDriver* drv = static_cast<GenericDriver*>(s.driver);
        drv->completeInitialPoll();
        std::string where = std::string(filePath) + (forceGeneric ? " (generic)" : "");

        ReportSet decoded;
        for (const auto& r : s.reports) decoded.insert({r.type, r.id});
        const auto& usages = s.host.getUsages();
        size_t n_own = 0;
        const NutTable::HidInfo* own = drv->hid2nut(n_own);
        ReportSet with_static, dynamic;
        bool beeper_row = false;
        for (const auto& b : drv->nutBindings()) {
            if (b.info >= own && b.info < own + n_own && strcmp(b.info->info_type, "ups.beeper.status") == 0) beeper_row = true;
            if (NutTable::isCommandRow(*b.info) || (b.info->hidflags & NutTable::HU_FLAG_ABSENT)) continue;
            if (b.role == NutTable::Bound::Role::DEAD) continue;
            // An undecided row (deferred without a value yet, or PENDING behind one) needs another read
            bool undecided = (b.deferred && !b.has) || b.role == NutTable::Bound::Role::PENDING;
            bool is_static = !undecided && (b.info->hidflags & (NutTable::HU_FLAG_STATIC | NutTable::HU_FLAG_SEMI_STATIC)) != 0;
            for (int idx : { b.idx, b.twin_idx }) {
                if (idx < 0 || (size_t)idx >= usages.size()) continue;
                (is_static ? with_static : dynamic).insert({usages[idx].report_type, usages[idx].report_id});
            }
        }
        String beeper = s.host.getActiveBeeperPath();
        if (!beeper_row && beeper.length() > 0) {
            for (const auto& u : usages) {
                if (strcmp(u.path, beeper.c_str()) == 0) dynamic.insert({u.report_type, u.report_id});
            }
        }
        ReportSet semi;
        for (const auto& r : with_static) {
            if (!dynamic.count(r) && decoded.count(r)) semi.insert(r);
        }

        auto window = [&](uint32_t t0, uint32_t t1) {
            s.host._requestedReports.clear();
            for (uint32_t now = t0; now <= t1; now += 25) s.driver->loop(&s.host, s.ups_data, now);
            ReportSet out;
            for (const auto& r : s.host._requestedReports) out.insert({r.second, r.first});
            return out;
        };
        ReportSet d0 = window(1, 60000);
        s.driver->noteDataChanged();
        ReportSet d1 = window(60025, 120000);

        std::vector<GenericDriver::PollItem> quick, full;
        PollListAccess::lists(*drv, &s.host, quick, full);
        ReportSet quick_set;
        for (const auto& q : quick) quick_set.insert({q.report_type, q.id});

        std::string actual, expected;
        size_t left_out = 0;
        for (const auto& r : d1) {
            std::string item = " " + std::to_string(r.second) + "/" + std::to_string(r.first);
            if (!d0.count(r)) { actual += item; left_out++; }
            if (semi.count(r) && !quick_set.count(r)) expected += item;
        }
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.c_str(), actual.c_str(), (where + ": reports read only after a change").c_str());
        return left_out;
    }

    /**
     * US-120: after the INIT walk (completeInitialPoll()), the FEATURE reports of the full poll
     * are exactly the ones something reads, computed here from the bound rows and not from the
     * driver's bitmap: the fields (usage or twin) of the rows that are not HU_FLAG_ABSENT, command
     * or DEAD rows; the active beeper when the driver's own table has no ups.beeper.status row;
     * for EatonDriver iModel and Flow.[4].ConfigApparentPower (mge_type, read by decodeReport());
     * all of it filtered by the driver's exclusions (acceptDiagReport() = acceptPollReport()).
     * PowercomDriver builds fixed lists and is skipped. Returns the number of FEATURE reports of
     * the descriptor the full poll leaves out because nothing reads them.
     */
    static size_t checkFullPollReaders(const char* filePath, bool forceGeneric) {
        ReplaySession s;
        if (!replay(filePath, s, forceGeneric)) return 0;
        if (dynamic_cast<PowercomDriver*>(s.driver)) return 0;
        GenericDriver* drv = static_cast<GenericDriver*>(s.driver);
        // loop() binds the rows before the first lists, also for a fixture without reports
        s.driver->loop(&s.host, s.ups_data, 1);
        drv->completeInitialPoll();
        std::string where = std::string(filePath) + (forceGeneric ? " (generic)" : "");

        const auto& usages = s.host.getUsages();
        size_t n_own = 0;
        const NutTable::HidInfo* own = drv->hid2nut(n_own);
        std::set<uint8_t> expected;
        bool beeper_row = false;
        auto add = [&](const HIDUsageDef& u) {
            if (u.report_type == 3 && drv->acceptDiagReport(u.report_type, u.report_id)) expected.insert(u.report_id);
        };
        for (const auto& b : drv->nutBindings()) {
            if (b.info >= own && b.info < own + n_own && strcmp(b.info->info_type, "ups.beeper.status") == 0) beeper_row = true;
            if (NutTable::isCommandRow(*b.info) || (b.info->hidflags & NutTable::HU_FLAG_ABSENT)) continue;
            if (b.role == NutTable::Bound::Role::DEAD) continue;
            for (int idx : { b.idx, b.twin_idx }) {
                if (idx >= 0 && (size_t)idx < usages.size()) add(usages[idx]);
            }
        }
        String beeper = s.host.getActiveBeeperPath();
        bool eaton = dynamic_cast<EatonDriver*>(s.driver) != nullptr;
        for (const auto& u : usages) {
            if (!beeper_row && beeper.length() > 0 && strcmp(u.path, beeper.c_str()) == 0) add(u);
            if (eaton && (strcmp(u.path, "UPS.PowerSummary.0xFFFF00F0") == 0 ||
                          NutTable::pathMatches(u, "UPS.Flow.[4].ConfigApparentPower"))) add(u);
        }

        std::vector<GenericDriver::PollItem> quick, full;
        PollListAccess::lists(*drv, &s.host, quick, full);
        std::set<uint8_t> actual;
        for (const auto& f : full) {
            if (f.report_type == 3) actual.insert(f.id);
        }
        auto text = [](const std::set<uint8_t>& ids) {
            std::string out;
            for (uint8_t id : ids) out += " " + std::to_string(id);
            return out;
        };
        TEST_ASSERT_EQUAL_STRING_MESSAGE(text(expected).c_str(), text(actual).c_str(),
                                         (where + ": FEATURE reports of the full poll").c_str());

        std::set<uint8_t> descriptor;
        for (const auto& u : usages) {
            if (u.report_type == 3 && drv->acceptDiagReport(u.report_type, u.report_id)) descriptor.insert(u.report_id);
        }
        return descriptor.size() - actual.size();
    }

    /**
     * US-123: IUPSDriver::inputValuesPolled() at steady state. One loop() builds the lists,
     * completeInitialPoll() ends the INIT walk, and the next loop() builds the pruned lists
     * (US-120), each time recomputing the coverage, as on the device.
     */
    static bool inputValuesPolled(const char* filePath, bool forceGeneric) {
        ReplaySession s;
        if (!replay(filePath, s, forceGeneric)) return false;
        s.driver->loop(&s.host, s.ups_data, 1);
        if (GenericDriver* drv = dynamic_cast<GenericDriver*>(s.driver)) drv->completeInitialPoll();
        s.driver->loop(&s.host, s.ups_data, 2);
        return s.driver->inputValuesPolled();
    }

    // A driver of class D with its poll hooks public (US-076)
    template <class D>
    struct PollListProbe : D {
        using GenericDriver::buildPollLists;
        using D::onLoop;
    };

    /**
     * The quick and full poll lists the driver of class D builds for the fixture's descriptor
     * (US-076), after one onLoop() with the replayed data (the Back-UPS BX flag comes from
     * ups.model): "quick: id/type ... | full: id/type ...", type 1 = INPUT, 3 = FEATURE.
     */
    template <class D>
    static std::string pollLists(const char* filePath) {
        ReplaySession s;
        if (!replay(filePath, s)) return "";
        PollListProbe<D> probe;
        probe.setup();
        probe.onLoop(&s.host, s.ups_data);
        std::vector<GenericDriver::PollItem> quick, full;
        probe.buildPollLists(&s.host, quick, full);
        std::string out = "quick:";
        for (const auto& i : quick) out += " " + std::to_string(i.id) + "/" + std::to_string(i.report_type);
        out += " | full:";
        for (const auto& i : full) out += " " + std::to_string(i.id) + "/" + std::to_string(i.report_type);
        return out;
    }

    /**
     * The status served at the end of the replay (US-107): ups.status, and ups.alarm /
     * input.transfer.reason, where nullptr means that the key must be missing.
     */
    static void runStatusBaseline(const char* filePath, const char* expectedStatus, const char* expectedAlarm,
                                  const char* expectedReason) {
        ReplaySession s;
        if (!replay(filePath, s)) return;
        std::string where = std::string(filePath) + " - ";
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedStatus, UPSData::computeUPSStatusString(s.ups_data).c_str(),
                                         (where + "ups.status").c_str());
        const struct { const char* key; const char* expected; } vars[] = {
            { "ups.alarm", expectedAlarm }, { "input.transfer.reason", expectedReason },
        };
        for (const auto& v : vars) {
            std::string msg = where + v.key;
            if (!v.expected) {
                TEST_ASSERT_FALSE_MESSAGE(s.ups_data.hasKey(v.key), msg.c_str());
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(s.ups_data.hasKey(v.key), msg.c_str());
            TEST_ASSERT_EQUAL_STRING_MESSAGE(v.expected, s.ups_data.get(v.key).c_str(), msg.c_str());
        }
    }

    /**
     * The fixture's reports with some fields changed (US-107): each (path, raw) is written into
     * the last replayed report holding the FEATURE usage of that path, the reports changed are
     * decoded again, then the status served is checked as runStatusBaseline(). Then the
     * original reports are decoded again and the status must be the baseline one.
     */
    static void runFieldScenario(const char* filePath, const std::vector<std::pair<const char*, uint32_t>>& fields,
                                 const char* expectedStatus, const char* expectedAlarm, const char* baselineStatus) {
        ReplaySession s;
        if (!replay(filePath, s)) return;
        std::vector<ReplayedReport> changed;
        for (const auto& f : fields) {
            const HIDUsageDef* def = nullptr;
            for (const auto& u : s.host.getUsages()) {
                if (u.report_type == 3 && NutTable::pathMatches(u, f.first)) { def = &u; break; }
            }
            TEST_ASSERT_NOT_NULL_MESSAGE(def, f.first);
            ReplayedReport* target = nullptr;
            for (auto& r : changed) {
                if (r.id == def->report_id && r.type == 3) target = &r;
            }
            if (!target) {
                const ReplayedReport* last = nullptr;
                for (const auto& r : s.reports) {
                    if (r.id == def->report_id && r.type == 3) last = &r;
                }
                TEST_ASSERT_NOT_NULL_MESSAGE(last, "no replayed FEATURE report holds the field");
                changed.push_back(*last);
                target = &changed.back();
            }
            uint8_t buf[256] = {0};
            memcpy(buf, target->data.data(), target->data.size());
            BeeperLogic::writeField(*def, f.second, buf, target->data.size());
            target->data.assign(buf, buf + target->data.size());
        }
        for (const auto& r : changed) {
            s.driver->decodeReport(&s.host, r.id, r.type, r.data.data(), r.data.size(), s.ups_data);
            s.driver->postDecode(&s.host, r.id, r.type, r.data.data(), r.data.size(), s.ups_data);
        }
        std::string where = std::string(filePath) + " - ";
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedStatus, UPSData::computeUPSStatusString(s.ups_data).c_str(),
                                         (where + "ups.status").c_str());
        if (expectedAlarm) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expectedAlarm, s.ups_data.get("ups.alarm").c_str(), (where + "ups.alarm").c_str());
        } else {
            TEST_ASSERT_FALSE_MESSAGE(s.ups_data.hasKey("ups.alarm"), (where + "ups.alarm").c_str());
        }

        // The original reports again: the condition is gone
        for (const auto& c : changed) {
            const ReplayedReport* last = nullptr;
            for (const auto& r : s.reports) {
                if (r.id == c.id && r.type == c.type) last = &r;
            }
            s.driver->decodeReport(&s.host, last->id, last->type, last->data.data(), last->data.size(), s.ups_data);
            s.driver->postDecode(&s.host, last->id, last->type, last->data.data(), last->data.size(), s.ups_data);
        }
        TEST_ASSERT_EQUAL_STRING_MESSAGE(baselineStatus, UPSData::computeUPSStatusString(s.ups_data).c_str(),
                                         (where + "ups.status after the original reports").c_str());
        TEST_ASSERT_FALSE_MESSAGE(s.ups_data.hasKey("ups.alarm"), (where + "ups.alarm after the original reports").c_str());
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
