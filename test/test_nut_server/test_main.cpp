#include <Arduino.h>
#include <unity.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "config_manager.h"
#include "USBHostUPS.h"
#include "NUTServer.h"

ConfigManager test_config;
USBHostUPS test_ups;
NUTServer test_server;

void setUp(void) {
    // Si esegue prima di ogni test
}

void tearDown(void) {
    // Si esegue dopo ogni test
}

void test_tokenization(void) {
    // Test splitTokens standard
    std::vector<String> tokens1 = NUTServer::splitTokens("GET VAR eaton battery.charge");
    TEST_ASSERT_EQUAL(4, tokens1.size());
    TEST_ASSERT_EQUAL_STRING("GET", tokens1[0].c_str());
    TEST_ASSERT_EQUAL_STRING("VAR", tokens1[1].c_str());
    TEST_ASSERT_EQUAL_STRING("eaton", tokens1[2].c_str());
    TEST_ASSERT_EQUAL_STRING("battery.charge", tokens1[3].c_str());

    // Test splitTokens con argomenti tra virgolette
    std::vector<String> tokens2 = NUTServer::splitTokens("GET VAR eaton \"battery.charge\"");
    TEST_ASSERT_EQUAL(4, tokens2.size());
    TEST_ASSERT_EQUAL_STRING("GET", tokens2[0].c_str());
    TEST_ASSERT_EQUAL_STRING("VAR", tokens2[1].c_str());
    TEST_ASSERT_EQUAL_STRING("eaton", tokens2[2].c_str());
    TEST_ASSERT_EQUAL_STRING("battery.charge", tokens2[3].c_str()); // le virgolette devono essere rimosse
}

