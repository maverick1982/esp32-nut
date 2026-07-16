#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

struct WifiConfig {
    String ssid;
    String password;
};

struct NutConfig {
    String username;
    String password;
    String ups_name;
};

class ConfigManager {
public:
    ConfigManager();
    bool begin(const char* filepath = "/config.json");
    
    WifiConfig getWifiConfig() const;
    NutConfig getNutConfig() const;
    bool isValid() const;
    
    void setWifiConfig(const WifiConfig& config);
    void setNutConfig(const NutConfig& config);
    bool save(const char* filepath = "/config.json");

private:
    WifiConfig wifi_config;
    NutConfig nut_config;
    bool is_valid;
    Preferences preferences;
};

#endif // CONFIG_MANAGER_H
