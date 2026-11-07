#ifndef WEB_API_JSON_H
#define WEB_API_JSON_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "IUpsSource.h"
#include "board/board_settings.h"
#include "board/eth_spi_settings.h"
#include "core/auto_shutdown.h"
#include "NetLinkInfo.h"
#include "NutOverrides.h"
#include "SyslogSettings.h"
#include "SyslogStatus.h"
#include "TimeSettings.h"
#include "TimeStatus.h"
#include "DiagReadStatus.h"

// State of the network time for GET /api/time (US-117), already formatted by the handler
// so that the native tests do not need a clock. local, abbr, utc and since_sync_s only
// when has_time
struct TimeStatusView {
    TimeState state;
    bool has_time;
    String local;  // "2026-11-05 09:12:03", time zone of the settings
    String abbr;   // "CET"
    String utc;    // "2026-11-05T08:12:03Z"
    uint32_t since_sync_s;
};

// The bound Q* driver for GET /api/board (US-138): detected is the claimed protocol
// ("Megatec 0.10"), "" before a claim
struct BoardQxInfo {
    const char* detected;
};

class WebApiJson {
public:
    // Lunghezza massima del body di POST /api/ups/command, /api/ups/variable e
    // /api/ups/variable/reset
    static constexpr size_t MAX_COMMAND_BODY_LENGTH = 256;

    static String generateUpsVars(IUpsSource* usb_ups);
    // Catalogo dei comandi supportati dall'UPS (US-059, US-062):
    // {"connected", "commands": [{name, description, destructive}]}
    // Nessun blocco in Access Point (US-068): la conferma dei distruttivi la chiede la pagina
    static String generateUpsCommands(IUpsSource* usb_ups);
    // Esegue un comando da body {"name": "..."}, distruttivi compresi, in STA come in AP
    // (US-068). Restituisce lo status HTTP e scrive il JSON in response
    static int runUpsCommand(IUpsSource* usb_ups, const String& body, String& response);

    // --- Pagina "UPS Settings" (US-068) ---

    // True per le variabili che la pagina fa confermare prima di applicarle: ups.shutdown,
    // outlet.switchable e outlet.<N>.switchable, input.bypass.*, input.eco.switchable e
    // input.transfer.bypass.forced
    static bool needsConfirmation(const char* name);
    // Catalogo delle variabili scrivibili, nell'ordine di getWritableVariables():
    // {"connected", "variables": [{name, description, value, max_length, enum: [...],
    // driver_side, confirm}]}. Senza host o con UPS scollegato: connected false e lista vuota.
    // US-115: "override": <valore> per le variabili sovrascritte nella pagina NUT Server
    // (assente per le altre), che la pagina mostra bloccate
    static String generateUpsVariables(IUpsSource* usb_ups, const std::vector<NutOverride>* overrides = nullptr);
    // Scrive una variabile da body {"name": "...", "value": "..."} con la logica di SET VAR
    // (NutTable::checkSetValue, poi setVariable() col nome canonico). Il valore ammette
    // solo byte 0x20-0x7E, come il lettore di riga del server NUT. 200 {"success":true,
    // "value": <valore in UPSData dopo la scrittura, null se l'UPS si è scollegato>},
    // altrimenti 400/500/503 con {"error"}.
    // Restituisce lo status HTTP e scrive il JSON in response. Se host_called non è nullptr
    // vi scrive true quando la richiesta è arrivata a setVariable() (l'esito lo registra
    // già l'host), false per i rifiuti precedenti (400 di forma o di validazione, 503)
    // US-115: una variabile con override (overrides) è di sola lettura, come per SET VAR:
    // 400 {"error": OVERRIDDEN_VARIABLE_ERROR} dopo il controllo dell'host
    static int runUpsVariable(IUpsSource* usb_ups, const String& body, String& response,
                              bool* host_called = nullptr, const std::vector<NutOverride>* overrides = nullptr);
    // Riporta al default del driver una variabile lato driver da body {"name": "..."}:
    // 200 {"success":true, "value": <default, null come sopra>}, altrimenti 400/500/503 con
    // {"error"}. host_called
    // come in runUpsVariable, riferito a resetVariable(); overrides come in runUpsVariable
    static int resetUpsVariable(IUpsSource* usb_ups, const String& body, String& response,
                                bool* host_called = nullptr, const std::vector<NutOverride>* overrides = nullptr);
    static constexpr const char* OVERRIDDEN_VARIABLE_ERROR = "Variable overridden in the NUT Server tab";

    // --- Variabili personalizzate della pagina NUT Server (US-115) ---

