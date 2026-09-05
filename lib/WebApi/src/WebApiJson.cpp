#include "WebApiJson.h"

String WebApiJson::generateUpsVars(IUSBHostUPS* usb_ups) {
    if (!usb_ups) {
        return "{\"error\": \"UPS non inizializzato\"}";
    }
    
    JsonDocument doc;
    
    if (!usb_ups->isConnected()) {
        doc["_disconnected"] = true;
        doc["ups.status"] = "Disconnected";
        std::string out_std;
        serializeJson(doc, out_std);
        return String(out_std.c_str());
    }
    
    auto data = usb_ups->getUPSData();
    
    doc["ups.status"] = usb_ups->getUPSStatusString();
    if (data->has.manufacturer) doc["ups.mfr"] = data->manufacturer;
    if (data->has.product) doc["ups.model"] = data->product;
    if (data->has.serialNumber) doc["ups.serial"] = data->serialNumber;
    
    if (data->has.remainingCapacity) doc["battery.charge"] = data->remainingCapacity;
    if (data->has.remainingCapacityLimit) doc["battery.charge.low"] = data->remainingCapacityLimit;
    if (data->has.designCapacity) doc["battery.capacity"] = data->designCapacity;
    if (data->has.fullChargeCapacity) doc["battery.capacity.full"] = data->fullChargeCapacity;
    if (data->has.runTimeToEmpty) doc["battery.runtime"] = data->runTimeToEmpty;
    
    if (data->has.outputVoltage) doc["output.voltage"] = data->outputVoltage;
    if (data->has.outputCurrent) doc["output.current"] = data->outputCurrent;
    if (data->has.inputVoltage) doc["input.voltage"] = data->inputVoltage;
    if (data->has.inputCurrent) doc["input.current"] = data->inputCurrent;
    if (data->has.batteryVoltage) doc["battery.voltage"] = data->batteryVoltage;
    if (data->has.batteryCurrent) doc["battery.current"] = data->batteryCurrent;
    
    if (data->has.highVoltageTransfer) doc["input.transfer.high"] = data->highVoltageTransfer;
    if (data->has.lowVoltageTransfer) doc["input.transfer.low"] = data->lowVoltageTransfer;
    
    if (data->has.configApparentPower) doc["ups.power.nominal"] = data->configApparentPower;
    if (data->has.configActivePower) doc["ups.realpower.nominal"] = data->configActivePower;
    if (data->has.configFrequency) doc["input.frequency.nominal"] = data->configFrequency;
    if (data->has.configVoltage) doc["input.voltage.nominal"] = data->configVoltage;
    
    if (data->has.outputVoltageNominal) doc["output.voltage.nominal"] = data->outputVoltageNominal;
    if (data->has.outputFrequencyNominal) doc["output.frequency.nominal"] = data->outputFrequencyNominal;
    
    if (data->has.load) doc["ups.load"] = data->load;
    if (data->has.realPower) doc["ups.realpower"] = data->realPower;
    if (data->has.batteryTemperature) doc["battery.temperature"] = serialized(String(data->batteryTemperature, 1));
    if (data->has.delayShutdown) doc["ups.delay.shutdown"] = data->delayShutdown;
    if (data->has.delayStart) doc["ups.delay.start"] = data->delayStart;
    if (data->has.timerStart) doc["ups.timer.start"] = data->timerStart;
    if (data->has.timerShutdown) doc["ups.timer.shutdown"] = data->timerShutdown;
    if (data->has.batteryType) doc["battery.type"] = data->batteryType;
    if (data->has.upsMfrDate) doc["ups.mfr.date"] = data->upsMfrDate;
    if (data->has.batteryMfrDate) doc["battery.mfr.date"] = data->batteryMfrDate;
    if (data->has.batteryDate) doc["battery.date"] = data->batteryDate;
    if (data->has.upsType) doc["ups.type"] = data->upsType;
    if (data->has.beeperEnabled) {
        doc["ups.beeper.status"] = data->beeperEnabled ? "enabled" : "disabled";
    }
    doc["ups.beeper.switchable"] = usb_ups->supportsBeeperToggle();

    std::string out_std;
    serializeJson(doc, out_std);
    return String(out_std.c_str());
}
