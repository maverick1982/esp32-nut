#include "main.h"

void setup() {
    // Inizializzazione della porta seriale per il debug diagnostico
    Serial.begin(MONITOR_BAUD_RATE);
    delay(1000); // Piccolo delay per stabilizzare la connessione seriale
    Serial.println("\n--- ESP32 NUT Server Initialized ---");
}

void loop() {
    // Ciclo principale vuoto per il firmware di bootstrap
    delay(1000);
}