    static constexpr size_t MAX_OVERRIDES_BODY_LENGTH = 6144;
    // GET /api/nut/overrides e risposta di POST: {"max", "max_value_length",
    // "max_description_length", "overrides": [{key, value, description, ups_value}]}.
    // ups_value è il valore reale dell'UPS per la chiave (per device.mfr/model/serial quello
    // di ups.*), null se l'UPS non la fornisce o non è connesso
    static String nutOverridesJson(const std::vector<NutOverride>& list, IUpsSource* usb_ups);
    // POST /api/nut/overrides: {"overrides": [{key, value, description?}]} sostituisce la
    // lista. 0 con next compilato; body oltre MAX_OVERRIDES_BODY_LENGTH, JSON non valido,
    // overrides non array o voce senza key/value stringa (description, se c'è, stringa) →
    // 400 {"error":"Invalid request"}; regola di NutOverrides::checkList violata → 400
    // {"error": NutOverrides::errorMessage, "index": <voce>}
    static int checkNutOverridesBody(const String& body, std::vector<NutOverride>& next, String& response);

    // Nome dell'UPS della configurazione salvata (US-082): la stringa se non vuota,
    // altrimenti NUTServer::defaultUpsName() (chiave assente, null, non stringa o vuota).
    // *defaulted dice se si è usato il default
    static String upsNameFromConfig(JsonVariantConst value, bool* defaulted = nullptr);
    // Controllo di ups_name nel body di POST /api/nut/config (US-082): assente o null resta
    // il valore attuale; non stringa o non valido per NUTServer::isValidUpsName() → 400
    // {"error":"Invalid UPS name"}. Restituisce 0 se il body va bene
    static int checkNutConfigBody(JsonVariantConst body, String& response);

    // GET /api/board (US-094): {"id","label","led":{present,on,level,levels},
    // "usb":{"ports":[{id,label}],"port"},"auto_shutdown":{on,available,reason}}; "led"
    // absent on a board without LED. US-114: reason "no_ups" / "no_command", absent when
    // available. US-138: "qx":{"protocol","novendor","protocols":[names in probing order],
    // "detected"} only with qx (a Q* driver bound); protocol "" = automatic
    static String boardSettingsJson(const BoardConfig& board, const BoardSettings& settings,
                                    AutoShutdownAvailability auto_shutdown, const BoardQxInfo* qx = nullptr);
    // POST /api/board: optional led_on (bool), led_level (integer 1-5), usb_port (port id),
    // auto_shutdown (bool, US-114: false always, true only when available), qx_protocol
    // ("" or a protocol name, case-insensitive, saved lower case) and qx_novendor (bool)
    // (US-138, accepted also without a Q* UPS, never a restart).
    // 0 with next filled and restart true when the USB port changes; otherwise 400
    // {"error"}: "Invalid request", "Invalid LED level", "Invalid USB port",
    // "No status LED on this board", "UPS not connected", "The UPS offers no shutdown command",
    // "Invalid protocol"
    static int checkBoardSettingsBody(JsonVariantConst body, const BoardConfig& board,
                                      const BoardSettings& current, AutoShutdownAvailability auto_shutdown,
                                      BoardSettings& next, bool& restart, String& response);

    // "network" of GET /api/system-status (US-112): type, state ("connected", "connecting",
    // "disconnected", "ap"), ip, gateway, mac ("AA:BB:CC:DD:EE:FF"), dhcp; Wi-Fi also ssid and
    // rssi, Ethernet also link_speed and full_duplex. Fields the link does not have now are
    // left out (no gateway in AP mode, no rssi while connecting, ...)
    static void networkStatusJson(JsonObject net, const NetLinkInfo& info);
    // "ups" of GET /api/system-status: status = ups.model ("Unknown Model" without it) and
    // stale (attached UPS with stale data, not while waiting for the first poll), or
    // "Disconnected". US-132: a recognised but unsupported UPS gives status
    // "Not supported yet" and unsupported = IUpsSource::unsupportedDevice()
    static void upsStatusJson(JsonObject ups, IUpsSource* src);
    // "network.links" of GET /api/config (US-112): the links of the board profile, "wifi"
    // then "ethernet"
    static void networkLinksJson(JsonArray links, const BoardConfig& board);
    // US-126: as above, with "ethernet" after "wifi" also when the user enabled a W5500
    static void networkLinksJson(JsonArray links, const BoardConfig& board, bool spi_eth_enabled);

