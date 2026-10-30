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
    bool _realPowerMeasured = false;
    uint32_t _writes = 0;

public:
    UPSData() {
    }

    void set(const String& key, const String& value) {
        _writes++;
        for (auto& param : _parameters) {
            if (param.key == key) {
                param.value = value;
                return;
            }
        }
        _parameters.push_back({key, value});
    }

    // Writes done by set() and remove() since construction, for the tests (US-073)
    uint32_t writes() const { return _writes; }

    // The value of key without copying it, nullptr when missing (US-073)
    const String* find(const char* key) const {
        for (const auto& param : _parameters) {
            if (param.key == key) return &param.value;
        }
        return nullptr;
    }

    // True when key holds exactly value; no temporary String (US-073, ADR 0007)
    bool valueEquals(const char* key, const String& value) const {
        const String* v = find(key);
        return v && *v == value;
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
                _writes++;
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
    // getBool() without the String copy: the same true values (US-107)
    static bool flagSet(const UPSData& d, const char* key) {
        const String* v = d.find(key);
        return v && (*v == "1" || *v == "true" || *v == "yes" || *v == "enabled");
    }

    static String computeUPSStatusString(const UPSData& d) {
        String status = "";
        
        // Read from dictionary
        bool acPresent = d.getBool("ups.status.ac_present");
        bool discharging = d.getBool("ups.status.discharging");
        bool good = d.getBool("ups.status.good");
        bool charging = d.getBool("ups.status.charging");
        float batteryCharge = d.getFloat("battery.charge", -1);
        
        // OL/OB comes from ACPresent (NUT online_info), as in usbhid-ups ups_status_set():
        // Discharging only adds DISCHRG, so AC present while discharging is "OL DISCHRG",
        // the upstream default without onlinedischarge_onbattery (APC BGM2200, issue #98).
        // PresentStatus.Good says the UPS works, not that it is on mains: Eaton keeps it at 1
        // on battery (NUT mge-hid maps it to off_info), which gave "OL OB". It only stands in
        // for ACPresent on devices without it, and never while discharging. Without ACPresent
        // Discharging means OB, as upstream ("does not report a power state, but it is
        // discharging - assuming it is on battery").
        bool hasAc = d.hasKey("ups.status.ac_present");
        bool dischrg = d.hasKey("ups.status.discharging") && discharging;
        bool online = hasAc ? acPresent
                            : (d.hasKey("ups.status.good") && good && !dischrg);
        // Extended PresentStatus flags are internal bits in usbhid-ups, exactly
        // like lowbatt/dischrg/chrg: ups_status_set() derives standard NUT tokens
        // from them and never publishes the raw flags. We mirror that here.
        bool depleted = d.getBool("ups.status.depleted");
        bool timeLimitExpired = d.getBool("ups.status.remaining_time_limit_expired");
        bool noBattery = d.getBool("ups.status.no_battery");

        // Token order of ups_status_set() (usbhid-ups.c:2691-3010): CAL, OL/OB, DISCHRG, CHRG,
        // LB, OVER, RB, TRIM, BOOST, BYPASS, OFF (US-107). Calibration comes first upstream,
        // since it looks like OFF or OB on many devices (usbhid-ups.c:2703-2711).
        if (flagSet(d, "ups.status.cal")) status += "CAL ";
        // OL and OB exclude each other here: upstream writes OB first when a device reports
        // both, which this function never produces.
        if (online) status += "OL ";
        if (hasAc ? !acPresent : dischrg) status += "OB ";
        // DEPLETED suppresses DISCHRG, as upstream: DISCHRG && !DEPLETED.
        if (dischrg && !depleted) status += "DISCHRG "; // from the Discharging usage, as usbhid-ups

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

        // LB covers lowbatt, timelimitexp and shutdownimm, as upstream
        // (LOWBATT | TIMELIMITEXP | SHUTDOWNIMM). ShutdownImminent never gives FSD:
        // only the upsmon primary sets it, and upsmon shuts down on FSD even on
        // mains (issue #65).
        bool shutdownImminent = d.getBool("ups.status.shutdown_imminent");
        if (d.getBool("ups.status.battery_low") || timeLimitExpired || shutdownImminent) status += "LB ";

        if (d.getBool("ups.status.overload")) status += "OVER ";
        // RB covers replacebatt and nobattery, as upstream (REPLACEBATT | NOBATTERY).
        if (d.getBool("ups.status.replace_battery") || noBattery) status += "RB ";
        if (flagSet(d, "ups.status.trim")) status += "TRIM ";
        if (flagSet(d, "ups.status.boost")) status += "BOOST ";
        if (flagSet(d, "ups.status.bypassauto") || flagSet(d, "ups.status.bypassman")) status += "BYPASS ";
        // ups.status.ecomode / essmode give no token: upstream puts ECO and ESS in
        // experimental.ups.mode.buzzwords (buzzmode_set), out of US-107.
        if (flagSet(d, "ups.status.off")) status += "OFF ";
        // CommunicationLost is an internal fault bit upstream (commfault ->
        // ups.alarm), not a status token: COMM_LOST is not NUT vocabulary (issue #65).
        // Every table reads it as commfault (US-079: genericHid2Nut() too).

        status.trim();
        // ALARM leads when ups.alarm is published, as status_commit() with alarm_active
        // (dstate.c:2315-2321): "ALARM" alone when no other token is set
        if (d.find("ups.alarm")) return status.length() == 0 ? String("ALARM") : String("ALARM ") + status;
        if (status.length() == 0) status = "Unknown";
        return status;
    }

    /**
     * ups.alarm as usbhid-ups builds it (US-107): the texts of the vendor ups.alarm rows,
     * applied during hid_ups_walk() (usbhid-ups.c:3194), then those of ups_alarm_set()
     * (usbhid-ups.c:2606-2644) in its order, separated by a space as alarm_set()
     * (dstate.c:2370). Empty when no alarm is active. CommunicationLost
     * is ups.status.commfault in every table (US-079).
     */
    static void alarmText(const UPSData& d, const String& vendor, String& out) {
        static const struct { const char* key; const char* text; } bits[] = {
            { "ups.status.replace_battery", "Replace battery!" },
            { "ups.status.shutdown_imminent", "Shutdown imminent!" },
            { "ups.status.fanfail", "Fan failure!" },
            { "ups.status.no_battery", "No battery installed!" },
            { "ups.status.battvoltlo", "Battery voltage too low!" },
            { "ups.status.battvolthi", "Battery voltage too high!" },
            { "ups.status.chargerfail", "Battery charger fail!" },
            { "ups.status.overheat", "Temperature too high!" },
            { "ups.status.commfault", "Internal UPS fault!" },
            { "ups.status.awaitingpower", "Awaiting power!" },
            { "ups.status.bypassauto", "Automatic bypass mode!" },
            { "ups.status.bypassman", "Manual bypass mode!" },
        };
        out = vendor;
        for (const auto& b : bits) {
            if (!flagSet(d, b.key)) continue;
            if (out.length() > 0) out += ' ';
            out += b.text;
        }
    }

    // input.transfer.reason of ups_status_set() (usbhid-ups.c:2695-2701), nullptr when the
    // variable must not exist (US-107)
    static const char* transferReason(const UPSData& d) {
        if (flagSet(d, "ups.status.vrange")) return "input voltage out of range";
        if (flagSet(d, "ups.status.frange")) return "input frequency out of range";
        return nullptr;
    }

    // Publishes or removes ups.alarm and input.transfer.reason, writing only what changed
    // (alarm_commit(), US-107): no String is built and nothing is written while no alarm is
    // active and nothing changes (ADR 0007)
    void applyAlarmVariables(const String& vendor) {
        String text;
        alarmText(*this, vendor, text);
        if (text.length() == 0) {
            if (find("ups.alarm")) remove("ups.alarm");
        } else if (!valueEquals("ups.alarm", text)) {
            set("ups.alarm", text);
        }
        const char* reason = transferReason(*this);
        const String* current = find("input.transfer.reason");
        if (!reason) {
            if (current) remove("input.transfer.reason");
        } else if (!current || !(*current == reason)) {
            set("input.transfer.reason", reason);
        }
    }

    // The device measures ups.realpower (ActivePower): updateRealPower() never estimates
    // it, so the estimate does not overwrite the measure between two reports
    void markRealPowerMeasured() { _realPowerMeasured = true; }
    bool realPowerMeasured() const { return _realPowerMeasured; }

    // ups.realpower from the load and the nominal power, only for devices that do not
    // measure it (not an upstream value)
    void updateRealPower() {
        if (_realPowerMeasured) return;
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
