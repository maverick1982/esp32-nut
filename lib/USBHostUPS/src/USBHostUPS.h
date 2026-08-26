#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "Quirks.h"
#include "HIDParser.h"

typedef void (*LogCallback)(const char* level, const char* msg);

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
    String batteryMfrDate = "";

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
        bool batteryMfrDate : 1;
    } has = {0};
};

class IUPSDriver;

class USBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    bool begin();
    void end();
    void loop();

    const UPSData& getUPSData() const;
    String getUPSStatusString() const;
    String dumpUSBDiagnostics();

    bool setBeeper(bool enable);
    String getActiveBeeperPath() const;
    
    uint8_t _iManufacturer;
    uint8_t _iProduct;
    uint8_t _iSerialNumber;
    void setLogCallback(LogCallback cb);

    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8);
    bool requestStringDescriptor(uint8_t string_index);

    bool isConnected() const;

    const HIDUsageDef* getUsageDef(uint32_t usage) const { return _hid_parser.getUsageDef(usage); }
    uint32_t getQuirks() const { return _quirks; }

    bool isControlPending() const { return _control_pending; }

    HIDParser _hid_parser;

private:
    static void usb_host_lib_task(void *arg);
    static void usb_client_task(void *arg);
    static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
    static void control_transfer_cb(usb_transfer_t *transfer);
    void handle_client_event(const usb_host_client_event_msg_t *event_msg);

    TaskHandle_t _usb_task_handle;
    TaskHandle_t _client_task_handle;
    usb_host_client_handle_t _client_handle;
    usb_device_handle_t _dev_handle;
    bool _initialized;
    bool _is_ready_to_poll;
    volatile bool _is_fetching;
    volatile bool _pending_dev_close;
    volatile bool _control_pending;
    usb_device_handle_t _dev_to_close;

    uint8_t _int_in_ep;
    uint16_t _int_in_mps;
    usb_transfer_t* _int_in_transfer;
    static void int_in_cb(usb_transfer_t *transfer);
    void handle_int_in(usb_transfer_t *transfer);

    UPSData _ups_data;
    IUPSDriver* _driver;
    LogCallback _log_cb;

    uint32_t _quirks;
};

#endif // USB_HOST_UPS_H
