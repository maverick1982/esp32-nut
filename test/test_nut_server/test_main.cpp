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

    // report 0x0e: output.voltage = 230V
    uint8_t r3[3] = {0x0e, 0xE6, 0x00}; // 230 in hex is 0x00E6 (little-endian: E6 00)
    test_ups.decodeReport(0x0e, r3, 3);

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

    // 8. Richiedi una singola variabile: output.voltage
    client.println("GET VAR eaton output.voltage");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n');
    resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton output.voltage \"230\"", resp.c_str());

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
    TEST_ASSERT_EQUAL_STRING("VAR eaton output.voltage \"230\"", resp.c_str());

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
    TEST_ASSERT_EQUAL_STRING("VAR EATON output.voltage \"230\"", resp.c_str());

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

    // C. GET VAR con case misto solo per variabile: GET VAR eaton OUTPUT.VOLTAGE
    client.println("GET VAR eaton OUTPUT.VOLTAGE");
    client.flush();
    for (int i = 0; i < 5; i++) {
        test_server.loop();
        delay(10);
    }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("VAR eaton output.voltage \"230\"", resp.c_str());

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

    // J. Test Comandi Esplorativi Dummy (TASK-02)
    client.println("LIST CMD eaton");
    client.flush();
    for (int i = 0; i < 5; i++) { test_server.loop(); delay(10); }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST CMD eaton", resp.c_str());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("END LIST CMD eaton", resp.c_str());

    client.println("LIST ENUM eaton battery.charge");
    client.flush();
    for (int i = 0; i < 5; i++) { test_server.loop(); delay(10); }
    TEST_ASSERT_TRUE(client.available());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("BEGIN LIST ENUM eaton battery.charge", resp.c_str());
    resp = client.readStringUntil('\n'); resp.trim();
    TEST_ASSERT_EQUAL_STRING("END LIST ENUM eaton battery.charge", resp.c_str());

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

void test_comprehensive_variables(void) {
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

    // Autenticazione
    client.println("USERNAME admin");
    client.flush();
    for(int i=0; i<5; i++){test_server.loop(); delay(10);}
    client.readStringUntil('\n'); // Consuma OK

    client.println("PASSWORD nut_password");
    client.flush();
    for(int i=0; i<5; i++){test_server.loop(); delay(10);}
    client.readStringUntil('\n'); // Consuma OK

    // MOCK HID PAYLOAD - EATON 3S (formato Little Endian)
    
    // report 0x01: acPresent (bit 0) = 1
    uint8_t r1[] = {0x01, 0x01, 0x00, 0x00}; 
    test_ups.decodeReport(0x01, r1, sizeof(r1));

    // report 0x02: outlet.1.switch, outlet.2.switch
    uint8_t r2[] = {0x02, 0x01, 0x00}; // outlet1 = 1, outlet2 = 0
    test_ups.decodeReport(0x02, r2, sizeof(r2));

    // report 0x06: remainingCapacity (offset 1), runTimeToEmpty (offset 2..5, 4 bytes)
    // cap = 100 (0x64), runtime = 1800 (0x00000708)
    uint8_t r6[] = {0x06, 0x64, 0x08, 0x07, 0x00, 0x00};
    test_ups.decodeReport(0x06, r6, sizeof(r6));

    // report 0x08: remainingCapacityLimit
    uint8_t r8[] = {0x08, 0x14}; // 20%
    test_ups.decodeReport(0x08, r8, sizeof(r8));

    // report 0x0c: designCapacity, fullChargeCapacity (offset 4, 5)
    uint8_t r0c[] = {0x0c, 0, 0, 0, 0, 100, 100}; 
    test_ups.decodeReport(0x0c, r0c, sizeof(r0c));

    // report 0x0d: configApparentPower (offset 1..2), configFrequency (offset 3)
    // 700 VA (0x02BC) little endian -> BC 02
    // 50 Hz (0x32)
    uint8_t r0d[] = {0x0d, 0xBC, 0x02, 0x32};
    test_ups.decodeReport(0x0d, r0d, sizeof(r0d));

    // report 0x0e: outputVoltage (offset 1..2)
    // 230V -> 0x00E6 -> E6 00
    uint8_t r0e[] = {0x0e, 0xE6, 0x00};
    test_ups.decodeReport(0x0e, r0e, sizeof(r0e));

    // report 0x12: configVoltage
    // 230
    uint8_t r12[] = {0x12, 230};
    test_ups.decodeReport(0x12, r12, sizeof(r12));

    // report 0x13: highVoltageTransfer
    // 264V -> 0x0108 -> 08 01
    uint8_t r13[] = {0x13, 0x08, 0x01};
    test_ups.decodeReport(0x13, r13, sizeof(r13));

    // report 0x14: lowVoltageTransfer
    // 161V
    uint8_t r14[] = {0x14, 161};
    test_ups.decodeReport(0x14, r14, sizeof(r14));

    // VERIFY ALL VARS via GET VAR eaton
    const char* vars[] = {
        "ups.status", "OL",
        "ups.mfr", "Unknown",
        "ups.model", "Unknown",
        "battery.charge", "100",
        "battery.charge.low", "20",
        "battery.capacity", "100",
        "battery.charge.full", "100",
        "battery.runtime", "1800",
        "output.voltage", "230",
        "input.transfer.high", "264",
        "input.transfer.low", "161",
        "ups.power.nominal", "700",
        "input.frequency.nominal", "50",
        "input.voltage.nominal", "230",
        "outlet.1.switch", "1",
        "outlet.2.switch", "0",
        "ups.mfr", "Unknown",
        "ups.model", "Unknown"
    };

    for(size_t i = 0; i < sizeof(vars)/sizeof(vars[0]); i+=2) {
        client.printf("GET VAR eaton %s\n", vars[i]);
        client.flush();
        for(int j=0; j<5; j++){test_server.loop(); delay(10);}
        String resp = client.readStringUntil('\n');
        resp.trim();
        String expected = String("VAR eaton ") + vars[i] + " \"" + vars[i+1] + "\"";
        TEST_ASSERT_EQUAL_STRING(expected.c_str(), resp.c_str());
    }

    client.println("LOGOUT");
    client.flush();
    for(int i=0; i<5; i++){test_server.loop(); delay(10);}
    
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
    RUN_TEST(test_comprehensive_variables);
    UNITY_END();
}

void loop() {
    // Loop vuoto
}
