#ifndef NUT_SERVER_H
#define NUT_SERVER_H

#include <Arduino.h>
#include <vector>
#include "IUSBHostUPS.h"
#include "NutTracking.h"

#ifndef PIO_UNIT_TESTING
#include <WiFi.h>
#endif

#define NUT_DEFAULT_PORT 3493
#define NUT_MAX_CLIENTS 4
#define NUT_TIMEOUT_MS 300000

struct NUTServerConfig {
    String username;
    String password;
    String ups_name;
};

class NUTServer {
public:
    NUTServer();
    ~NUTServer();

    // Inizializza il server TCP e memorizza la configurazione e le dipendenze
    bool begin(const NUTServerConfig& config, IUSBHostUPS* usb_ups, uint16_t port = NUT_DEFAULT_PORT);
    
    // Esegue la gestione non bloccante delle connessioni e dei comandi
    void loop();

    // Metodi di utilità (esposti per facilitare il testing)
    static std::vector<String> splitTokens(const String& input);
    // Valore escapato come pconf_encode() di upstream (#, \ e "), per VAR, RW ed ENUM
    static String encodeValue(const char* raw);
    void processCommand(Print& client, int slot, const String& cmdLine);

    // Helpers per testing
    // true: USERNAME e PASSWORD giusti inviati; false: nessuna PASSWORD inviata
    void setAuthenticated(int slot, bool auth) {
        if (slot < 0 || slot >= NUT_MAX_CLIENTS) return;
        _clientAuthenticated[slot] = auth;
        _clientPasswordSet[slot] = auth;
        if (auth) _clientUsernameSet[slot] = true;
    }
    void setClientUsername(int slot, const String& user) {
        if (slot < 0 || slot >= NUT_MAX_CLIENTS) return;
        _clientUsername[slot] = user;
        _clientUsernameSet[slot] = user.length() > 0;
    }
    // Svuota la tabella degli esiti e spegne TRACKING su tutte le sessioni
    void clearTracking() {
        _tracking.clear();
        for (int i = 0; i < NUT_MAX_CLIENTS; i++) _clientTracking[i] = false;
    }

    // System Log: rifiuti di SET VAR (US-067). Gli esiti delle richieste valide li registra l'host
    typedef void (*LogFn)(const char* level, const char* msg);
    void setLogCallback(LogFn cb) { _log_cb = cb; }

    // Orologio e sorgente casuale di TRACKING, sostituibili nei test (default millis ed esp_random)
    typedef uint32_t (*ClockFn)();
    void setClock(ClockFn now);
    void setRandom(NutTracking::RandomFn rnd);

private:
    void handleCommand(int slot, const String& cmdLine);
    void closeSession(int slot);
    void setVar(Print& client, int slot, const String& upsName, const String& varName, const String& value);
    void rejectSet(Print& client, int slot, const String& varName, const String& value, const char* err);
    const char* missingCredentials(int slot) const;
    static bool isVarPresent(const UPSData& data, const String& varNameLower);
    void log(const char* level, const char* fmt, ...) const;

    NUTServerConfig _config;
    IUSBHostUPS* _usb_ups;
    uint16_t _port;
    bool _initialized;

#ifndef PIO_UNIT_TESTING
    WiFiServer _server;
    WiFiClient _clients[NUT_MAX_CLIENTS];
#endif
    bool _clientActive[NUT_MAX_CLIENTS];
    bool _clientAuthenticated[NUT_MAX_CLIENTS];
    uint32_t _clientLastActivity[NUT_MAX_CLIENTS];
    String _clientBuffer[NUT_MAX_CLIENTS];
    String _clientUsername[NUT_MAX_CLIENTS];
    // USERNAME / PASSWORD inviati, giusti o no: il controllo FLAG_USER di check_command() in upsd
    bool _clientUsernameSet[NUT_MAX_CLIENTS];
    bool _clientPasswordSet[NUT_MAX_CLIENTS];
    // SET TRACKING vale per la sessione; la tabella degli esiti è unica, come in upsd
    bool _clientTracking[NUT_MAX_CLIENTS];
    NutTracking _tracking;
    ClockFn _now;
    LogFn _log_cb;
};

#endif // NUT_SERVER_H