    // GET /api/network/eth (US-126): {"available":true,"enabled":bool,"pins":{"sck","mosi",
    // "miso","cs","irq","rst"} (null when not set),"strapping":[strapping GPIOs of the board],
    // "status":{"state":
    // "off"|"active"|"fallback","reason":"no_chip"|"no_link"|"no_ip"}}; reason absent when "".
    // US-127: "active" can carry no_link / no_ip too, when the board is on Ethernet without
    // a connection (Ethernet mode without a cable, or before the switch to Wi-Fi)
    static String ethSpiJson(const BoardConfig& board, const EthSpiSettings& settings, const char* state,
                             const char* reason);
    // POST /api/network/eth (US-126): {"enabled": bool, "pins": {"sck": n|null, ...}}, a pin
    // missing or null is not set. 0 with next filled, restart = next != current and
    // response = {"success":true,"restart":bool,"warnings":["GPIO 3 is a strapping pin..."]};
    // otherwise 400 {"error"}: "Invalid request" (enabled missing or not bool, pins not an
    // object, a pin neither integer nor null) or the message of checkEthSpiSettings()
    static int checkEthSpiBody(JsonVariantConst body, const BoardConfig& board, const EthSpiSettings& current,
                               EthSpiSettings& next, bool& restart, String& response);

    // US-127: as ethSpiJson() above, with "mode":"auto"|"ethernet"|"wifi" after "enabled"
    // (the link to use, PDR 0002 D3) and two more states: "unused" (Wi-Fi mode) and
    // "standby" (the board is on Wi-Fi in AUTO mode, the W5500 waits for the cable)
    static String ethSpiJson(const BoardConfig& board, const EthSpiSettings& settings, NetworkMode mode,
                             const char* state, const char* reason);
    // US-127: as checkEthSpiBody() above, with an optional "mode": "auto", "ethernet" or
    // "wifi". Missing keeps current_mode; another text or type (null too) gives 400
    // INVALID_NETWORK_MODE_ERROR, checked after "enabled" and the pins. restart = the pins
    // or the mode changed. The US-126 overload ignores a "mode" key
    static int checkEthSpiBody(JsonVariantConst body, const BoardConfig& board, const EthSpiSettings& current,
                               NetworkMode current_mode, EthSpiSettings& next, NetworkMode& next_mode,
                               bool& restart, String& response);
    static constexpr const char* INVALID_NETWORK_MODE_ERROR = "Invalid network mode";

    // Error of POST /api/network/hostname for a name outside NetHostname::isValid (US-113)
    static constexpr const char* INVALID_HOSTNAME_ERROR =
        "Invalid hostname: use only letters, digits and hyphens, no hyphen at the start or end, at "
        "most 32 characters";
    // POST /api/network/hostname (US-113): {"hostname": "<name>"}, "" back to the default.
    // 0 with next filled; "hostname" missing or not a string → 400 {"error":"Invalid request"};
    // a name outside NetHostname::isValid → 400 {"error": INVALID_HOSTNAME_ERROR}
    static int checkHostnameBody(JsonVariantConst body, String& next, String& response);
    // "network.hostname" of GET /api/config (US-113): {"name": in use, "custom": saved name
    // ("" for the default), "default": default name of the link}
    static void hostnameJson(JsonObject out, const String& in_use, const String& saved, const String& def);

    // GET /api/logging (US-116): {"syslog":{enabled,host,port,protocol,facility,
    // min_level,rate},"status":{"syslog":{state,target,error,sent,failed,rate_limited,
    // not_ready}},"options":{protocols,facilities,levels,rate_min,rate_max}}
    static String remoteLogJson(const SyslogSettings& syslog, const SyslogStatus& status);
    // POST /api/logging: {"syslog":{...}} with any of the fields above; the others keep
    // their value. 0 with next filled, otherwise 400 {"error"}: "Invalid request",
    // "Invalid server", "Invalid port", "Invalid protocol", "Invalid facility",
    // "Invalid level", "Invalid rate", "Server required to enable syslog"
    static int checkRemoteLogBody(JsonVariantConst body, const SyslogSettings& current, SyslogSettings& next,
                                  String& response);

    // POST e GET /api/usb/dump/read (US-119): stato della lettura su richiesta dell'export,
    // {"state":"idle|reading|done|interrupted","total","done","read","empty","failed"}
    static String diagReadJson(const DiagReadStatus& status);

    // GET /api/time (US-117): {"time":{ntp,server,zone},"status":{state[,local,abbr,utc,
    // since_sync_s]},"options":{"zones":[...]}}
    static String timeJson(const TimeSettings& time, const TimeStatusView& status);
    // POST /api/time: {"time":{...}} with any of the fields above; the others keep their
    // value. 0 with next filled, otherwise 400 {"error"}: "Invalid request", "Invalid
    // server", "Invalid time zone", "Server required to enable NTP"
    static int checkTimeBody(JsonVariantConst body, const TimeSettings& current, TimeSettings& next,
                             String& response);
};

#endif
