#ifndef WEB_API_JSON_H
#define WEB_API_JSON_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "IUSBHostUPS.h"

class WebApiJson {
public:
    static String generateUpsVars(IUSBHostUPS* usb_ups);
};

#endif
