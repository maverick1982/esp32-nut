#ifndef USB_HOST_UPS_H
#define USB_HOST_UPS_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <mutex>
#include "usb/usb_host.h"
#include "usb/hid_host.h"
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

    void lock() const override { _mutex.lock(); }
    void unlock() const override { _mutex.unlock(); }

    UPSDataLock getUPSData() const override;
    String getUPSStatusString() const override;
    String dumpUSBDiagnostics();

    bool setBeeper(bool enable) override;
    bool isConnected() const override;
    bool supportsBeeperToggle() const override;
    
    void setLogCallback(LogCallback cb);
    void logDebug(const String& msg) const override;

    const std::vector<HIDUsageDef>& getUsages() const override { return _hid_parser.getUsages(); }
    const HIDUsageDef* getUsageDef(uint32_t usage) const override { return _hid_parser.getUsageDef(usage); }
    String getActiveBeeperPath() const override;
    uint32_t getQuirks() const override { return _quirks; }
    bool isControlPending() const override { return _control_pending; }
    bool requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length = 8) override;
    bool requestStringDescriptor(uint8_t string_index) override;
    uint16_t getVID() const override { return _vid; }
    uint16_t getPID() const override { return _pid; }

    HIDParser _hid_parser;

public:
    static void populateStringsFromDeviceInfo(const hid_host_dev_info_t& dev_info, UPSData& ups_data);
private:
    mutable std::recursive_mutex _mutex;
    static void hid_host_driver_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);
    static void hid_host_interface_event_cb(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg);
    static void control_transfer_cb(usb_transfer_t *transfer);
    static void usb_host_lib_task(void *arg);
    TaskHandle_t _usb_task_handle;
    volatile bool _usb_task_run;
    
    void handle_driver_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event);
    void handle_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event);
    
    hid_host_device_handle_t _hid_dev_handle;
    usb_device_handle_t _dev_handle;
    
    String _cached_report_descriptor_hex;
    uint16_t _vid;
    uint16_t _pid;
    bool _initialized;
    bool _is_ready_to_poll;
    volatile bool _is_fetching;
    volatile bool _control_pending;

    UPSData _ups_data;
    IUPSDriver* _driver;
    LogCallback _log_cb;

    uint32_t _quirks;
};

#endif // USB_HOST_UPS_H
