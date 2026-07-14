#ifndef WEB_CONFIG_SERVER_H
#define WEB_CONFIG_SERVER_H

#include <WebServer.h>
#include <DNSServer.h>
#include "config_manager.h"

class WebConfigServer {
public:
    WebConfigServer(ConfigManager& config_mgr);
    void begin(bool isAPMode);
    void loop();

private:
    WebServer server;
    DNSServer dnsServer;
    ConfigManager& config_mgr;
    bool is_ap_mode;

    void handleScan();
    void handleConnect();
    void handleLogs();
    void handleNutConfig();

    bool should_restart = false;
    unsigned long restart_time = 0;
};

#endif // WEB_CONFIG_SERVER_H
