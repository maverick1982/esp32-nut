#include "USBHostUPS.h"
#include "IUPSDriver.h"
#include "EatonDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "GenericDriver.h"
#include <ArduinoJson.h>

struct DiagContext {
    volatile bool done;
    uint8_t data[1024];
    size_t len;
};

USBHostUPS::USBHostUPS() : 
    _usb_task_handle(NULL), 
    _client_task_handle(NULL), 
    _client_handle(NULL), 
    _dev_handle(NULL), 
    _initialized(false),
    _is_ready_to_poll(false),
    _driver(nullptr),
    _log_cb(nullptr),
    _quirks(0) {
}

USBHostUPS::~USBHostUPS() {
    if (_driver) {
        delete _driver;
        _driver = nullptr;
    }
}

void USBHostUPS::setLogCallback(LogCallback cb) {
    _log_cb = cb;
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
                    _dev_handle = dev_hdl;
                    esp_err_t claim_err = usb_host_interface_claim(_client_handle, _dev_handle, 0, 0);
                    if (claim_err == ESP_OK) {
                        Serial.println("[USBHostUPS] HID Interface 0 claimed successfully.");
                        switch(desc->idVendor) {
                            case 0x0463:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: Eaton");
                                Serial.println("[USBHostUPS] Recognized vendor: Eaton");
                                _driver = new EatonDriver();
                                _ups_data.upsType = "Eaton";
                                break;
                            case 0x051d:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: APC");
                                Serial.println("[USBHostUPS] Recognized vendor: APC");
                                _driver = new APCDriver();
                                _ups_data.upsType = "APC";
                                break;
                            case 0x0764:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: CyberPower");
                                Serial.println("[USBHostUPS] Recognized vendor: CyberPower");
                                _driver = new CyberPowerDriver();
                                _ups_data.upsType = "CyberPower";
                                break;
                            default:
                                {
                                    char buf[64];
                                    snprintf(buf, sizeof(buf), "[USBHostUPS] Unknown vendor: %04X", desc->idVendor);
                                    if (_log_cb) _log_cb("INFO", buf);
                                }
                                Serial.printf("[USBHostUPS] Unknown vendor: %04X\n", desc->idVendor);
                                _driver = new GenericDriver();
                                _ups_data.upsType = "Generic";
                                break;
                        }

                        _quirks = 0;
                        for (int q = 0; UPS_QUIRKS[q].vid != 0; q++) {
                            if (UPS_QUIRKS[q].vid == desc->idVendor && (UPS_QUIRKS[q].pid == 0xFFFF || UPS_QUIRKS[q].pid == desc->idProduct)) {
                                _quirks |= UPS_QUIRKS[q].flags;
                            }
                        }
                        
                        _is_ready_to_poll = false;
                        
                        xTaskCreate([](void* arg) {
                            USBHostUPS* self = (USBHostUPS*)arg;
                            usb_transfer_t *transfer = NULL;
                            if (usb_host_transfer_alloc(8 + 1024, 0, &transfer) == ESP_OK) {
                                uint16_t report_len = 1024;
                                const usb_config_desc_t *config_desc = NULL;
                                esp_err_t cfg_err = usb_host_get_active_config_descriptor(self->_dev_handle, &config_desc);
                                
                                DiagContext ctx_cfg;
                                if (cfg_err != ESP_OK || config_desc == NULL) {
                                    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
                                    setup->bmRequestType = 0x80;
                                    setup->bRequest = 0x06;
                                    setup->wValue = 0x0200; // Config Descriptor, index 0
                                    setup->wIndex = 0;
                                    setup->wLength = 512;
                                    
                                    ctx_cfg.done = false;
                                    ctx_cfg.len = 0;
                                    
                                    transfer->device_handle = self->_dev_handle;
                                    transfer->context = &ctx_cfg;
                                    transfer->callback = [](usb_transfer_t *t) {
                                        DiagContext *c = (DiagContext*)t->context;
                                        if (t->status == USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes > sizeof(usb_setup_packet_t)) {
                                            c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
                                            if (c->len > 1024) c->len = 1024;
                                            if (c->len > 0) memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
                                        }
                                        c->done = true;
                                    };
                                    transfer->num_bytes = 8 + 512;
                                    transfer->timeout_ms = 1000;
                                    
                                    if (usb_host_transfer_submit_control(self->_client_handle, transfer) == ESP_OK) {
                                        int timeout = 150;
                                        while (!ctx_cfg.done && timeout > 0) {
                                            vTaskDelay(pdMS_TO_TICKS(10));
                                            timeout--;
                                        }
                                        if (ctx_cfg.done && ctx_cfg.len >= 9) {
                                            config_desc = (const usb_config_desc_t *)ctx_cfg.data;
                                        }
                                    }
                                }

                                if (config_desc != NULL) {
                                    const uint8_t* p = (const uint8_t*)config_desc;
                                    size_t offset = 0;
                                    size_t max_len = (cfg_err == ESP_OK) ? config_desc->wTotalLength : ctx_cfg.len;
                                    while (offset < max_len && offset + 1 < max_len) {
                                        uint8_t len = p[offset];
                                        uint8_t type = p[offset + 1];
                                        if (len == 0) break;
                                        if (type == 0x21 && len >= 9 && offset + 8 < max_len) {
                                            report_len = p[offset + 7] | (p[offset + 8] << 8);
                                            break;
                                        }
                                        offset += len;
                                    }
                                }
                                if (report_len == 0 || report_len > 1024) report_len = 1024;


                                usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
                                setup->bmRequestType = 0x81;
                                setup->bRequest = 0x06;
                                setup->wValue = 0x2200; // Report Descriptor
                                setup->wIndex = 0;
                                setup->wLength = report_len;
                                
                                DiagContext ctx;
                                ctx.done = false;
                                ctx.len = 0;
                                
                                transfer->device_handle = self->_dev_handle;
                                transfer->context = &ctx;
                                transfer->callback = [](usb_transfer_t *t) {
                                    DiagContext *c = (DiagContext*)t->context;
                                    if (t->status == USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes > sizeof(usb_setup_packet_t)) {
                                        c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
                                        if (c->len > 1024) c->len = 1024;
                                        if (c->len > 0) memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
                                    }
                                    c->done = true;
                                };
                                transfer->num_bytes = 8 + report_len;
                                transfer->timeout_ms = 5000;
                                
                                esp_err_t err_transfer = usb_host_transfer_submit_control(self->_client_handle, transfer);
                                if (err_transfer == ESP_OK) {
                                    int timeout = 600;
                                    while (!ctx.done && timeout > 0) {
                                        vTaskDelay(pdMS_TO_TICKS(10));
                                        timeout--;
                                    }
                                    if (ctx.done && ctx.len > 0) {
                                        Serial.printf("[USBHostUPS] Report Descriptor fetched, len %d\n", ctx.len);
                                        self->_hid_parser.parseReportDescriptor(ctx.data, ctx.len);
                                    } else {
                                        Serial.printf("[USBHostUPS] Report Descriptor fetch failed. status=%d, len=%d\n", ctx.done, ctx.len);
                                    }
                                }
                                usb_host_transfer_free(transfer);
                            }
                            if (self->_driver) {
                                self->_driver->setup();
                            }
                            self->_is_ready_to_poll = true;
                            vTaskDelete(NULL);
                        }, "fetch_desc", 4096, this, 5, NULL);
                    } else {
                        Serial.printf("[USBHostUPS] Error claiming interface 0: %d\n", claim_err);
                        usb_host_device_close(_client_handle, dev_hdl);
                        _dev_handle = NULL;
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
    if (!_initialized || _dev_handle == NULL || !_is_ready_to_poll) {
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
                        self->_driver->parseStringDescriptor(self, setup->wValue & 0xFF, data, actual_length, self->_ups_data);
                    }
                }
            } else if (setup->bmRequestType == 0xA1 && setup->bRequest == 0x01) {
                uint8_t report_id = setup->wValue & 0xFF;
                if (self->_driver) {
                    uint8_t rep_type = (setup->wValue >> 8) & 0xFF; self->_driver->decodeReport(self, report_id, rep_type, data, actual_length, self->_ups_data);
                }
            }
        }
    }
    usb_host_transfer_free(transfer);
}

