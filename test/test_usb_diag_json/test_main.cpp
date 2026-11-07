#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include <climits>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "ChunkWriter.h"
#include "UsbDiagJson.h"
#include "UsbDiagSnapshot.h"

// Export senza JsonDocument (US-096): UsbDiagJson::write() deve produrre gli stessi byte di
// serializeJsonPretty del documento che costruiva USBHostUPS::buildUSBDiagnostics()

static const size_t BUFFER_SIZES[] = {1, 7, 64, 1024};

static const char* exportReadName(DiagSweep::State st) {
    switch (st) {
        case DiagSweep::State::Reading:     return "reading";
        case DiagSweep::State::Done:        return "done";
        case DiagSweep::State::Interrupted: return "interrupted";
        default:                            return "not_requested";
    }
}

// Copia fedele di USBHostUPS::buildUSBDiagnostics() (commit 975df38, US-095), che legge
// dall'istantanea invece che dai membri dell'host. US-119 aggiunge solo l'oggetto
// "export_read" dopo "link" (with_export_read = false: l'export di prima) e le chiavi
// "on_demand"/"empty" dei report letti su richiesta.
static void buildReferenceDoc(const UsbDiagSnapshot& s, JsonDocument& doc, bool with_export_read = true) {
    doc["firmware_version"] = s.firmware_version;
    doc["vid"] = s.vid;
    doc["pid"] = s.pid;
    doc["manufacturer"] = s.manufacturer;
    doc["product"] = s.product;
    doc["serial_number"] = s.serial_number;

    String arrStr = "[\n";
    if (s.descriptor_hex.length() > 0) {
        for (size_t i = 0; i < s.descriptor_hex.length(); i += 2) {
            if (i > 0 && (i / 2) % 10 == 0) arrStr += ",\n    ";
            else if (i > 0) arrStr += ", ";
            else arrStr += "    ";

            String byteStr = s.descriptor_hex.substring(i, i+2);
            byteStr.toUpperCase();
            arrStr += "\"0x" + byteStr + "\"";
        }
    }
    arrStr += "\n  ]";
    doc["report_descriptor_hex"] = serialized(arrStr);

    doc["quirks"] = s.quirks;
    doc["driver"] = s.driver;

    JsonObject link = doc["link"].to<JsonObject>();
    link["failures"] = s.link_failures;
    link["recoveries"] = s.link_recoveries;
    link["stale"] = s.stale;
    link["dropped_input_reports"] = s.dropped_input_reports;
    link["input_period_ms"] = s.input_period_ms;
    link["input_recoveries"] = s.input_recoveries;
    link["input_watchdog"] = s.input_watchdog;
    link["port_cycles"] = s.port_cycles;
    link["incomplete_input_reports"] = s.incomplete_input_reports;
    link["ep_in_mps"] = s.ep_in_mps;

    if (with_export_read) {
        JsonObject er = doc["export_read"].to<JsonObject>();
        er["state"] = exportReadName(s.export_read.state);
        er["requested"] = s.export_read.total;
        er["read"] = s.export_read.read;
        er["empty"] = s.export_read.empty;
        er["failed"] = s.export_read.failed;
    }

    JsonArray scenarios = doc["scenarios"].to<JsonArray>();
    JsonObject scenario = scenarios.add<JsonObject>();
    scenario["description"] = "Live ESP32 dump";

    JsonArray reports = scenario["reports"].to<JsonArray>();
    for (const auto& r : s.reports) {
        JsonObject report = reports.add<JsonObject>();
        report["id"] = r.id;
        report["type"] = r.type;

        String dataStr = "[";
        for (size_t i = 0; i < r.data.size(); ++i) {
            if (i > 0) dataStr += ", ";
            dataStr += String(r.data[i]);
        }
        dataStr += "]";
        report["data"] = serialized(dataStr);
        if (r.on_demand) report["on_demand"] = true;
        if (r.empty) report["empty"] = true;
    }

    JsonObject expectedData = scenario["expected_ups_data"].to<JsonObject>();
    for (const auto& param : s.ups_data) {
        expectedData[param.key] = param.value;
    }
}