void test_auth_and_commands(void) {
    // Inizializza l'AP per consentire la connessione loopback locale
    WiFi.softAP("TestNUT_AP");
    IPAddress apIP = WiFi.softAPIP();
    
    // Avvia il server NUT per il test
    NutConfig nut_config = test_config.getNutConfig();
    NUTServerConfig server_config = {nut_config.username, nut_config.password, nut_config.ups_name};
    test_server.begin(server_config, &test_ups, 3493);

    // Connetti il client di test
    WiFiClient client;
    bool connected = false;
    for (int retry = 0; retry < 5; retry++) {
        if (client.connect(apIP, 3493)) {
            connected = true;
            break;
        }
        delay(100);
    }
    TEST_ASSERT_TRUE(connected);

    // Esegui loop del server per accettare il client
    test_server.loop();
    delay(50);

    // 1. Invia un comando prima dell'autenticazione -> deve fallire con ERR ACCESS-DENIED
    client.println("LIST UPS");
    client.flush();
    
    // Aspetta la risposta e fai processare al server
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    
    TEST_ASSERT_TRUE(client.available());
    String resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED", resp.c_str());

    // 2. Invia USERNAME corretto
    client.println("USERNAME admin");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("OK", resp.c_str());

    // 3. Invia PASSWORD errata
    client.println("PASSWORD wrong_pass");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR ACCESS-DENIED", resp.c_str());

    // 4. Invia PASSWORD corretta
    client.println("PASSWORD nut_password");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("OK", resp.c_str());

    // 5. Invia comando LIST UPS post-autenticazione -> deve avere successo
    client.println("LIST UPS");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    
    // Leggi risposta multiline
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST UPS", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("UPS eaton \"ESP32-S3 UPS Bridge\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("END LIST UPS", resp.c_str());

    // 6. Configura dati fittizi in USBHostUPS tramite decodeReport
    // report 0x01: battery.charge = 85
    uint8_t r1[2] = {0x01, 85};
    test_ups.decodeReport(0x01, r1, 2);

    // report 0x02: ups.status = 1 (OL)
    uint8_t r2[2] = {0x02, 1};
    test_ups.decodeReport(0x02, r2, 2);

    // report 0x03: input.voltage = 2300 (230.0V)
    uint8_t r3[3] = {0x03, 0xFC, 0x08}; // 2300 in hex is 0x08FC (little-endian: FC 08)
    test_ups.decodeReport(0x03, r3, 3);

    // 7. Richiedi una singola variabile: battery.charge
    client.println("GET VAR eaton battery.charge");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton battery.charge \"85\"", resp.c_str());

    // 8. Richiedi una singola variabile: input.voltage
    client.println("GET VAR eaton input.voltage");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton input.voltage \"230.0\"", resp.c_str());

    // 9. Richiedi una singola variabile: ups.status
    client.println("GET VAR eaton ups.status");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton ups.status \"OL\"", resp.c_str());

    // 10. Richiedi la lista di tutte le variabili: LIST VAR eaton
    client.println("LIST VAR eaton");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    
    // Leggi risposta multiline per LIST VAR
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST VAR eaton", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton battery.charge \"85\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton input.voltage \"230.0\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton ups.status \"OL\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("END LIST VAR eaton", resp.c_str());

    // 11. Richiesta variabile non supportata
    client.println("GET VAR eaton non.existent");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR VAR-NOT-SUPPORTED", resp.c_str());

    // --- Nuovi Test Case-Insensitivity e Gestione Errori (TASK-02) ---

    // A. LIST VAR con case misto per UPS: LIST VAR EATON
    client.println("LIST VAR EATON");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST VAR EATON", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR EATON battery.charge \"85\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR EATON input.voltage \"230.0\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR EATON ups.status \"OL\"", resp.c_str());

    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("END LIST VAR EATON", resp.c_str());

    // B. GET VAR con case misto per UPS e variabili: GET VAR EATON BATTERY.CHARGE
    client.println("GET VAR EATON BATTERY.CHARGE");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR EATON battery.charge \"85\"", resp.c_str());

    // C. GET VAR con case misto solo per variabile: GET VAR eaton INPUT.VOLTAGE
    client.println("GET VAR eaton INPUT.VOLTAGE");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton input.voltage \"230.0\"", resp.c_str());

    // D. GET VAR con case misto per comando e argomenti: get var EaToN ups.status
    client.println("get var EaToN ups.status");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR EaToN ups.status \"OL\"", resp.c_str());

    // E. Gestione Errori: UPS inesistente (es. GET VAR apc battery.charge)
    client.println("GET VAR apc battery.charge");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS", resp.c_str());

    // F. Gestione Errori: LIST VAR con UPS inesistente
    client.println("LIST VAR apc");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-UPS", resp.c_str());

    // G. Gestione Errori: Argomenti mancanti per GET VAR
    client.println("GET VAR");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT", resp.c_str());

    // H. Gestione Errori: Argomenti mancanti per LIST VAR
    client.println("LIST VAR");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR INVALID-ARGUMENT", resp.c_str());

    // I. Gestione Errori: Comando non supportato/sconosciuto
    client.println("INVALIDCMD");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("ERR UNKNOWN-COMMAND", resp.c_str());

    // 12. LOGOUT
    client.println("LOGOUT");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("OK Goodbye", resp.c_str());

    // Verifica che il client si disconnetta
    client.stop();
    WiFi.softAPdisconnect(true);
}

void setup() {
    delay(2000); // Stabilizzazione porta seriale

    // Inizializza LittleFS e crea file di configurazione di test
    LittleFS.begin(true);
    File f = LittleFS.open("/test_config.json", "w");
    if (f) {
        f.print("{\"wifi\":{\"ssid\":\"test_wifi\",\"password\":\"test_pass\"},\"nut\":{\"username\":\"admin\",\"password\":\"nut_password\",\"ups_name\":\"eaton\"}}");
        f.close();
    }

    // Carica la configurazione di test
    test_config.begin("/test_config.json");

    UNITY_BEGIN();
    RUN_TEST(test_tokenization);
    RUN_TEST(test_auth_and_commands);
    UNITY_END();
}

void loop() {
    // Loop vuoto
}
