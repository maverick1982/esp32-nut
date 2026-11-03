#ifndef CORE_HOSTNAME_STORE_H
#define CORE_HOSTNAME_STORE_H

#include <Arduino.h>

/**
 * Hostname chosen in the Network page (US-113), in the NVS namespace "net", key
 * "hostname". Its own namespace, not the config_json of ConfigManager: saving it from
 * the first-setup access point must not create a Wi-Fi configuration. No key means the
 * default name of the link (NetHostname::defaultFor).
 */
class HostnameStore {
public:
    // The saved name, "" when none is saved or the saved value is not valid (WARN)
    String load();
    // "" removes the key: back to the default name
    bool save(const String& name);
};

#endif // CORE_HOSTNAME_STORE_H
