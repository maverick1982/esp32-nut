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

void snum(ChunkWriter& w, int32_t v) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%ld", static_cast<long>(v));
    raw(w, buf);
}

// Raw Q* bytes as a JSON string (US-136): quotes, backslash, \r \n \t by name, every other
// byte below 0x20 or from 0x7F as \u00XX, so any answer gives valid JSON
void textBytes(ChunkWriter& w, const uint8_t* data, size_t len) {
    w.write('"');
    for (size_t i = 0; i < len; ++i) {
        const uint8_t c = data[i];
        switch (c) {
            case '"':  raw(w, "\\\""); continue;
            case '\\': raw(w, "\\\\"); continue;
            case '\r': raw(w, "\\r"); continue;
            case '\n': raw(w, "\\n"); continue;
            case '\t': raw(w, "\\t"); continue;
            default: break;
        }
        if (c < 0x20 || c >= 0x7F) {
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04X", static_cast<unsigned>(c));
            raw(w, esc);
        } else {
            w.write(c);
        }
    }
    w.write('"');
}

// "28 32 33 0D": two upper case digits per byte, separated by a space
void hexBytes(ChunkWriter& w, const uint8_t* data, size_t len) {
    w.write('"');
    for (size_t i = 0; i < len; ++i) {
        char h[4];
        snprintf(h, sizeof(h), i > 0 ? " %02X" : "%02X", static_cast<unsigned>(data[i]));
        raw(w, h);
    }
    w.write('"');
}

// The "qx" object (US-136): transport, protocol, probing outcome and the last exchanges
void qxSection(ChunkWriter& w, const UsbDiagQx& q) {
    key(w, 1, "qx", false);
    w.write('{');
    key(w, 2, "transport", true);        str(w, q.transport);
    key(w, 2, "protocol", false);        str(w, q.protocol);
    key(w, 2, "probe", false);           str(w, q.probe);
    key(w, 2, "tries", false);           num(w, q.tries);
    key(w, 2, "failed_cycles", false);   num(w, q.failed_cycles);
    key(w, 2, "retry_ms", false);        num(w, q.retry_ms);
    key(w, 2, "forced_protocol", false); str(w, q.forced_protocol);
    key(w, 2, "exchanges", false);
    if (q.exchanges.empty()) {
        raw(w, "[]");
    } else {
        w.write('[');
        for (size_t i = 0; i < q.exchanges.size(); ++i) {
            const UsbDiagQxExchange& e = q.exchanges[i];
            if (i > 0) w.write(',');
            newline(w, 3);
            w.write('{');
            key(w, 4, "uptime_ms", true);
            num(w, e.uptime_ms);
            if (e.time.length() > 0) { key(w, 4, "time", false); str(w, e.time); }
            key(w, 4, "command", false);
            textBytes(w, reinterpret_cast<const uint8_t*>(e.command.c_str()), e.command.length());
            key(w, 4, "answer_text", false); textBytes(w, e.answer.data(), e.answer.size());
            key(w, 4, "answer_hex", false);  hexBytes(w, e.answer.data(), e.answer.size());
            key(w, 4, "length", false);      snum(w, e.length);
            key(w, 4, "duration_ms", false); num(w, e.duration_ms);
            key(w, 4, "truncated", false);   raw(w, e.truncated ? "true" : "false");
            key(w, 4, "error", false);       str(w, e.error);
            newline(w, 3);
            w.write('}');
        }
        newline(w, 2);
        w.write(']');
    }
    newline(w, 1);
    w.write('}');
}

// "state" of "export_read": an export without a read says so (a client that only calls
// GET /api/usb/dump)
const char* exportReadState(DiagSweep::State st) {
    switch (st) {
        case DiagSweep::State::Reading:     return "\"reading\"";
        case DiagSweep::State::Done:        return "\"done\"";
        case DiagSweep::State::Interrupted: return "\"interrupted\"";
        default:                            return "\"not_requested\"";
    }
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
    key(w, 2, "input_watchdog", false);          raw(w, s.input_watchdog ? "true" : "false");
    key(w, 2, "port_cycles", false);             num(w, s.port_cycles);
    key(w, 2, "incomplete_input_reports", false); num(w, s.incomplete_input_reports);
    key(w, 2, "ep_in_mps", false);               num(w, s.ep_in_mps);
    newline(w, 1);
    w.write('}');

    // Outcome of the on-demand read of the export (US-119)
    key(w, 1, "export_read", false);
    w.write('{');
    key(w, 2, "state", true);      raw(w, exportReadState(s.export_read.state));
    key(w, 2, "requested", false); num(w, s.export_read.total);
    key(w, 2, "read", false);      num(w, s.export_read.read);
    key(w, 2, "empty", false);     num(w, s.export_read.empty);
    key(w, 2, "failed", false);    num(w, s.export_read.failed);
    newline(w, 1);
    w.write('}');

    // Q* section (US-136), only with a Q* UPS: the HID export stays byte for byte as before
    if (s.qx.present) qxSection(w, s.qx);

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
            // Only for the reports of the on-demand read (US-119): the poll's reports stay
            // as before, byte for byte
            if (r.on_demand) { key(w, 5, "on_demand", false); raw(w, "true"); }
            if (r.empty) { key(w, 5, "empty", false); raw(w, "true"); }
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
