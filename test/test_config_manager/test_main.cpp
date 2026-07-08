#include <Arduino.h>
#include <unity.h>
#include <LittleFS.h>
#include "config_manager.h"

ConfigManager config_manager;

void setUp(void) {
    // Inizializza LittleFS all'inizio di ogni test
    LittleFS.begin(true);
}

void tearDown(void) {
    // Eventuale pulizia dopo ciascun test
}

void test_load_valid_config(void) {
    // Scrittura di un file di configurazione valido
    File file = LittleFS.open("/temp_valid.json", "w");
    TEST_ASSERT_TRUE(file);
    
    const char* valid_json = 
        "{\n"
        "  \"wifi\": {\n"
        "    \"ssid\": \"TEST_SSID\",\n"
        "    \"password\": \"TEST_PASS\"\n"
        "  },\n"
        "  \"nut\": {\n"
        "    \"username\": \"test_user\",\n"
        "    \"password\": \"test_nut_pass\",\n"
        "    \"ups_name\": \"test_ups\"\n"
        "  }\n"
        "}\n";
    
    file.print(valid_json);
    file.close();

    // Inizializzazione ConfigManager con il file di test
    TEST_ASSERT_TRUE(config_manager.begin("/temp_valid.json"));
    TEST_ASSERT_TRUE(config_manager.isValid());

    // Verifica credenziali Wi-Fi
    WifiConfig wifi = config_manager.getWifiConfig();
    TEST_ASSERT_EQUAL_STRING("TEST_SSID", wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("TEST_PASS", wifi.password.c_str());

    // Verifica credenziali NUT
    NutConfig nut = config_manager.getNutConfig();
    TEST_ASSERT_EQUAL_STRING("test_user", nut.username.c_str());
    TEST_ASSERT_EQUAL_STRING("test_nut_pass", nut.password.c_str());
    TEST_ASSERT_EQUAL_STRING("test_ups", nut.ups_name.c_str());

    // Rimozione del file temporaneo
    LittleFS.remove("/temp_valid.json");
}

void test_load_missing_file(void) {
    // Assicuriamoci che il file non esista
    LittleFS.remove("/temp_missing.json");

    // Inizializzazione ConfigManager con un file inesistente
    TEST_ASSERT_FALSE(config_manager.begin("/temp_missing.json"));
    TEST_ASSERT_FALSE(config_manager.isValid());
}

void test_load_malformed_json(void) {
    // Scrittura di un file JSON non valido
    File file = LittleFS.open("/temp_malformed.json", "w");
    TEST_ASSERT_TRUE(file);
    
    const char* malformed_json = "{ \"wifi\": { \"ssid\": \"incomplete_json\" "; // Mancano le parentesi graffe di chiusura
    file.print(malformed_json);
    file.close();

    // Inizializzazione ConfigManager con il file malformato
    TEST_ASSERT_FALSE(config_manager.begin("/temp_malformed.json"));
    TEST_ASSERT_FALSE(config_manager.isValid());

    // Rimozione del file temporaneo
    LittleFS.remove("/temp_malformed.json");
}

void setup() {
    delay(2000); // Stabilizzazione porta seriale

    UNITY_BEGIN();
    RUN_TEST(test_load_valid_config);
    RUN_TEST(test_load_missing_file);
    RUN_TEST(test_load_malformed_json);
    UNITY_END();
}

void loop() {
    // Loop vuoto
}
