#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "Quirks.h"
#include "HIDParser.h"
#include "UPSData.h"
#include "IUSBHostUPS.h"

typedef void (*LogCallback)(const char* level, const char* msg);

class IUPSDriver;

class USBHostUPS : public IUSBHostUPS {
public:
    USBHostUPS();
    ~USBHostUPS();

    bool begin();
    void end();
    void loop();

    const UPSData& getUPSData() const override;
    String getUPSStatusString() const override;
    String dumpUSBDiagnostics();

    bool setBeeper(bool enable) override;
    bool isConnected() const override;
    
    void setLogCallback(LogCallback cb);

    const std::vector<HIDUsageDef>& getUsages() const override { return _hid_parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _hid_parser.getUsageDef(usage); }
    String getActiveBeeperPath() const override;
    uint32_t getQuirks() const override { return _quirks; }
    bool isControlPending() const override { return _control_pending; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) override;
    bool requestStringDescriptor(uint8_t string_index) override;

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
    
    String _cached_report_descriptor_hex;
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
