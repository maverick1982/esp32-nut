#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

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
    uint8_t configFrequency = 0;
    uint8_t configVoltage = 0;

    uint16_t outputVoltage = 0;

    uint16_t highVoltageTransfer = 0;
    uint8_t lowVoltageTransfer = 0;

    String manufacturer = "";
    String product = "";
    String serialNumber = "";
};

class USBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    bool begin();
    void loop();

    const UPSData& getUPSData() const;
    String getUPSStatusString() const;

    void decodeReport(uint8_t report_id, const uint8_t *data, size_t length);
    bool isConnected() const;

private:
    static void usb_host_lib_task(void *arg);
    static void usb_client_task(void *arg);
    static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
    static void control_transfer_cb(usb_transfer_t *transfer);
    void parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length);
    void handle_client_event(const usb_host_client_event_msg_t *event_msg);
    bool requestReport(uint8_t report_id, uint8_t report_type);
    bool requestStringDescriptor(uint8_t string_index);

    TaskHandle_t _usb_task_handle;
    TaskHandle_t _client_task_handle;
    usb_host_client_handle_t _client_handle;
    usb_device_handle_t _dev_handle;
    bool _initialized;

    UPSData _ups_data;
    uint32_t _last_poll;
};

#endif // USB_HOST_UPS_H
