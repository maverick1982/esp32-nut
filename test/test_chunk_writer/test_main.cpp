#include <unity.h>
#include <ArduinoJson.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "ChunkWriter.h"

// Export a pezzi di /api/usb/dump (US-095): l'uscita del ChunkWriter deve essere identica,
// byte per byte, a quella serializzata in un'unica stringa, con qualsiasi dimensione del buffer

static const size_t BUFFER_SIZES[] = {1, 7, 64, 1024, 4096};

struct ChunkResult {
    std::string out;
    std::vector<size_t> chunks;  // dimensione di ogni chiamata al sink
    size_t total = 0;
};

static ChunkResult serializeChunked(const JsonDocument& doc, size_t cap) {
    ChunkResult r;
    std::vector<char> buf(cap);
    ChunkWriter w(buf.data(), cap, [&r](const char* data, size_t n) {
        r.out.append(data, n);
        r.chunks.push_back(n);
    });
    serializeJsonPretty(doc, w);
    w.flush();
    r.total = w.total();
    return r;
}

static void assertSameOutput(const JsonDocument& doc) {
    std::string expected;
    serializeJsonPretty(doc, expected);
    TEST_ASSERT_TRUE(expected.size() > 0);
    for (size_t cap : BUFFER_SIZES) {
        ChunkResult r = serializeChunked(doc, cap);
        char msg[48];
        snprintf(msg, sizeof(msg), "buffer of %u bytes", (unsigned)cap);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), r.out.size(), msg);
        TEST_ASSERT_TRUE_MESSAGE(expected == r.out, msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), r.total, msg);
        // Blocchi pieni tranne l'ultimo: un writer che mandasse un byte per volta (migliaia
        // di chunk HTTP) darebbe la stessa uscita ma fallirebbe qui
        TEST_ASSERT_EQUAL_size_t_MESSAGE((expected.size() + cap - 1) / cap, r.chunks.size(), msg);
        for (size_t i = 0; i < r.chunks.size(); ++i) {
            TEST_ASSERT_TRUE_MESSAGE(r.chunks[i] > 0, msg);
            if (i + 1 < r.chunks.size()) TEST_ASSERT_EQUAL_size_t_MESSAGE(cap, r.chunks[i], msg);
        }
    }
}

// Documento con la forma dell'export di USBHostUPS::buildUSBDiagnostics(): parti grezze
// serialized() per il descriptor e i dati dei report, oggetti annidati, stringhe da escapare
static void buildDumpLikeDoc(JsonDocument& doc) {
    doc["firmware_version"] = "idf55-v2";
    doc["vid"] = "463";
    doc["pid"] = "ffff";
    doc["manufacturer"] = "EATON";
    doc["product"] = "Eaton 3S";
    doc["serial_number"] = "                ";

    std::string arr = "[\n";
    for (int i = 0; i < 742; ++i) {
        if (i > 0 && i % 10 == 0) arr += ",\n    ";
        else if (i > 0) arr += ", ";
        else arr += "    ";
        char b[8];
        snprintf(b, sizeof(b), "\"0x%02X\"", i & 0xFF);
        arr += b;
    }
    arr += "\n  ]";
    doc["report_descriptor_hex"] = serialized(arr);

    doc["quirks"] = 0;
    doc["driver"] = "EatonDriver";

    JsonObject link = doc["link"].to<JsonObject>();
    link["failures"] = 0;
    link["recoveries"] = 0;
    link["stale"] = false;
    link["input_period_ms"] = 2000;

    JsonArray scenarios = doc["scenarios"].to<JsonArray>();
    JsonObject scenario = scenarios.add<JsonObject>();
    scenario["description"] = "Live ESP32 dump";
    JsonArray reports = scenario["reports"].to<JsonArray>();
    for (int id = 1; id <= 100; ++id) {
        JsonObject report = reports.add<JsonObject>();
        report["id"] = id;
        report["type"] = 3;
        std::string data = "[" + std::to_string(id);
        for (int k = 0; k < id % 9; ++k) data += ", " + std::to_string((id * 31 + k) & 0xFF);
        data += "]";
        report["data"] = serialized(data);
    }

    JsonObject expected = scenario["expected_ups_data"].to<JsonObject>();
    expected["ups.mfr"] = "EATON";
    expected["ups.model"] = "Model \"3S\" 700";
    expected["ups.serial"] = "back\\slash";
    expected["ups.test.result"] = "line1\nline2\ttab";
    expected["ambient.temperature.unit"] = "\xC2\xB0" "C";  // °C in UTF-8
    expected["battery.charge"] = "100";
}

