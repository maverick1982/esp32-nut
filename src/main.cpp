#include "main.h"
#include "USBHostUPS.h"

USBHostUPS usb_ups;

void setup() {
    // Inizializzazione della porta seriale per il debug diagnostico
    Serial.begin(MONITOR_BAUD_RATE);
    delay(1000); // Piccolo delay per stabilizzare la connessione seriale
    Serial.println("\n--- ESP32 NUT Server Initialized ---");

    // Inizializzazione della libreria USBHostUPS
    if (!usb_ups.begin()) {
        Serial.println("[MAIN] ERRORE: Inizializzazione USBHostUPS fallita!");
    } else {
        Serial.println("[MAIN] USBHostUPS inizializzato correttamente.");
    }
}

void loop() {
    usb_ups.loop();
    delay(10);
}
