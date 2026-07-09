#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include "config_manager.h"
#include "network_manager.h"
#include "NUTServer.h"
#include "DiagnosticLED.h"

// Definizioni globali ed intestazioni del firmware
#define MONITOR_BAUD_RATE 115200

// Calcola lo stato diagnostico del sistema a partire dallo stato Wi-Fi e UPS
LedState computeSystemState(bool wifiConnected, bool upsConnected);

#endif // MAIN_H


