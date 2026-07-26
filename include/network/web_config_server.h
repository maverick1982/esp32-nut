#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <WebServer.h>
#include <DNSServer.h>
#include "core/config_manager.h"
#include "USBHostUPS.h"

class WebConfigServer {
public:
    WebConfigServer(ConfigManager& config_mgr);
    void begin(bool isAPMode);
    void loop();
    void setUPS(USBHostUPS* ups);

private:
    WebServer server;
    DNSServer dnsServer;
    ConfigManager& config_mgr;
    bool is_ap_mode;

    void handleConnect();
    void handleLogs();
    void handleNutConfig();
    void handleGetConfig();
    void handleUpsVars();
    void handleSystemStatus();
    void handleBeeper();
    
    void handleOTAPage();
    void handleOTAUpload();

    bool should_restart = false;
    unsigned long restart_request_time = 0;
    USBHostUPS* usb_ups = nullptr;
};

#endif // WEB_CONFIG_SERVER_H
