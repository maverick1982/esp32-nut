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
        _parameters.reserve(60); // Pre-allocate to prevent heap fragmentation
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
        
        if (d.hasKey("ups.status.ac_present") && acPresent && (!d.hasKey("ups.status.discharging") || !discharging)) status += "OL ";
        else if (d.hasKey("ups.status.good") && good) status += "OL ";
        
        if (d.hasKey("ups.status.discharging") && discharging) status += "OB ";
        if (d.getBool("ups.status.battery_low")) status += "LB ";
        
        if (d.hasKey("ups.status.charging") && charging && !(batteryCharge == 100.0f && d.hasKey("ups.status.ac_present") && acPresent)) status += "CHRG ";
        
        if (d.getBool("ups.status.replace_battery")) status += "RB ";
        if (d.getBool("ups.status.overload")) status += "OVER ";
        if (d.getBool("ups.status.shutdown_imminent")) status += "FSD ";
        if (d.getBool("ups.status.comm_lost")) status += "COMM_LOST ";
        
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
