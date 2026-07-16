#include <Arduino.h>
#include <unity.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "config_manager.h"

ConfigManager config_manager;
Preferences test_preferences;

void setUp(void) {
    // Inizializza LittleFS e NVS all'inizio di ogni test
    LittleFS.begin(true);
    test_preferences.begin("nutos", false);
    test_preferences.clear();
    test_preferences.end();
}

void tearDown(void) {
    // Pulizia dopo ciascun test
}

void test_save_and_load_config(void) {
    // Inizializzazione pulita
    TEST_ASSERT_FALSE(config_manager.begin("/config.json")); // Dovrebbe fallire perché NVS e FS vuoti
    
    WifiConfig newWifi = {"NVS_SSID", "NVS_PASS"};
    NutConfig newNut = {"nvs_user", "nvs_nut_pass", "nvs_ups"};
    config_manager.setWifiConfig(newWifi);
    config_manager.setNutConfig(newNut);
    
    // Salva in NVS
    TEST_ASSERT_TRUE(config_manager.save());
    
    // Ricarica per verificare
    ConfigManager new_manager;
    TEST_ASSERT_TRUE(new_manager.begin());
    TEST_ASSERT_TRUE(new_manager.isValid());
    
    WifiConfig wifi = new_manager.getWifiConfig();
    TEST_ASSERT_EQUAL_STRING("NVS_SSID", wifi.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("NVS_PASS", wifi.password.c_str());
    
    NutConfig nut = new_manager.getNutConfig();
    TEST_ASSERT_EQUAL_STRING("nvs_user", nut.username.c_str());
    TEST_ASSERT_EQUAL_STRING("nvs_nut_pass", nut.password.c_str());
    TEST_ASSERT_EQUAL_STRING("nvs_ups", nut.ups_name.c_str());
}

void test_migration(void) {
    // Scrittura di un file legacy su LittleFS
    File file = LittleFS.open("/config.json", "w");
    TEST_ASSERT_TRUE(file);
    
    const char* valid_json = 
        "{\n"
        "  \"wifi\": {\n"
        "    \"ssid\": \"LEGACY_SSID\",\n"
        "    \"password\": \"LEGACY_PASS\"\n"
        "  },\n"
        "  \"nut\": {\n"
        "    \"username\": \"legacy_user\",\n"
        "    \"password\": \"legacy_nut_pass\",\n"
        "    \"ups_name\": \"legacy_ups\"\n"
        "  }\n"
        "}\n";
    
    file.print(valid_json);
    file.close();

    // Caricamento innesca la migrazione
    TEST_ASSERT_TRUE(config_manager.begin("/config.json"));
    TEST_ASSERT_TRUE(config_manager.isValid());

    // Verifica credenziali migrate
    WifiConfig wifi = config_manager.getWifiConfig();
    TEST_ASSERT_EQUAL_STRING("LEGACY_SSID", wifi.ssid.c_str());
    
    // Verifica che il file originale sia stato rinominato
    TEST_ASSERT_FALSE(LittleFS.exists("/config.json"));
    TEST_ASSERT_TRUE(LittleFS.exists("/config.json.bak"));
    
    LittleFS.remove("/config.json.bak");
}

void test_load_missing_file(void) {
    // Inizializzazione ConfigManager con NVS vuota e file inesistente
    TEST_ASSERT_FALSE(config_manager.begin("/temp_missing.json"));
    TEST_ASSERT_FALSE(config_manager.isValid());
}

void setup() {
    delay(2000); // Stabilizzazione porta seriale

    UNITY_BEGIN();
    RUN_TEST(test_save_and_load_config);
    RUN_TEST(test_migration);
    RUN_TEST(test_load_missing_file);
    UNITY_END();
}

void loop() {
}
