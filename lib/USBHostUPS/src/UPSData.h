#ifndef UPS_DATA_H
#define UPS_DATA_H

#include <Arduino.h>
#include <vector>

struct UPSParameter {
    String key;
    String value;
};

struct UPSData {
private:
    std::vector<UPSParameter> _parameters;

public:
    UPSData() {
    }

    void set(const String& key, const String& value) {
        for (auto& param : _parameters) {
            if (param.key == key) {
                param.value = value;
                return;
            }
        }
        _parameters.push_back({key, value});
    }

    String get(const String& key, const String& defaultValue = "") const {
        for (const auto& param : _parameters) {
            if (param.key == key) {
                return param.value;
            }
        }
        return defaultValue;
    }

    bool hasKey(const String& key) const {
        for (const auto& param : _parameters) {
            if (param.key == key) {
                return true;
            }
        }
        return false;
    }

    void remove(const String& key) {
        for (auto it = _parameters.begin(); it != _parameters.end(); ++it) {
            if (it->key == key) {
                _parameters.erase(it);
                return;
            }
        }
    }

    float getFloat(const String& key, float defaultVal = 0.0f) const {
        if (!hasKey(key)) return defaultVal;
        return get(key).toFloat();
    }

    bool getBool(const String& key, bool defaultVal = false) const {
        if (!hasKey(key)) return defaultVal;
        String val = get(key);
        return (val == "1" || val == "true" || val == "yes" || val == "enabled");
    }

    const std::vector<UPSParameter>& getAll() const {
        return _parameters;
    }



    // --- LOGIC ---
    static String computeUPSStatusString(const UPSData& d) {
        String status = "";
        
        // Read from dictionary
        bool acPresent = d.getBool("ups.status.ac_present");
        bool discharging = d.getBool("ups.status.discharging");
        bool good = d.getBool("ups.status.good");
        bool charging = d.getBool("ups.status.charging");
        float batteryCharge = d.getFloat("battery.charge", -1);
        
        // OL comes from ACPresent (NUT online_info) and OB is "not online", as in usbhid-ups.
        // PresentStatus.Good says the UPS works, not that it is on mains: Eaton keeps it at 1
        // on battery (NUT mge-hid maps it to off_info), which gave "OL OB". It only stands in
        // for ACPresent on devices without it, and never while discharging.
        bool hasAc = d.hasKey("ups.status.ac_present");
        bool onBattery = d.hasKey("ups.status.discharging") && discharging;
        bool online = hasAc ? (acPresent && !onBattery)
                            : (d.hasKey("ups.status.good") && good && !onBattery);
        // Extended PresentStatus flags are internal bits in usbhid-ups, exactly
        // like lowbatt/dischrg/chrg: ups_status_set() derives standard NUT tokens
        // from them and never publishes the raw flags. We mirror that here.
        bool depleted = d.getBool("ups.status.depleted");
        bool timeLimitExpired = d.getBool("ups.status.remaining_time_limit_expired");
        bool noBattery = d.getBool("ups.status.no_battery");

        if (online) status += "OL ";
        if (onBattery || (hasAc && !acPresent)) status += "OB ";
        // DEPLETED suppresses DISCHRG, as upstream: DISCHRG && !DEPLETED.
        if (onBattery && !depleted) status += "DISCHRG "; // from the Discharging usage, as usbhid-ups
        // LB covers lowbatt, timelimitexp and shutdownimm, as upstream
        // (LOWBATT | TIMELIMITEXP | SHUTDOWNIMM). ShutdownImminent never gives FSD:
        // only the upsmon primary sets it, and upsmon shuts down on FSD even on
        // mains (issue #65).
        bool shutdownImminent = d.getBool("ups.status.shutdown_imminent");
        if (d.getBool("ups.status.battery_low") || timeLimitExpired || shutdownImminent) status += "LB ";

        // CHRG is qualified by FullyCharged when the device reports it: present
        // and false (notfullycharged) shows CHRG, present and true hides it.
        // Absent, fall back to battery.charge in (0, 100), as upstream. The flag
        // only qualifies an active charging state, it does not create one.
        if (d.hasKey("ups.status.charging") && charging) {
            if (d.hasKey("ups.status.fully_charged")) {
                if (!d.getBool("ups.status.fully_charged")) status += "CHRG ";
            } else if (batteryCharge > 0.0f && batteryCharge < 100.0f) {
                status += "CHRG ";
            }
        }

        // RB covers replacebatt and nobattery, as upstream (REPLACEBATT | NOBATTERY).
        if (d.getBool("ups.status.replace_battery") || noBattery) status += "RB ";
        if (d.getBool("ups.status.overload")) status += "OVER ";
        // CommunicationLost is an internal fault bit upstream (commfault ->
        // ups.alarm), not a status token: COMM_LOST is not NUT vocabulary (issue #65).
        // ups.status.shutdown_imminent and ups.status.comm_lost stay in the
        // dictionary for a future ups.alarm.

        if (status.length() == 0) status = "Unknown";
        status.trim();
        return status;
    }

    void updateRealPower() {
        if (!hasKey("ups.load")) return;
        uint8_t loadPct = (uint8_t)getFloat("ups.load");
        
        if (hasKey("ups.realpower.nominal") && getFloat("ups.realpower.nominal") > 0) {
            uint16_t nominal = (uint16_t)getFloat("ups.realpower.nominal");
            set("ups.realpower", String((uint16_t)(((uint32_t)nominal * loadPct) / 100)));
        } else if (hasKey("ups.power.nominal") && getFloat("ups.power.nominal") > 0) {
            uint16_t apparent = (uint16_t)getFloat("ups.power.nominal");
            set("ups.realpower", String((uint16_t)(((uint32_t)apparent * 60 * loadPct) / 10000)));
        }
    }
};

#endif // UPS_DATA_H
