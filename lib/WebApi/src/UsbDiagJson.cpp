#include "UsbDiagJson.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

void raw(ChunkWriter& w, const char* s) {
    w.write(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

// A capo del pretty print di ArduinoJson (CRLF) e rientro di 2 spazi per livello
void newline(ChunkWriter& w, int level) {
    raw(w, "\r\n");
    for (int i = 0; i < level; ++i) raw(w, "  ");
}

// Stringa JSON con l'escape di ArduinoJson (EscapeSequence.hpp): solo virgolette, backslash,
// \b \f \n \r \t e il byte nullo; gli altri caratteri di controllo e i byte UTF-8 passano
// invariati
void str(ChunkWriter& w, const String& s) {
    w.write('"');
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s.c_str()[i];
        const char* esc = nullptr;
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\b': esc = "\\b"; break;
            case '\f': esc = "\\f"; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            case '\0': esc = "\\u0000"; break;  // TextFormatter::writeChar()
            default: break;
        }
        if (esc) raw(w, esc);
        else w.write(static_cast<uint8_t>(c));
    }
    w.write('"');
}

void num(ChunkWriter& w, uint32_t v) {
    char buf[11];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(v));
    raw(w, buf);
}

// Chiave dell'oggetto al livello dato: a capo, rientro, "chiave":
void key(ChunkWriter& w, int level, const char* k, bool first) {
    if (!first) w.write(',');
    newline(w, level);
    w.write('"');
    raw(w, k);
    raw(w, "\": ");
}

// Descriptor come lo scriveva buildUSBDiagnostics(): 10 valori "0xHH" per riga, a capo LF
void descriptor(ChunkWriter& w, const String& hex) {
    raw(w, "[\n");
    const size_t len = hex.length();
    for (size_t i = 0; i < len; i += 2) {
        if (i > 0 && (i / 2) % 10 == 0) raw(w, ",\n    ");
        else if (i > 0) raw(w, ", ");
        else raw(w, "    ");
        raw(w, "\"0x");
        // Come substring(i, i + 2): l'ultima cifra di una stringa dispari resta da sola
        for (size_t j = i; j < i + 2 && j < len; ++j) {
            w.write(static_cast<uint8_t>(toupper(static_cast<unsigned char>(hex.c_str()[j]))));
        }
        w.write('"');
    }
    raw(w, "\n  ]");
}

// Dati di un report come li scriveva buildUSBDiagnostics(): [1, 2, 3]
void reportData(ChunkWriter& w, const std::vector<uint8_t>& data) {
    w.write('[');
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) raw(w, ", ");
        num(w, data[i]);
    }
    w.write(']');
}

}  // namespace

void UsbDiagJson::write(const UsbDiagSnapshot& s, ChunkWriter& w) {
    w.write('{');
    key(w, 1, "firmware_version", true);  str(w, s.firmware_version);
    key(w, 1, "vid", false);              str(w, s.vid);
    key(w, 1, "pid", false);              str(w, s.pid);
    key(w, 1, "manufacturer", false);     str(w, s.manufacturer);
    key(w, 1, "product", false);          str(w, s.product);
    key(w, 1, "serial_number", false);    str(w, s.serial_number);
    key(w, 1, "report_descriptor_hex", false); descriptor(w, s.descriptor_hex);
    key(w, 1, "quirks", false);           num(w, s.quirks);
    key(w, 1, "driver", false);           str(w, s.driver);

    key(w, 1, "link", false);
    w.write('{');
    key(w, 2, "failures", true);                 num(w, s.link_failures);
    key(w, 2, "recoveries", false);              num(w, s.link_recoveries);
    key(w, 2, "stale", false);                   raw(w, s.stale ? "true" : "false");
    key(w, 2, "dropped_input_reports", false);   num(w, s.dropped_input_reports);
    key(w, 2, "input_period_ms", false);         num(w, s.input_period_ms);
    key(w, 2, "input_recoveries", false);        num(w, s.input_recoveries);
    key(w, 2, "incomplete_input_reports", false); num(w, s.incomplete_input_reports);
    key(w, 2, "ep_in_mps", false);               num(w, s.ep_in_mps);
    newline(w, 1);
    w.write('}');

    // scenarios: un solo oggetto, quindi l'array non è mai vuoto
    key(w, 1, "scenarios", false);
    w.write('[');
    newline(w, 2);
    w.write('{');
    key(w, 3, "description", true);
    raw(w, "\"Live ESP32 dump\"");

    key(w, 3, "reports", false);
    if (s.reports.empty()) {
        raw(w, "[]");
    } else {
        w.write('[');
        for (size_t i = 0; i < s.reports.size(); ++i) {
            if (i > 0) w.write(',');
            newline(w, 4);
            const UsbDiagReport& r = s.reports[i];
            w.write('{');
            key(w, 5, "id", true);    num(w, r.id);
            key(w, 5, "type", false); num(w, r.type);
            key(w, 5, "data", false); reportData(w, r.data);
            newline(w, 4);
            w.write('}');
        }
        newline(w, 3);
        w.write(']');
    }

    key(w, 3, "expected_ups_data", false);
    if (s.ups_data.empty()) {
        raw(w, "{}");
    } else {
        w.write('{');
        for (size_t i = 0; i < s.ups_data.size(); ++i) {
            if (i > 0) w.write(',');
            newline(w, 4);
            str(w, s.ups_data[i].key);
            raw(w, ": ");
            str(w, s.ups_data[i].value);
        }
        newline(w, 3);
        w.write('}');
    }

    newline(w, 2);
    w.write('}');
    newline(w, 1);
    w.write(']');
    newline(w, 0);
    w.write('}');
}
