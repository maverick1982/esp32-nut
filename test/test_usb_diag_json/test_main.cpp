#include <Arduino.h>
#include <unity.h>
#include <ArduinoJson.h>
#include <climits>
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

// Copia fedele di USBHostUPS::buildUSBDiagnostics() (commit 975df38, US-095), che legge
// dall'istantanea invece che dai membri dell'host
static void buildReferenceDoc(const UsbDiagSnapshot& s, JsonDocument& doc) {
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
    link["incomplete_input_reports"] = s.incomplete_input_reports;
    link["ep_in_mps"] = s.ep_in_mps;

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
    s.incomplete_input_reports = UINT32_MAX;
    s.ep_in_mps = UINT16_MAX;
    assertSameAsReference(s);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_eaton_fixture_same_bytes);
    RUN_TEST(test_empty_snapshot_same_bytes);
    RUN_TEST(test_escaped_strings_same_bytes);
    RUN_TEST(test_driver_none_and_flags);
    RUN_TEST(test_odd_descriptor_and_line_breaks);
    RUN_TEST(test_reports_edge_cases);
    RUN_TEST(test_numeric_limits);
    return UNITY_END();
}
