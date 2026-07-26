#include "USBHostUPS.h"
#include "EatonDriver.h"

USBHostUPS::USBHostUPS() : 
    _usb_task_handle(NULL), 
    _client_task_handle(NULL), 
    _client_handle(NULL), 
    _dev_handle(NULL), 
    _initialized(false),
    _driver(nullptr) {
}

USBHostUPS::~USBHostUPS() {
    if (_driver) {
        delete _driver;
        _driver = nullptr;
    }
}

const UPSData& USBHostUPS::getUPSData() const {
    return _ups_data;
}

String USBHostUPS::getUPSStatusString() const {
    String status = "";
    if (_ups_data.acPresent && !_ups_data.discharging) status += "OL ";
    if (_ups_data.discharging) status += "OB ";
    if (_ups_data.belowRemainingCapacityLimit) status += "LB ";
    if (_ups_data.charging && !(_ups_data.remainingCapacity == 100 && _ups_data.acPresent)) status += "CHRG ";
    if (_ups_data.needReplacement) status += "RB ";
    if (_ups_data.overload) status += "OVER ";
    if (_ups_data.shutdownImminent) status += "FSD ";
    if (_ups_data.communicationLost) status += "COMM_LOST ";
    
    if (status.length() == 0) status = "Unknown";
    status.trim();
    return status;
}

bool USBHostUPS::isConnected() const {
    return _initialized && (_dev_handle != NULL);
}

bool USBHostUPS::setBeeper(bool enable) {
    if (_dev_handle == NULL) return false;

    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + 2, 0, &transfer);
    if (err != ESP_OK) return false;

    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = 0x21; // SET_REPORT type
    setup->bRequest = 0x09;      // SET_REPORT request
    setup->wValue = (0x03 << 8) | 0x1f; // Feature report, ID 0x1f
    setup->wIndex = 0;           // Interface 0
    setup->wLength = 2;

    uint8_t *data = transfer->data_buffer + sizeof(usb_setup_packet_t);
    data[0] = 0x1f; 
    data[1] = enable ? 2 : 1; 

    transfer->device_handle = _dev_handle;
    transfer->callback = control_transfer_cb;
    transfer->context = this;
    transfer->num_bytes = 8 + 2;

    err = usb_host_transfer_submit_control(_client_handle, transfer);
    if (err != ESP_OK) {
        usb_host_transfer_free(transfer);
        return false;
    }
    _ups_data.beeperEnabled = enable;
    return true;
}

void USBHostUPS::usb_host_lib_task(void *arg) {
    USBHostUPS *self = static_cast<USBHostUPS*>(arg);
    (void)self;

    Serial.println("[USBHostUPS] USB Host events task started.");
    while (true) {
        uint32_t event_flags;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    vTaskDelete(NULL);
}

void USBHostUPS::usb_client_task(void *arg) {
    USBHostUPS *self = static_cast<USBHostUPS*>(arg);
    Serial.println("[USBHostUPS] USB Host Client events task started.");
    while (true) {
        if (self->_client_handle != NULL) {
            esp_err_t err = usb_host_client_handle_events(self->_client_handle, portMAX_DELAY);
            if (err != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    vTaskDelete(NULL);
}

void USBHostUPS::client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    USBHostUPS *self = static_cast<USBHostUPS*>(arg);
    self->handle_client_event(event_msg);
}

void USBHostUPS::handle_client_event(const usb_host_client_event_msg_t *event_msg) {
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV: {
            Serial.printf("[USBHostUPS] New USB device connected. Address: %d\n", event_msg->new_dev.address);
            usb_device_handle_t dev_hdl;
            esp_err_t err = usb_host_device_open(_client_handle, event_msg->new_dev.address, &dev_hdl);
            if (err == ESP_OK) {
                const usb_device_desc_t *desc;
                err = usb_host_get_device_descriptor(dev_hdl, &desc);
                if (err == ESP_OK) {
                    Serial.printf("[USBHostUPS] Device Info: Address %d, VID %04X, PID %04X\n",
                                  event_msg->new_dev.address, desc->idVendor, desc->idProduct);
                    if (desc->idVendor == 0x0463 && desc->idProduct == 0xFFFF) {
                        Serial.println("[USBHostUPS] Eaton 3S 700 UPS detected successfully! Registered as HID.");
                        _dev_handle = dev_hdl;
                        esp_err_t claim_err = usb_host_interface_claim(_client_handle, _dev_handle, 0, 0);
                        if (claim_err == ESP_OK) {
                            Serial.println("[USBHostUPS] HID Interface 0 claimed successfully.");
                            _driver = new EatonDriver();
                            _driver->setup();
                        } else {
                            Serial.printf("[USBHostUPS] Error claiming interface 0: %d\n", claim_err);
                            usb_host_device_close(_client_handle, dev_hdl);
                            _dev_handle = NULL;
                        }
                    } else {
                        Serial.println("[USBHostUPS] Incompatible device.");
                        usb_host_device_close(_client_handle, dev_hdl);
                    }
                } else {
                    Serial.printf("[USBHostUPS] Error reading descriptor: %d\n", err);
                    usb_host_device_close(_client_handle, dev_hdl);
                }
            } else {
                Serial.printf("[USBHostUPS] Error opening device: %d\n", err);
            }
            break;
        }
        case USB_HOST_CLIENT_EVENT_DEV_GONE: {
            Serial.println("[USBHostUPS] Device disconnected.");
            _ups_data = UPSData(); // reset
            if (_driver) {
                delete _driver;
                _driver = nullptr;
            }
            if (_dev_handle != NULL && event_msg->dev_gone.dev_hdl == _dev_handle) {
                esp_err_t release_err = usb_host_interface_release(_client_handle, _dev_handle, 0);
                if (release_err == ESP_OK) {
                    Serial.println("[USBHostUPS] HID Interface 0 released successfully.");
                } else {
                    Serial.printf("[USBHostUPS] Error releasing interface 0: %d\n", release_err);
                }
                usb_host_device_close(_client_handle, _dev_handle);
                _dev_handle = NULL;
            } else {
                usb_host_device_close(_client_handle, event_msg->dev_gone.dev_hdl);
            }
            break;
        }
        default:
            break;
    }
}

bool USBHostUPS::begin() {
    if (_initialized) {
        return true;
    }

    usb_host_config_t host_config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        Serial.printf("[USBHostUPS] Error: usb_host_install failed: %d\n", err);
        return false;
    }

    BaseType_t task_err = xTaskCreatePinnedToCore(
        usb_host_lib_task,
        "usb_host_events",
        4096,
        this,
        2,
        &_usb_task_handle,
        0
    );

    if (task_err != pdPASS) {
        Serial.println("[USBHostUPS] Error: Cannot create FreeRTOS task for library events");
        return false;
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = this,
        }
    };

    err = usb_host_client_register(&client_config, &_client_handle);
    if (err != ESP_OK) {
        Serial.printf("[USBHostUPS] Error: usb_host_client_register failed: %d\n", err);
        return false;
    }

    task_err = xTaskCreatePinnedToCore(
        usb_client_task,
        "usb_client_events",
        4096,
        this,
        2,
        &_client_task_handle,
        0
    );

    if (task_err != pdPASS) {
        Serial.println("[USBHostUPS] Error: Cannot create FreeRTOS task for client events");
        return false;
    }

    _initialized = true;
    Serial.println("[USBHostUPS] USB Host and Client initialization completed.");
    return true;
}

