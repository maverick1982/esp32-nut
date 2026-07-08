#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

class USBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    bool begin();
    void loop();

    uint8_t getBatteryCharge() const;
    String getUPSStatus() const;
    float getInputVoltage() const;
    void decodeReport(uint8_t report_id, const uint8_t *data, size_t length);

private:
    static void usb_host_lib_task(void *arg);
    static void usb_client_task(void *arg);
    static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
    static void control_transfer_cb(usb_transfer_t *transfer);
    void handle_client_event(const usb_host_client_event_msg_t *event_msg);
    bool requestReport(uint8_t report_id, uint8_t report_type);

    TaskHandle_t _usb_task_handle;
    TaskHandle_t _client_task_handle;
    usb_host_client_handle_t _client_handle;
    usb_device_handle_t _dev_handle;
    bool _initialized;

    uint8_t _battery_charge;
    String _ups_status;
    float _input_voltage;
};

#endif // USB_HOST_UPS_H