void test_real_fixture_same_bytes(void) {
    std::ifstream f("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json");
    TEST_ASSERT_TRUE_MESSAGE(f.is_open(), "cannot open the Eaton 3S fixture");
    std::stringstream ss;
    ss << f.rdbuf();
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, ss.str()) == DeserializationError::Ok);
    assertSameOutput(doc);
}

void test_dump_shaped_doc_same_bytes(void) {
    JsonDocument doc;
    buildDumpLikeDoc(doc);
    assertSameOutput(doc);
}

void test_flush_twice_sends_nothing_more(void) {
    std::vector<size_t> calls;
    char buf[16];
    ChunkWriter w(buf, sizeof(buf), [&calls](const char*, size_t n) { calls.push_back(n); });
    const uint8_t data[] = "abcdefghijklmnopqrst";  // 20 byte + terminatore, non scritto
    w.write(data, 20);
    w.flush();
    w.flush();
    TEST_ASSERT_EQUAL_size_t(2, calls.size());
    TEST_ASSERT_EQUAL_size_t(16, calls[0]);
    TEST_ASSERT_EQUAL_size_t(4, calls[1]);
}

void test_flush_without_data_sends_nothing(void) {
    int calls = 0;
    char buf[8];
    ChunkWriter w(buf, sizeof(buf), [&calls](const char*, size_t) { calls++; });
    w.flush();
    TEST_ASSERT_EQUAL_INT(0, calls);
    TEST_ASSERT_EQUAL_size_t(0, w.total());
}

void test_exact_multiple_of_buffer_has_no_empty_chunk(void) {
    std::vector<size_t> calls;
    char buf[4];
    ChunkWriter w(buf, sizeof(buf), [&calls](const char*, size_t n) { calls.push_back(n); });
    for (uint8_t c = 'a'; c < 'a' + 8; ++c) w.write(c);
    w.flush();
    TEST_ASSERT_EQUAL_size_t(2, calls.size());
    TEST_ASSERT_EQUAL_size_t(4, calls[0]);
    TEST_ASSERT_EQUAL_size_t(4, calls[1]);
}

// write() a blocchi con il buffer già in parte pieno: 3 byte singoli, poi 20 in un colpo
void test_block_write_on_partly_full_buffer(void) {
    std::vector<size_t> calls;
    std::string out;
    char buf[8];
    ChunkWriter w(buf, sizeof(buf), [&](const char* data, size_t n) {
        calls.push_back(n);
        out.append(data, n);
    });
    w.write('A');
    w.write('B');
    w.write('C');
    const uint8_t block[] = "defghijklmnopqrstuvw";  // 20 byte + terminatore, non scritto
    TEST_ASSERT_EQUAL_size_t(20, w.write(block, 20));
    TEST_ASSERT_EQUAL_size_t(2, calls.size());
    w.flush();
    TEST_ASSERT_EQUAL_size_t(3, calls.size());
    TEST_ASSERT_EQUAL_size_t(8, calls[0]);
    TEST_ASSERT_EQUAL_size_t(8, calls[1]);
    TEST_ASSERT_EQUAL_size_t(7, calls[2]);
    TEST_ASSERT_EQUAL_STRING("ABCdefghijklmnopqrstuvw", out.c_str());
    TEST_ASSERT_EQUAL_size_t(23, w.total());
}

void test_short_output_is_one_chunk(void) {
    JsonDocument doc;
    doc["a"] = 1;
    ChunkResult r = serializeChunked(doc, 1024);
    std::string expected;
    serializeJsonPretty(doc, expected);
    TEST_ASSERT_EQUAL_size_t(1, r.chunks.size());
    TEST_ASSERT_EQUAL_size_t(expected.size(), r.chunks[0]);
    TEST_ASSERT_EQUAL_size_t(expected.size(), r.total);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_real_fixture_same_bytes);
    RUN_TEST(test_dump_shaped_doc_same_bytes);
    RUN_TEST(test_flush_twice_sends_nothing_more);
    RUN_TEST(test_flush_without_data_sends_nothing);
    RUN_TEST(test_exact_multiple_of_buffer_has_no_empty_chunk);
    RUN_TEST(test_block_write_on_partly_full_buffer);
    RUN_TEST(test_short_output_is_one_chunk);
    return UNITY_END();
}
