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

private:
    static void usb_host_lib_task(void *arg);
    static void usb_client_task(void *arg);
    static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
    void handle_client_event(const usb_host_client_event_msg_t *event_msg);

    TaskHandle_t _usb_task_handle;
    TaskHandle_t _client_task_handle;
    usb_host_client_handle_t _client_handle;
    usb_device_handle_t _dev_handle;
    bool _initialized;
};

#endif // USB_HOST_UPS_H
