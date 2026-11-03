#ifndef NET_HOSTNAME_H
#define NET_HOSTNAME_H

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Hostname the board sends to the DHCP server on Wi-Fi and Ethernet (US-113).
 *
 * Pure logic, so it runs in the native unit tests. Rule of the spec: 1-32 characters,
 * only letters, digits and hyphens, no hyphen at the start or at the end. 32 is the
 * limit of esp_netif_set_hostname(), reached through the per-interface setHostname()
 * (WiFi.STA, ETH). WiFi.setHostname() is not used: it copies the name into a 32-byte
 * buffer, so it keeps at most 31 characters.
 *
 * Without a valid saved name the board uses esp32-nut-xxxxxx, from the last three bytes
 * of the MAC of the link, in lower case (the Ethernet name before US-113).
 */
namespace NetHostname {

constexpr size_t kMaxLength = 32;

inline bool isAsciiAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// ASCII ranges, not isalnum(): bytes outside ASCII are refused whatever the locale
inline bool isValid(const char* name) {
    if (!name) return false;
    size_t len = strlen(name);
    if (len == 0 || len > kMaxLength) return false;
    for (size_t i = 0; i < len; i++) {
        if (!isAsciiAlnum(name[i]) && name[i] != '-') return false;
    }
    return name[0] != '-' && name[len - 1] != '-';
}

inline String defaultFor(const uint8_t mac[6]) {
    char name[24];
    snprintf(name, sizeof(name), "esp32-nut-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(name);
}

// The saved name when valid, otherwise the default of the link
inline String effective(const String& saved, const uint8_t mac[6]) {
    return isValid(saved.c_str()) ? saved : defaultFor(mac);
}

} // namespace NetHostname

#endif // NET_HOSTNAME_H