static std::string writeChunked(const UsbDiagSnapshot& s, size_t cap) {
    std::string out;
    std::vector<char> buf(cap);
    ChunkWriter w(buf.data(), cap, [&out](const char* data, size_t n) { out.append(data, n); });
    UsbDiagJson::write(s, w);
    w.flush();
    return out;
}

static void assertSameAsReference(const UsbDiagSnapshot& s) {
    JsonDocument doc;
    buildReferenceDoc(s, doc);
    std::string expected;
    serializeJsonPretty(doc, expected);
    for (size_t cap : BUFFER_SIZES) {
        std::string got = writeChunked(s, cap);
        if (got != expected) {
            size_t i = 0;
            while (i < got.size() && i < expected.size() && got[i] == expected[i]) ++i;
            char msg[160];
            snprintf(msg, sizeof(msg), "buffer %u: first difference at byte %u (got %u bytes, expected %u)",
                     (unsigned)cap, (unsigned)i, (unsigned)got.size(), (unsigned)expected.size());
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

static UsbDiagSnapshot baseSnapshot() {
    UsbDiagSnapshot s;
    s.firmware_version = "us096-test";
    s.vid = "463";
    s.pid = "ffff";
    s.manufacturer = "EATON";
    s.product = "Eaton 3S";
    s.serial_number = "Blank";
    s.descriptor_hex = "05840904a101";
    s.quirks = 0;
    s.driver = "EatonDriver";
    s.input_period_ms = 2000;
    s.ep_in_mps = 8;
    s.reports.push_back({1, 3, {1, 2, 255}});
    s.ups_data.push_back({"ups.status", "OL"});
    return s;
}

// Istantanea ricavata dalla fixture Eaton 3S: descriptor, report e variabili reali
void test_eaton_fixture_same_bytes(void) {
    std::ifstream f("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json");
    TEST_ASSERT_TRUE_MESSAGE(f.is_open(), "cannot open the Eaton 3S fixture");
    std::stringstream ss;
    ss << f.rdbuf();
    JsonDocument fx;
    TEST_ASSERT_TRUE(deserializeJson(fx, ss.str()) == DeserializationError::Ok);

    UsbDiagSnapshot s;
    s.firmware_version = "us096-test";
    s.vid = fx["vid"].as<const char*>();
    s.pid = fx["pid"].as<const char*>();
    s.manufacturer = fx["manufacturer"].as<const char*>();
    s.product = fx["product"].as<const char*>();
    s.serial_number = fx["serial_number"].as<const char*>();
    // L'host salva le cifre in maiuscolo, ma il vecchio export le convertiva comunque con
    // toUpperCase(): in minuscolo qui si verifica che il writer faccia lo stesso
    for (JsonVariant b : fx["report_descriptor_hex"].as<JsonArray>()) {
        String h = String(b.as<const char*>()).substring(2);
        for (auto& c : h) c = (char)tolower((unsigned char)c);
        s.descriptor_hex += h;
    }
    s.quirks = fx["quirks"] | 0u;
    s.driver = fx["driver"] | "EatonDriver";
    s.input_period_ms = 2000;
    s.ep_in_mps = 8;
    JsonObject sc = fx["scenarios"][0];
    for (JsonObject r : sc["reports"].as<JsonArray>()) {
        UsbDiagReport rep;
        rep.id = r["id"];
        rep.type = r["type"];
        for (JsonVariant v : r["data"].as<JsonArray>()) rep.data.push_back(v.as<uint8_t>());
        s.reports.push_back(rep);
    }
    for (JsonPair p : sc["expected_ups_data"].as<JsonObject>()) {
        s.ups_data.push_back({p.key().c_str(), p.value().as<const char*>()});
    }
    TEST_ASSERT_TRUE(s.descriptor_hex.length() > 200);
    TEST_ASSERT_TRUE(s.reports.size() > 10);
    TEST_ASSERT_TRUE(s.ups_data.size() > 10);
    assertSameAsReference(s);
}

void test_empty_snapshot_same_bytes(void) {
    UsbDiagSnapshot s;  // stringhe vuote, descriptor vuoto, nessun report, nessuna variabile
    assertSameAsReference(s);
}

void test_escaped_strings_same_bytes(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.manufacturer = "Quote \" and back\\slash";
    s.product = "line1\nline2\r\ttab";
    s.serial_number = String("bs\b ff\f ctl\x01 end");
    s.ups_data.push_back({"ambient.temperature.unit", "\xC2\xB0" "C"});
    s.ups_data.push_back({"key \"with\" quotes", "a/b"});
    s.ups_data.push_back({"back\\slash.key", "v"});
    s.ups_data.push_back({"", "empty key"});
    s.serial_number += '\0';  // byte nullo interno: \u0000 come in TextFormatter
    s.serial_number += "after-nul";
    assertSameAsReference(s);
}

void test_driver_none_and_flags(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.driver = "None";
    s.stale = true;
    assertSameAsReference(s);
    s.stale = false;
    assertSameAsReference(s);
}

// US-123: link.input_watchdog is a JSON boolean after input_recoveries
void test_input_watchdog_flag(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.input_watchdog = true;
    assertSameAsReference(s);
    std::string on = writeChunked(s, 64);
    TEST_ASSERT_TRUE(on.find("\"input_watchdog\": true") != std::string::npos);
    TEST_ASSERT_TRUE(on.find("\"input_recoveries\"") < on.find("\"input_watchdog\""));
    s.input_watchdog = false;
    assertSameAsReference(s);
    TEST_ASSERT_TRUE(writeChunked(s, 64).find("\"input_watchdog\": false") != std::string::npos);
}

void test_odd_descriptor_and_line_breaks(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.descriptor_hex = "0123456789abcdef0123456789abcdef0123456789abcde";  // 47 cifre: due righe e una cifra sola
    assertSameAsReference(s);
    s.descriptor_hex = "aabbccddeeff00112233";  // esattamente 10 byte
    assertSameAsReference(s);
}

void test_reports_edge_cases(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.reports.clear();
    s.reports.push_back({0, 1, {}});
    s.reports.push_back({255, 3, {0}});
    assertSameAsReference(s);
    s.reports.clear();  // nessun report: "reports": []
    assertSameAsReference(s);
}

void test_numeric_limits(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.quirks = UINT32_MAX;
    s.link_failures = UINT8_MAX;
    s.link_recoveries = UINT8_MAX;
    s.dropped_input_reports = UINT32_MAX;
    s.input_period_ms = UINT32_MAX;
    s.input_recoveries = UINT8_MAX;
    s.port_cycles = UINT32_MAX;
    s.incomplete_input_reports = UINT32_MAX;
    s.ep_in_mps = UINT16_MAX;
    assertSameAsReference(s);
}

// --- US-119: lettura su richiesta dell'export ---

static const char* kNotRequested =
    ",\r\n  \"export_read\": {\r\n    \"state\": \"not_requested\",\r\n    \"requested\": 0,"
    "\r\n    \"read\": 0,\r\n    \"empty\": 0,\r\n    \"failed\": 0\r\n  }";

// Senza lettura l'export è quello di prima byte per byte, a parte l'oggetto export_read
void test_without_read_only_export_read_is_added(void) {
    UsbDiagSnapshot s = baseSnapshot();
    JsonDocument old_doc;
    buildReferenceDoc(s, old_doc, false);
    std::string old_bytes;
    serializeJsonPretty(old_doc, old_bytes);

    std::string got = writeChunked(s, 64);
    size_t at = got.find(kNotRequested);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "export_read not_requested missing");
    // Subito dopo la chiusura di "link"
    TEST_ASSERT_EQUAL_STRING("\"ep_in_mps\": 8\r\n  }", got.substr(at - 19, 19).c_str());
    got.erase(at, strlen(kNotRequested));
    TEST_ASSERT_TRUE(got == old_bytes);
    TEST_ASSERT_TRUE(got.find("on_demand") == std::string::npos);
    assertSameAsReference(s);
}

void test_on_demand_report(void) {
    UsbDiagSnapshot s = baseSnapshot();
    UsbDiagReport r;
    r.id = 227;
    r.type = 3;
    r.data = {227, 5};
    r.on_demand = true;
    s.reports.push_back(r);
    s.export_read.state = DiagSweep::State::Done;
    s.export_read.total = 1;
    s.export_read.done = 1;
    s.export_read.read = 1;
    assertSameAsReference(s);

    std::string got = writeChunked(s, 7);
    TEST_ASSERT_TRUE(got.find("\"id\": 227,\r\n          \"type\": 3,\r\n          \"data\": [227, 5],"
                              "\r\n          \"on_demand\": true\r\n        }") != std::string::npos);
    TEST_ASSERT_TRUE(got.find("\"empty\": true") == std::string::npos);
    // Il report del poll resta senza chiavi nuove
    TEST_ASSERT_TRUE(got.find("\"data\": [1, 2, 255]\r\n        }") != std::string::npos);
    TEST_ASSERT_TRUE(got.find("\"state\": \"done\",\r\n    \"requested\": 1,\r\n    \"read\": 1,") != std::string::npos);
}

void test_empty_on_demand_report(void) {
    UsbDiagSnapshot s = baseSnapshot();
    UsbDiagReport r;
    r.id = 227;
    r.type = 3;
    r.on_demand = true;
    r.empty = true;
    s.reports.push_back(r);
    s.export_read.state = DiagSweep::State::Done;
    s.export_read.total = 1;
    s.export_read.done = 1;
    s.export_read.empty = 1;
    assertSameAsReference(s);

    std::string got = writeChunked(s, 1);
    TEST_ASSERT_TRUE(got.find("\"data\": [],\r\n          \"on_demand\": true,\r\n          \"empty\": true\r\n        }")
                     != std::string::npos);
    TEST_ASSERT_TRUE(got.find("\"empty\": 1,") != std::string::npos);
}

// Le letture su richiesta si uniscono ai report del poll per chiave (type << 8) | id; la copia
// del poll vince
void test_on_demand_reports_merged_by_key(void) {
    std::vector<UsbDiagReport> reports;
    reports.push_back({0x01, 1, {9}});   // INPUT 1  -> 0x0101
    reports.push_back({0x02, 3, {8}});   // FEATURE 2 -> 0x0302
    reports.push_back({0x10, 3, {7}});   // FEATURE 16 -> 0x0310

    std::vector<DiagSweep::Entry> results;
    results.push_back({3, 0x20, false, {1, 2}});
    results.push_back({3, 0x01, true, {}});
    results.push_back({3, 0x10, false, {6, 6}});  // già nel poll: ignorato
    results.push_back({3, 0x05, false, {5}});
    addOnDemandReports(reports, results);

    const uint8_t ids[] = {0x01, 0x01, 0x02, 0x05, 0x10, 0x20};
    const uint8_t types[] = {1, 3, 3, 3, 3, 3};
    const bool on_demand[] = {false, true, false, true, false, true};
    TEST_ASSERT_EQUAL(6, (int)reports.size());
    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_UINT8(ids[i], reports[i].id);
        TEST_ASSERT_EQUAL_UINT8(types[i], reports[i].type);
        TEST_ASSERT_EQUAL(on_demand[i], reports[i].on_demand);
    }
    TEST_ASSERT_TRUE(reports[1].empty);
    TEST_ASSERT_EQUAL(0, (int)reports[1].data.size());
    TEST_ASSERT_EQUAL(1, (int)reports[4].data.size());  // la copia del poll
    TEST_ASSERT_EQUAL_UINT8(7, reports[4].data[0]);

    UsbDiagSnapshot s = baseSnapshot();
    s.reports = reports;
    assertSameAsReference(s);
}

void test_interrupted_and_reading_states(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.export_read.state = DiagSweep::State::Interrupted;
    s.export_read.total = 41;
    s.export_read.done = 12;
    s.export_read.read = 10;
    s.export_read.empty = 1;
    s.export_read.failed = 1;
    assertSameAsReference(s);
    std::string got = writeChunked(s, 1024);
    TEST_ASSERT_TRUE(got.find("\"export_read\": {\r\n    \"state\": \"interrupted\",\r\n    \"requested\": 41,"
                              "\r\n    \"read\": 10,\r\n    \"empty\": 1,\r\n    \"failed\": 1\r\n  }") != std::string::npos);

    s.export_read.state = DiagSweep::State::Reading;
    assertSameAsReference(s);
    TEST_ASSERT_TRUE(writeChunked(s, 64).find("\"state\": \"reading\"") != std::string::npos);
}

// --- Sezione Q* (US-136) ---

static UsbDiagQxExchange qxExchange(const char* cmd, const char* answer, int32_t length,
                                    uint32_t uptime, uint32_t duration, const char* error) {
    UsbDiagQxExchange e;
    e.command = cmd;
    e.answer.assign(answer, answer + strlen(answer));
    e.length = length;
    e.uptime_ms = uptime;
    e.duration_ms = duration;
    e.error = error;
    return e;
}

// Con qx.present falso l'export HID resta byte per byte quello di prima, anche se i campi
// della sezione sono pieni
void test_qx_absent_keeps_hid_export(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.qx.transport = "cypress";
    s.qx.exchanges.push_back(qxExchange("Q1\r", "(229.0\r", 7, 10, 20, ""));
    assertSameAsReference(s);
    TEST_ASSERT_TRUE(writeChunked(s, 64).find("\"qx\"") == std::string::npos);
}

// UPS Q* riconosciuto dalla tabella ma senza trasporto portato (US-132): solo il trasporto,
// nessuno scambio
void test_qx_without_driver(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.driver = "none";
    s.qx.present = true;
    s.qx.transport = "krauler";
    std::string got = writeChunked(s, 7);
    TEST_ASSERT_TRUE(got.find("\"failed\": 0\r\n  },\r\n  \"qx\": {\r\n    \"transport\": \"krauler\",\r\n"
                              "    \"protocol\": \"\",\r\n    \"probe\": \"\",\r\n    \"tries\": 0,\r\n"
                              "    \"failed_cycles\": 0,\r\n    \"retry_ms\": 0,\r\n    \"forced_protocol\": \"\",\r\n"
                              "    \"novendor\": false,\r\n    \"exchanges\": []\r\n  },\r\n  \"scenarios\": [") != std::string::npos);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, got) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL_STRING("krauler", doc["qx"]["transport"]);
    TEST_ASSERT_EQUAL(0, (int)doc["qx"]["exchanges"].size());
}