String USBHostUPS::dumpUSBDiagnostics() {
    JsonDocument doc;
    doc["driver"] = _ups_data.upsType.length() > 0 ? _ups_data.upsType : "Unknown";
    
    if (_dev_handle == NULL) {
        doc["error"] = "No device connected";
        String out;
        serializeJson(doc, out);
        return out;
    }

    const usb_device_desc_t *desc;
    if (usb_host_get_device_descriptor(_dev_handle, &desc) == ESP_OK) {
        char hex[10];
        sprintf(hex, "0x%04X", desc->idVendor);
        doc["vid"] = hex;
        sprintf(hex, "0x%04X", desc->idProduct);
        doc["pid"] = hex;
    }

    doc["manufacturer"] = _ups_data.manufacturer;
    doc["product"] = _ups_data.product;

    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + 1024, 0, &transfer);
    if (err == ESP_OK) {
        uint16_t report_len = 1024;
        const usb_config_desc_t *config_desc = NULL;
        esp_err_t cfg_err = usb_host_get_active_config_descriptor(_dev_handle, &config_desc);
        
        DiagContext ctx_cfg;
        if (cfg_err != ESP_OK || config_desc == NULL) {
            usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
            setup->bmRequestType = 0x80;
            setup->bRequest = 0x06;
            setup->wValue = 0x0200; // Config Descriptor
            setup->wIndex = 0;
            setup->wLength = 512;
            
            ctx_cfg.done = false;
            ctx_cfg.len = 0;
            
            transfer->device_handle = _dev_handle;
            transfer->context = &ctx_cfg;
            transfer->callback = [](usb_transfer_t *t) {
                DiagContext *c = (DiagContext*)t->context;
                if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
                    c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
                    if (c->len > 1024) c->len = 1024;
                    memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
                }
                c->done = true;
            };
            transfer->num_bytes = 8 + 512;
            transfer->timeout_ms = 1000;
            
            if (usb_host_transfer_submit_control(_client_handle, transfer) == ESP_OK) {
                int timeout = 150;
                while (!ctx_cfg.done && timeout > 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    timeout--;
                }
                if (ctx_cfg.done && ctx_cfg.len >= 9) {
                    config_desc = (const usb_config_desc_t *)ctx_cfg.data;
                }
            }
        }
        
        doc["cfg_err"] = cfg_err;
        if (config_desc != NULL) {
            size_t max_len = (cfg_err == ESP_OK) ? config_desc->wTotalLength : ctx_cfg.len;
            doc["cfg_total_len"] = max_len;
            const uint8_t* p = (const uint8_t*)config_desc;
            size_t offset = 0;
            while (offset < max_len && offset + 1 < max_len) {
                uint8_t len = p[offset];
                uint8_t type = p[offset + 1];
                if (len == 0) break;
                if (type == 0x21 && len >= 9 && offset + 8 < max_len) {
                    report_len = p[offset + 7] | (p[offset + 8] << 8);
                    break;
                }
                offset += len;
            }
        }
        if (report_len == 0 || report_len > 1024) report_len = 1024;
        doc["report_len_req"] = report_len;

        usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
        setup->bmRequestType = 0x81;
        setup->bRequest = 0x06;
        setup->wValue = 0x2200; // Report Descriptor
        setup->wIndex = 0;
        setup->wLength = report_len;

        DiagContext ctx;
        ctx.done = false;
        ctx.len = 0;
        
        transfer->device_handle = _dev_handle;
        transfer->context = &ctx;
        transfer->callback = [](usb_transfer_t *t) {
            DiagContext *c = (DiagContext*)t->context;
            if (t->status == USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes > sizeof(usb_setup_packet_t)) {
                c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
                if (c->len > 1024) c->len = 1024;
                if (c->len > 0) memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
            }
            c->done = true;
        };
        transfer->num_bytes = 8 + report_len;
        transfer->timeout_ms = 5000;

        err = usb_host_transfer_submit_control(_client_handle, transfer);
        if (err == ESP_OK) {
            int timeout = 600;
            while (!ctx.done && timeout > 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                timeout--;
            }
            if (ctx.done && ctx.len > 0) {
                JsonArray raw = doc["report_descriptor_hex"].to<JsonArray>();
                for (size_t i = 0; i < ctx.len; i++) {
                    char hexb[5];
                    sprintf(hexb, "0x%02X", ctx.data[i]);
                    raw.add(hexb);
                }
            } else {
                doc["error"] = "Report Descriptor timeout";
            }
        } else {
            doc["error"] = "Failed to submit Report Descriptor transfer";
        }
        usb_host_transfer_free(transfer);
    } else {
        doc["error"] = "Failed to allocate transfer";
    }

    String out;
    serializeJson(doc, out);
    return out;
}


