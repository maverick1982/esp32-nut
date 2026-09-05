#include "WebApiJson.h"

String WebApiJson::generateUpsVars(IUSBHostUPS* usb_ups) {
    if (!usb_ups) {
        return "{\"error\": \"UPS non inizializzato\"}";
    }
    
    JsonDocument doc;
    
    if (!usb_ups->isConnected()) {
        doc["_disconnected"] = true;
        doc["ups.status"] = "Disconnected";
        std::string out_std;
        serializeJson(doc, out_std);
        return String(out_std.c_str());
    }
    
    auto data = usb_ups->getUPSData();
    
    doc["ups.status"] = usb_ups->getUPSStatusString();
    
    for (const auto& param : data->getAll()) {
        if (param.key.startsWith("ups.status.") && param.key != "ups.status") {
            continue; // Skip internal status flags
        }
        
        bool isNumeric = true;
        bool hasDot = false;
        if (param.value.length() == 0) isNumeric = false;
        for (int i = 0; i < param.value.length(); i++) {
            if (i == 0 && param.value[i] == '-') continue;
            if (param.value[i] == '.') {
                if (hasDot) { isNumeric = false; break; }
                hasDot = true;
                continue;
            }
            if (!isdigit(param.value[i])) {
                isNumeric = false;
                break;
            }
        }
        
        if (isNumeric) {
            if (hasDot) doc[param.key] = serialized(param.value); // Use serialized for floats to preserve formatting
            else doc[param.key] = param.value.toInt();
        } else {
            doc[param.key] = param.value;
        }
    }
    
    doc["ups.beeper.switchable"] = usb_ups->supportsBeeperToggle();

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}