// Uno scambio riuscito e uno in timeout: escape di \r, esadecimale maiuscolo separato da
// spazi, lunghezza negativa, "time" solo quando il server l'ha calcolata
void test_qx_two_exchanges(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.qx.present = true;
    s.qx.transport = "cypress";
    s.qx.protocol = "Megatec 0.10";
    s.qx.probe = "claimed";
    s.qx.tries = 1;
    s.qx.failed_cycles = 2;
    s.qx.retry_ms = 30000;
    s.qx.forced_protocol = "megatec";
    s.qx.novendor = true;
    UsbDiagQxExchange ok = qxExchange("Q1\r", "(229.0 230\r", 11, 12345, 250, "");
    ok.time = "2026-11-07 10:15:42";
    s.qx.exchanges.push_back(ok);
    s.qx.exchanges.push_back(qxExchange("F\r", "", -7, 15000, 2000, "ESP_ERR_TIMEOUT"));

    std::string got = writeChunked(s, 1);
    TEST_ASSERT_TRUE(got.find("    \"exchanges\": [\r\n      {\r\n        \"uptime_ms\": 12345,\r\n"
                              "        \"time\": \"2026-11-07 10:15:42\",\r\n"
                              "        \"command\": \"Q1\\r\",\r\n"
                              "        \"answer_text\": \"(229.0 230\\r\",\r\n"
                              "        \"answer_hex\": \"28 32 32 39 2E 30 20 32 33 30 0D\",\r\n"
                              "        \"length\": 11,\r\n        \"duration_ms\": 250,\r\n"
                              "        \"truncated\": false,\r\n        \"error\": \"\"\r\n      },\r\n"
                              "      {\r\n        \"uptime_ms\": 15000,\r\n        \"command\": \"F\\r\",\r\n"
                              "        \"answer_text\": \"\",\r\n        \"answer_hex\": \"\",\r\n"
                              "        \"length\": -7,\r\n        \"duration_ms\": 2000,\r\n"
                              "        \"truncated\": false,\r\n        \"error\": \"ESP_ERR_TIMEOUT\"\r\n"
                              "      }\r\n    ]\r\n  },") != std::string::npos);
    // Stessa uscita con ogni dimensione del buffer
    for (size_t cap : BUFFER_SIZES) TEST_ASSERT_TRUE(writeChunked(s, cap) == got);

    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, got) == DeserializationError::Ok);
    JsonObject q = doc["qx"];
    TEST_ASSERT_EQUAL_STRING("Megatec 0.10", q["protocol"]);
    TEST_ASSERT_EQUAL_STRING("claimed", q["probe"]);
    TEST_ASSERT_EQUAL(2, q["failed_cycles"].as<int>());
    TEST_ASSERT_EQUAL(30000, q["retry_ms"].as<int>());
    TEST_ASSERT_EQUAL_STRING("megatec", q["forced_protocol"]);
    // US-138: the novendor setting, right after forced_protocol
    TEST_ASSERT_TRUE(q["novendor"].as<bool>());
    TEST_ASSERT_TRUE(got.find("\"forced_protocol\": \"megatec\",\r\n    \"novendor\": true,\r\n") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("(229.0 230\r", q["exchanges"][0]["answer_text"]);
    TEST_ASSERT_TRUE(q["exchanges"][1]["time"].isNull());
    TEST_ASSERT_EQUAL(-7, q["exchanges"][1]["length"].as<int>());
    // L'oggetto qx sta tra export_read e scenarios
    TEST_ASSERT_TRUE(got.find("\"export_read\"") < got.find("\"qx\""));
    TEST_ASSERT_TRUE(got.find("\"qx\"") < got.find("\"scenarios\""));
}

