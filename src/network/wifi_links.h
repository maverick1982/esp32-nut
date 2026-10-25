#ifndef WIFI_LINKS_H
#define WIFI_LINKS_H

#include <Arduino.h>
#include <WiFi.h>
#include "INetLink.h"

// Wi-Fi adapters of the net module (ADR 0010): the only code that calls WiFi.*

// Station on the home network, with the reconnection of the former AppNetworkManager
class WifiStaLink : public INetLink {
public:
    WifiStaLink(const String& ssid, const String& password);

    NetLinkKind kind() const override { return NetLinkKind::STATION; }
    void begin() override;
    void end() override;
    void loop(uint32_t now) override;
    bool isConnected() const override;
    String statusText() const override;
    String localIP() const override;
    void macAddress(uint8_t mac[6]) const override;

private:
    String m_ssid;
    String m_password;
    wl_status_t m_lastStatus;
    uint32_t m_lastConnectionAttempt;
    uint32_t m_lastDisconnectTime;
};

// Configuration access point (first setup, double reset), with the captive portal
class WifiApLink : public INetLink {
public:
    WifiApLink(const String& ssid, const String& password);

    NetLinkKind kind() const override { return NetLinkKind::ACCESS_POINT; }
    void begin() override;
    void end() override;
    void loop(uint32_t) override {}
    // The status LED shows the AP mode instead (computeSystemState)
    bool isConnected() const override { return false; }
    String statusText() const override { return "AP Mode Active"; }
    String localIP() const override;
    void macAddress(uint8_t mac[6]) const override;
    uint32_t stationCount() const;

private:
    String m_ssid;
    String m_password;
};

#endif // WIFI_LINKS_H
