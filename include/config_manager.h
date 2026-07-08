#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>

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

private:
    WifiConfig wifi_config;
    NutConfig nut_config;
    bool is_valid;
};

#endif // CONFIG_MANAGER_H
