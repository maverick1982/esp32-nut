#ifndef USB_DIAG_JSON_H
#define USB_DIAG_JSON_H

#include "ChunkWriter.h"
#include "UsbDiagSnapshot.h"

// Export della diagnostica USB (/api/usb/dump) scritto direttamente sul ChunkWriter, senza
// JsonDocument (US-096): il picco di heap di un export resta quello dell'istantanea.
// L'uscita è identica, byte per byte, a serializeJsonPretty del documento che costruiva
// USBHostUPS::buildUSBDiagnostics() fino alla US-095 (regole di ArduinoJson 7.4.3: a capo
// CRLF, rientro di 2 spazi, escape di virgolette, backslash, \b \f \n \r \t e del byte nullo
// come \u0000). Il descriptor e i dati dei report restano parti grezze con la loro
// formattazione storica (a capo LF).
// Lo verifica test/test_usb_diag_json.
class UsbDiagJson {
public:
    static void write(const UsbDiagSnapshot& s, ChunkWriter& w);
};

#endif // USB_DIAG_JSON_H