void USBHostUPS::loop() {
    if (!_initialized || _dev_handle == NULL) {
        return;
    }

    uint32_t now = millis();
    if (_driver) {
        _driver->loop(this, _ups_data, now);
    }
}


bool USBHostUPS::requestReport(uint8_t report_id, uint8_t report_type, uint16_t expected_length) {
    if (_dev_handle == NULL) return false;

    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + expected_length, 0, &transfer);
    if (err != ESP_OK) {
        return false;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = 0xA1; 
    setup->bRequest = 0x01;      
    setup->wValue = (report_type << 8) | report_id;
    setup->wIndex = 0;           
    setup->wLength = expected_length;

    transfer->device_handle = _dev_handle;
    transfer->callback = control_transfer_cb;
    transfer->context = this;
    transfer->num_bytes = 8 + expected_length;

    err = usb_host_transfer_submit_control(_client_handle, transfer);
    if (err != ESP_OK) {
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

bool USBHostUPS::requestStringDescriptor(uint8_t string_index) {
    if (_dev_handle == NULL) return false;

    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + 255, 0, &transfer);
    if (err != ESP_OK) return false;

    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = 0x80; 
    setup->bRequest = 0x06;      
    setup->wValue = (0x03 << 8) | string_index;
    setup->wIndex = 0x0409;      
    setup->wLength = 255;

    transfer->device_handle = _dev_handle;
    transfer->callback = control_transfer_cb;
    transfer->context = this;
    transfer->num_bytes = 8 + 255;

    err = usb_host_transfer_submit_control(_client_handle, transfer);
    if (err != ESP_OK) {
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

void USBHostUPS::control_transfer_cb(usb_transfer_t *transfer) {
    USBHostUPS *self = static_cast<USBHostUPS*>(transfer->context);
    if (self != NULL) {
        if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
            usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
            uint8_t *data = transfer->data_buffer + sizeof(usb_setup_packet_t);
            size_t actual_length = transfer->actual_num_bytes - sizeof(usb_setup_packet_t);

            if (setup->bmRequestType == 0x80 && setup->bRequest == 0x06 && (setup->wValue >> 8) == 0x03) {
                if (actual_length > 2 && data[1] == 0x03) {
                    if (self->_driver) {
                        self->_driver->parseStringDescriptor(setup->wValue & 0xFF, data, actual_length, self->_ups_data);
                    }
                }
            } else if (setup->bmRequestType == 0xA1 && setup->bRequest == 0x01) {
                uint8_t report_id = setup->wValue & 0xFF;
                if (self->_driver) {
                    self->_driver->decodeReport(self, report_id, data, actual_length, self->_ups_data);
                }
            }
        }
    }
    usb_host_transfer_free(transfer);
}


