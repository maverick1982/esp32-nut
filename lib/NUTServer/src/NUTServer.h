#ifndef NUT_SERVER_H
#define NUT_SERVER_H

#include <Arduino.h>
#include <vector>
#include "IUpsSource.h"
#include "NutTracking.h"

#ifndef PIO_UNIT_TESTING
#include <WiFi.h>
#endif

#define NUT_DEFAULT_PORT 3493
// Connessioni contemporanee (US-084). Il core precompilato ha CONFIG_LWIP_MAX_SOCKETS 16:
// restano occupati i listener NUT e web, la richiesta web in corso e in AP il DNS del
// captive portal, quindi 8 client lasciano 4-5 socket liberi
#define NUT_MAX_CLIENTS 8
#define NUT_TIMEOUT_MS 300000
// Intervallo minimo tra le righe di rifiuto e tra i riepiloghi degli slot nel System Log,
// che tiene 50 righe: i client che si riconnettono a ogni lettura lo riempirebbero (US-084)
#define NUT_LOG_INTERVAL_MS 60000
// Keepalive TCP sui socket accettati: un client sparito senza chiudere la connessione
// libera lo slot dopo circa 60 + 10 * 3 = 90 s invece dei 300 s di NUT_TIMEOUT_MS
#define NUT_KEEPALIVE_IDLE_S 60
#define NUT_KEEPALIVE_INTVL_S 10
#define NUT_KEEPALIVE_COUNT 3

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
    bool begin(const NUTServerConfig& config, IUpsSource* usb_ups, uint16_t port = NUT_DEFAULT_PORT);
    
    // Esegue la gestione non bloccante delle connessioni e dei comandi
    void loop();

    // Metodi di utilità (esposti per facilitare il testing)
    static std::vector<String> splitTokens(const String& input);
    // Valore escapato come pconf_encode() di upstream (#, \ e "), per VAR, RW ed ENUM
    static String encodeValue(const char* raw);
    // Nome dell'UPS (US-082). In upstream è una sezione di ups.conf e non è mai vuoto: con il
    // nome vuoto LIST UPS stampava "UPS  ..." e aionut inviava "LIST VAR \n"
    static String defaultUpsName();
    static String normalizeUpsName(const String& name);   // vuoto → defaultUpsName()
    static bool isValidUpsName(const String& name);       // non vuoto, byte 0x21-0x7E senza " \ # @
    void processCommand(Print& client, int slot, const String& cmdLine);

    // Motivo della chiusura di una sessione (US-084): solo il timeout va nel System Log
    enum class CloseReason { PeerClosed, Logout, Inactivity };

    // Occupa il primo slot libero per un client appena accettato e ne azzera lo stato.
    // -1 se gli slot sono esauriti: il rifiuto va nel System Log, al più una riga ogni
    // NUT_LOG_INTERVAL_MS. loop() ci aggancia il socket; i test la chiamano direttamente
    int openSession(const String& peerIp, uint16_t peerPort);
    void closeSession(int slot, CloseReason reason);

    // Helpers per testing
    // Chiude tutte le sessioni aperte e riparte con il log degli slot da adesso
    void resetSessions() {
        for (int i = 0; i < NUT_MAX_CLIENTS; i++) {
            if (_clientActive[i]) closeSession(i, CloseReason::PeerClosed);
        }
        _lastSummary = "";
        _lastSummaryMs = _now();
        _rejectLogged = false;
        _lastRejectLogMs = 0;
        _rejectsSinceLog = 0;
    }
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
    // IP dei client degli slot attivi, in ordine di slot, separati da ", "
    String slotSummary() const;
    // Riepilogo degli slot nel System Log, solo se cambia e al più ogni NUT_LOG_INTERVAL_MS
    void logClientsSummary(uint32_t now);
    void setVar(Print& client, int slot, const String& upsName, const String& varName, const String& value);
    void rejectSet(Print& client, int slot, const String& varName, const String& value, const char* err);
    // Esito di SET VAR o INSTCMD con TRACKING ON: voce nella tabella condivisa e OK TRACKING <id>
    void replyTracking(Print& client, NutTable::SetStatus st);
    const char* missingCredentials(int slot) const;
    static bool isVarPresent(const UPSData& data, const String& varNameLower);
    void log(const char* level, const char* fmt, ...) const;

    NUTServerConfig _config;
    IUpsSource* _usb_ups;
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
    // IP del client, per il riepilogo degli slot e il log del timeout (US-084)
    String _clientPeer[NUT_MAX_CLIENTS];
    // Ultimo riepilogo registrato e quando; rifiuti non ancora registrati (US-084)
    String _lastSummary;
    uint32_t _lastSummaryMs;
    bool _rejectLogged;
    uint32_t _lastRejectLogMs;
    uint32_t _rejectsSinceLog;
    NutTracking _tracking;
    ClockFn _now;
    LogFn _log_cb;
};

#endif // NUT_SERVER_H
