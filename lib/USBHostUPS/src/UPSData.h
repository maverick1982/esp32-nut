#ifndef UPS_DATA_H
#define UPS_DATA_H

#include <Arduino.h>
#include <cmath>
#include <vector>

struct UPSParameter {
    String key;
    String value;
};

// Where the ups.realpower estimate comes from (US-146): the custom variables of the NUT
// Server tab or the nominal powers of the UPS, in W or in VA (x 0.6)
enum class RealPowerSource : uint8_t { NONE, CUSTOM_W, UPS_W, CUSTOM_VA, UPS_VA };

struct UPSData {
private:
    std::vector<UPSParameter> _parameters;
    bool _realPowerMeasured = false;
    bool _realPowerEstimated = false; // ups.realpower written by updateRealPower() (US-146)
    float _customRealNominal = 0.0f;     // custom ups.realpower.nominal, W (US-146)
    float _customApparentNominal = 0.0f; // custom ups.power.nominal, VA (US-146)
    RealPowerSource _realPowerSource = RealPowerSource::NONE; // of the last estimate (US-146)
    float _realPowerSourceNominal = 0.0f;
    uint32_t _writes = 0;

    void setRealPowerSource(RealPowerSource source, float nominal) {
        _realPowerSource = source;
        _realPowerSourceNominal = nominal;
    }

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
        // ups.status.bypass is the BYPASS bit of nutdrv_qx (US-133): the same token, without
        // the "bypass mode" alarms of bypassauto/bypassman, which ups_alarm_set() of
        // nutdrv_qx does not have (nutdrv_qx.c:4786-4794)
        if (flagSet(d, "ups.status.bypassauto") || flagSet(d, "ups.status.bypassman") ||
            flagSet(d, "ups.status.bypass")) status += "BYPASS ";
        // ups.status.ecomode / essmode give no token: upstream puts ECO and ESS in
        // experimental.ups.mode.buzzwords (buzzmode_set), out of US-107.
        if (flagSet(d, "ups.status.off")) status += "OFF ";
        // FSD bit of nutdrv_qx (US-133): ups_status_set() writes the FSD token
        // (nutdrv_qx.c:4834-4836). No HID table writes ups.status.fsd: for the HID drivers
        // ShutdownImminent stays LB, never FSD (issue #65). Upstream nutdrv_qx writes CAL
        // after OFF; here CAL stays first, in the usbhid-ups order (same tokens).
        if (flagSet(d, "ups.status.fsd")) status += "FSD ";
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
            // FSD of nutdrv_qx: ups_alarm_set() after RB (nutdrv_qx.c:4786-4794, US-133)
            { "ups.status.fsd", "Shutdown imminent!" },
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

    // The device measures or computes ups.realpower (ActivePower, eaton_compute_realpower_info):
    // updateRealPower() never estimates it, so the estimate does not overwrite the value of the
    // device. An estimate already written is removed: from now on the key belongs to the device
    void markRealPowerMeasured() {
        if (_realPowerEstimated && find("ups.realpower")) remove("ups.realpower");
        _realPowerEstimated = false;
        _realPowerMeasured = true;
        setRealPowerSource(RealPowerSource::NONE, 0.0f);
    }
    bool realPowerMeasured() const { return _realPowerMeasured; }

    // ups.realpower currently holds the estimate of updateRealPower()
    bool realPowerEstimated() const { return _realPowerEstimated; }

    // Source and nominal power of the last estimate, NONE and 0 without one (US-146 rework:
    // GET /api/ups-vars tells the page where the estimate comes from)
    RealPowerSource realPowerSource() const { return _realPowerSource; }
    float realPowerSourceNominal() const { return _realPowerSourceNominal; }

    // Nominal powers the user set as custom variables of the NUT Server tab (US-146, #128):
    // ups.realpower.nominal in W, ups.power.nominal in VA. Kept out of the keys: NUT clients
    // and the telemetry see them through the custom variable. Not finite or <= 0 means none.
    // True when one of them changed; the caller runs updateRealPower()
    bool setCustomPowerNominals(float real_w, float apparent_va) {
        if (!std::isfinite(real_w) || real_w <= 0.0f) real_w = 0.0f;
        if (!std::isfinite(apparent_va) || apparent_va <= 0.0f) apparent_va = 0.0f;
        bool changed = real_w != _customRealNominal || apparent_va != _customApparentNominal;
        _customRealNominal = real_w;
        _customApparentNominal = apparent_va;
        return changed;
    }

    // ups.realpower from the load and a nominal power, only for devices that neither measure
    // nor compute it (not an upstream value; US-075, US-146). Sources in order: the custom
    // ups.realpower.nominal, the one of the UPS, then the custom ups.power.nominal x 0.6 and
    // the one of the UPS x 0.6. The decimal load is kept and the result rounded to the watt
    // (10.4 % of 980 W = 102 W). Without a source an estimate already written is removed;
    // nothing is written while the value does not change (ADR 0007)
    void updateRealPower() {
        if (_realPowerMeasured) return;
        double watts = -1.0;
        const String* load_text = find("ups.load");
        double load = load_text ? (double)load_text->toFloat() : -1.0;
        if (load >= 0.0) {
            double ups_real = getFloat("ups.realpower.nominal");
            double ups_apparent = getFloat("ups.power.nominal");
            if (_customRealNominal > 0.0f) {
                watts = load * _customRealNominal / 100.0;
                setRealPowerSource(RealPowerSource::CUSTOM_W, _customRealNominal);
            } else if (ups_real > 0.0) {
                watts = load * ups_real / 100.0;
                setRealPowerSource(RealPowerSource::UPS_W, (float)ups_real);
            } else if (_customApparentNominal > 0.0f) {
                watts = load * _customApparentNominal * 0.6 / 100.0;
                setRealPowerSource(RealPowerSource::CUSTOM_VA, _customApparentNominal);
            } else if (ups_apparent > 0.0) {
                watts = load * ups_apparent * 0.6 / 100.0;
                setRealPowerSource(RealPowerSource::UPS_VA, (float)ups_apparent);
            }
        }
        if (watts < 0.0) {
            if (_realPowerEstimated && find("ups.realpower")) remove("ups.realpower");
            _realPowerEstimated = false;
            setRealPowerSource(RealPowerSource::NONE, 0.0f);
            return;
        }
        String value((long)std::lround(watts));
        if (!valueEquals("ups.realpower", value)) set("ups.realpower", value);
        _realPowerEstimated = true;
    }
};

#endif // UPS_DATA_H