// Ogni byte di controllo o non ASCII della risposta dà JSON valido; truncated si vede
void test_qx_control_bytes_and_truncated(void) {
    UsbDiagSnapshot s = baseSnapshot();
    s.qx.present = true;
    s.qx.transport = "cypress";
    UsbDiagQxExchange e;
    e.command = "I\r";
    const uint8_t answer[] = {0x00, 0x01, '"', '\\', '\n', '\t', 0x1F, 0x7F, 0x80, 0xFF, 'A'};
    e.answer.assign(answer, answer + sizeof(answer));
    e.length = 200;
    e.truncated = true;
    s.qx.exchanges.push_back(e);

    std::string got = writeChunked(s, 64);
    TEST_ASSERT_TRUE(got.find("\"answer_text\": \"\\u0000\\u0001\\\"\\\\\\n\\t\\u001F\\u007F\\u0080\\u00FFA\"")
                     != std::string::npos);
    TEST_ASSERT_TRUE(got.find("\"answer_hex\": \"00 01 22 5C 0A 09 1F 7F 80 FF 41\"") != std::string::npos);
    TEST_ASSERT_TRUE(got.find("\"length\": 200,") != std::string::npos);
    TEST_ASSERT_TRUE(got.find("\"truncated\": true,") != std::string::npos);
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, got) == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc["qx"]["exchanges"][0]["truncated"].as<bool>());
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_eaton_fixture_same_bytes);
    RUN_TEST(test_empty_snapshot_same_bytes);
    RUN_TEST(test_escaped_strings_same_bytes);
    RUN_TEST(test_driver_none_and_flags);
    RUN_TEST(test_input_watchdog_flag);
    RUN_TEST(test_odd_descriptor_and_line_breaks);
    RUN_TEST(test_reports_edge_cases);
    RUN_TEST(test_numeric_limits);
    RUN_TEST(test_without_read_only_export_read_is_added);
    RUN_TEST(test_on_demand_report);
    RUN_TEST(test_empty_on_demand_report);
    RUN_TEST(test_on_demand_reports_merged_by_key);
    RUN_TEST(test_interrupted_and_reading_states);
    RUN_TEST(test_qx_absent_keeps_hid_export);
    RUN_TEST(test_qx_without_driver);
    RUN_TEST(test_qx_two_exchanges);
    RUN_TEST(test_qx_control_bytes_and_truncated);
    return UNITY_END();
}
