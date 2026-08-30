#ifndef UPS_DATA_H
#define UPS_DATA_H

#include <Arduino.h>

struct UPSData {
    bool acPresent = false;
    bool belowRemainingCapacityLimit = false;
    bool charging = false;
    bool communicationLost = false;
    bool discharging = false;
    bool good = false;
    bool internalFailure = false;
    bool needReplacement = false;
    bool overload = false;
    bool shutdownImminent = false;

    bool outlet1Switch = false;
    bool outlet2Switch = false;

    uint8_t remainingCapacity = 0;
    uint32_t runTimeToEmpty = 0;

    uint8_t remainingCapacityLimit = 0;
    uint8_t designCapacity = 0;
    uint8_t fullChargeCapacity = 0;

    uint16_t configApparentPower = 0;
    uint16_t configActivePower = 0;
    uint8_t configFrequency = 0;
    uint16_t configVoltage = 0;

    float outputVoltage = 0.0f;
    float inputVoltage = 0.0f;
    float batteryVoltage = 0.0f;
    float batteryTemperature = 0.0f;

    uint16_t highVoltageTransfer = 0;
    uint16_t lowVoltageTransfer = 0;

    String manufacturer = "";
    String product = "";
    String serialNumber = "";
    String upsMfrDate = "";
    String batteryMfrDate = "";
    String batteryDate = "";

    uint8_t load = 0;
    uint16_t realPower = 0;
    bool beeperEnabled = true;
    int32_t delayShutdown = 0;
    int32_t delayStart = 0;
    int32_t timerStart = 0;
    int32_t timerShutdown = 0;

    String batteryType = "";
    String upsType = "";
    uint16_t outputVoltageNominal = 0;
    uint16_t outputFrequencyNominal = 0;

    struct {
        bool acPresent : 1;
        bool belowRemainingCapacityLimit : 1;
        bool charging : 1;
        bool communicationLost : 1;
        bool discharging : 1;
        bool good : 1;
        bool internalFailure : 1;
        bool needReplacement : 1;
        bool overload : 1;
        bool shutdownImminent : 1;
        bool outlet1Switch : 1;
        bool outlet2Switch : 1;
        bool remainingCapacity : 1;
        bool runTimeToEmpty : 1;
        bool remainingCapacityLimit : 1;
        bool designCapacity : 1;
        bool fullChargeCapacity : 1;
        bool configApparentPower : 1;
        bool configActivePower : 1;
        bool configFrequency : 1;
        bool configVoltage : 1;
        bool outputVoltage : 1;
        bool inputVoltage : 1;
        bool batteryVoltage : 1;
        bool batteryTemperature : 1;
        bool highVoltageTransfer : 1;
        bool lowVoltageTransfer : 1;
        bool load : 1;
        bool realPower : 1;
        bool beeperEnabled : 1;
        bool delayShutdown : 1;
        bool delayStart : 1;
        bool timerStart : 1;
        bool timerShutdown : 1;
        bool batteryType : 1;
        bool upsType : 1;
        bool manufacturer : 1;
        bool product : 1;
        bool serialNumber : 1;
        bool outputVoltageNominal : 1;
        bool outputFrequencyNominal : 1;
        bool upsMfrDate : 1;
        bool batteryMfrDate : 1;
        bool batteryDate : 1;
    } has = {0};

    static String computeUPSStatusString(const UPSData& d) {
        String status = "";
        if (d.has.acPresent && d.acPresent && (!d.has.discharging || !d.discharging)) status += "OL ";
        else if (d.has.good && d.good) status += "OL ";
        if (d.has.discharging && d.discharging) status += "OB ";
        if (d.has.belowRemainingCapacityLimit && d.belowRemainingCapacityLimit) status += "LB ";
        if (d.has.charging && d.charging && !(d.remainingCapacity == 100 && d.has.acPresent && d.acPresent)) status += "CHRG ";
        if (d.has.needReplacement && d.needReplacement) status += "RB ";
        if (d.has.overload && d.overload) status += "OVER ";
        if (d.has.shutdownImminent && d.shutdownImminent) status += "FSD ";
        if (d.has.communicationLost && d.communicationLost) status += "COMM_LOST ";
        
        if (status.length() == 0) status = "Unknown";
        status.trim();
        return status;
    }

    void updateRealPower() {
        if (!has.load) return;
        if (has.configActivePower && configActivePower > 0) {
            has.realPower = true;
            realPower = (uint16_t)(((uint32_t)configActivePower * load) / 100);
        } else if (has.configApparentPower && configApparentPower > 0) {
            has.realPower = true;
            realPower = (uint16_t)(((uint32_t)configApparentPower * 60 * load) / 10000);
        }
    }
};

#endif // UPS_DATA_H
