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

    uint16_t highVoltageTransfer = 0;
    uint16_t lowVoltageTransfer = 0;

    String manufacturer = "";
    String product = "";
    String serialNumber = "";

    uint8_t load = 0;
    uint16_t realPower = 0;
    bool beeperEnabled = true;
    int32_t delayShutdown = -1;
    int32_t delayStart = -1;
    int32_t timerStart = -1;
    int32_t timerShutdown = -1;

    String batteryType = "";
    String upsType = "";
    uint16_t outputVoltageNominal = 0;
    uint16_t outputFrequencyNominal = 0;
};

class IUPSDriver;

class USBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    bool begin();
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

    UPSData _ups_data;
    IUPSDriver* _driver;
    LogCallback _log_cb;

    uint32_t _quirks;
};

#endif // USB_HOST_UPS_H
