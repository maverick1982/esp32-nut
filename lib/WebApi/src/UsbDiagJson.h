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
// US-119 aggiunge l'oggetto "export_read" dopo "link" e, solo nei report letti su richiesta,
// le chiavi "on_demand" ed "empty": i report del poll restano come prima.
// US-136 aggiunge l'oggetto "qx" dopo "export_read", solo con un UPS Q* agganciato: trasporto,
// protocollo, esito del riconoscimento e ultimi 20 scambi (risposta in testo con escape JSON
// valido per ogni byte di controllo, ed esadecimale). L'export HID non cambia.
// Lo verifica test/test_usb_diag_json.
class UsbDiagJson {
public:
    static void write(const UsbDiagSnapshot& s, ChunkWriter& w);
};

#endif // USB_DIAG_JSON_H
