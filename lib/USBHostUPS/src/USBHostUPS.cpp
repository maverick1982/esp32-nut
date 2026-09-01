#include "USBHostUPS.h"
#include "IUPSDriver.h"
#include "EatonDriver.h"
#include "APCDriver.h"
#include "CyberPowerDriver.h"
#include "GenericDriver.h"
#include "PowercomDriver.h"
#include <ArduinoJson.h>
#include <atomic>

struct DiagContext {
    volatile bool done;
    uint8_t* data;
    size_t len;
    size_t capacity;

    DiagContext(size_t cap = 4096) : done(false), len(0), capacity(cap) {
        data = (uint8_t*)malloc(capacity);
    }

    ~DiagContext() {
        if (data) free(data);
    }
};



USBHostUPS::USBHostUPS() : 
    _usb_task_handle(NULL), 
    _client_task_handle(NULL), 
    _client_handle(NULL), 
    _dev_handle(NULL), 
    _initialized(false),
    _is_ready_to_poll(false),
    _is_fetching(false),
    _pending_dev_close(false),
    _control_pending(false),
    _dev_to_close(NULL),
    _int_in_ep(0),
    _int_in_mps(0),
    _int_in_transfer(NULL),
    _driver(nullptr),
    _log_cb(nullptr),
    _quirks(0) {
    _iManufacturer = 0;
    _iProduct = 0;
    _iSerialNumber = 0;
}

USBHostUPS::~USBHostUPS() {
    end();
    if (_driver) {
        delete _driver;
        _driver = nullptr;
    }
}

void USBHostUPS::end() {
    if (!_initialized) {
        return;
    }

    _is_ready_to_poll = false;
    _initialized = false;

    // Release interface and close device if open
    if (_dev_handle != NULL) {
        if (_int_in_transfer != NULL) {
            // Give brief time for transfer to finish or free
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        usb_host_interface_release(_client_handle, _dev_handle, 0);
        usb_host_device_close(_client_handle, _dev_handle);
        _dev_handle = NULL;
    }

    if (_dev_to_close != NULL) {
        usb_host_interface_release(_client_handle, _dev_to_close, 0);
        usb_host_device_close(_client_handle, _dev_to_close);
        _dev_to_close = NULL;
    }

    if (_client_handle != NULL) {
        usb_host_client_deregister(_client_handle);
        _client_handle = NULL;
    }

    if (_client_task_handle != NULL) {
        vTaskDelete(_client_task_handle);
        _client_task_handle = NULL;
    }

    if (_usb_task_handle != NULL) {
        vTaskDelete(_usb_task_handle);
        _usb_task_handle = NULL;
    }

    usb_host_uninstall();
    Serial.println("[USBHostUPS] USB Host uninstalled.");
}

void USBHostUPS::setLogCallback(LogCallback cb) {
    _log_cb = cb;
}

const UPSData& USBHostUPS::getUPSData() const {
    return _ups_data;
}

String USBHostUPS::getUPSStatusString() const {
    return UPSData::computeUPSStatusString(_ups_data);
}

bool USBHostUPS::isConnected() const {
    return _initialized && (_dev_handle != NULL);
}

bool USBHostUPS::supportsBeeperToggle() const {
    if (_quirks & QUIRK_NO_BEEPER_CONTROL) {
        return false;
    }
    return _hid_parser.hasFeatureBeeperControl();
}

String USBHostUPS::getActiveBeeperPath() const {
    for (const auto& u : _hid_parser.getUsages()) {
        if (u.path == "UPS.PowerSummary.AudibleAlarmControl" || 
            u.path == "UPS.BatterySystem.Battery.AudibleAlarmControl" || 
            u.path == "UPS.AudibleAlarmControl") {
            return u.path;
        }
    }
    return "";
}

bool USBHostUPS::setBeeper(bool enable) {
    if (_dev_handle == NULL) return false;

    uint8_t report_id = 0x1f; // Fallback
    uint16_t bit_size = 8;
    uint16_t bit_offset = 0;
    String active_path = getActiveBeeperPath();
    if (active_path == "") return false;

    for (const auto& u : _hid_parser.getUsages()) {
        if (u.path == active_path) {
            report_id = u.report_id;
            bit_size = u.bit_size;
            bit_offset = u.bit_offset;
            break;
        }
    }

    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + 256, 0, &transfer);
    if (err != ESP_OK) return false;

    // STEP 1: GET_REPORT
    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = 0xA1; // GET_REPORT
    setup->bRequest = 0x01;
    setup->wValue = (0x03 << 8) | report_id;
    setup->wIndex = 0;
    setup->wLength = 255;

    DiagContext ctx;
    ctx.done = false;
    ctx.len = 0;

    transfer->device_handle = _dev_handle;
    transfer->context = &ctx;
    transfer->callback = [](usb_transfer_t *t) {
        DiagContext *c = (DiagContext*)t->context;
        if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > sizeof(usb_setup_packet_t)) {
            c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
            if (c->len > c->capacity) c->len = c->capacity;
            memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
        }
        c->done = true;
    };
    transfer->num_bytes = 8 + 255;
    transfer->timeout_ms = 1000;

    if (usb_host_transfer_submit_control(_client_handle, transfer) != ESP_OK) {
        usb_host_transfer_free(transfer);
        return false;
    }

    int timeout = 150;
    while (!ctx.done && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }

    if (!ctx.done || ctx.len == 0) {
        usb_host_transfer_free(transfer);
        return false;
    }

    // STEP 2: MODIFY BUFFER
    uint16_t byte_idx = (bit_offset / 8) + (report_id != 0 ? 1 : 0);
    uint8_t bit_shift = bit_offset % 8;

    if (byte_idx < ctx.len) {
        uint8_t val = _driver ? _driver->encodeBeeperValue(enable, bit_size) : (bit_size == 1 ? (enable ? 1 : 0) : (enable ? 2 : 1));
        
        uint8_t mask = (1 << bit_size) - 1;
        ctx.data[byte_idx] &= ~(mask << bit_shift);
        ctx.data[byte_idx] |= (val & mask) << bit_shift;
    }

    // STEP 3: SET_REPORT
    setup->bmRequestType = 0x21; // SET_REPORT
    setup->bRequest = 0x09;
    setup->wValue = (0x03 << 8) | report_id;
    setup->wIndex = 0;
    setup->wLength = ctx.len;

    uint8_t *data = transfer->data_buffer + sizeof(usb_setup_packet_t);
    memcpy(data, ctx.data, ctx.len);

    ctx.done = false; // Reset for SET_REPORT
    transfer->num_bytes = 8 + ctx.len;
    
    if (usb_host_transfer_submit_control(_client_handle, transfer) != ESP_OK) {
        usb_host_transfer_free(transfer);
        return false;
    }

    timeout = 150;
    while (!ctx.done && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }

    usb_host_transfer_free(transfer);
    
    if (ctx.done) {
        _ups_data.beeperEnabled = enable;
        return true;
    }
    return false;
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
                    
                    _iManufacturer = desc->iManufacturer;
                    _iProduct = desc->iProduct;
                    _iSerialNumber = desc->iSerialNumber;
                    _vid = desc->idVendor;
                    _pid = desc->idProduct;
                    
                    _dev_handle = dev_hdl;
                    esp_err_t claim_err = usb_host_interface_claim(_client_handle, _dev_handle, 0, 0);
                    if (claim_err == ESP_OK) {
                        Serial.println("[USBHostUPS] HID Interface 0 claimed successfully.");
                        switch(desc->idVendor) {
                            case 0x0463:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: Eaton");
                                Serial.println("[USBHostUPS] Recognized vendor: Eaton");
                                _driver = new EatonDriver();
                                _ups_data.has.upsType = true; _ups_data.upsType = "Eaton";
                                break;
                            case 0x051d:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: APC");
                                Serial.println("[USBHostUPS] Recognized vendor: APC");
                                _driver = new APCDriver();
                                _ups_data.has.upsType = true; _ups_data.upsType = "APC";
                                break;
                            case 0x0764:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: CyberPower");
                                Serial.println("[USBHostUPS] Recognized vendor: CyberPower");
                                _driver = new CyberPowerDriver();
                                _ups_data.has.upsType = true; _ups_data.upsType = "CyberPower";
                                break;
                            case 0x0D9F:
                                if (_log_cb) _log_cb("INFO", "[USBHostUPS] Recognized vendor: Powercom");
                                Serial.println("[USBHostUPS] Recognized vendor: Powercom");
                                _driver = new PowercomDriver();
                                _ups_data.upsType = "Powercom";
                                break;
                            default:
                                {
                                    char buf[64];
                                    snprintf(buf, sizeof(buf), "[USBHostUPS] Unknown vendor: %04X", desc->idVendor);
                                    if (_log_cb) _log_cb("INFO", buf);
                                }
                                Serial.printf("[USBHostUPS] Unknown vendor: %04X\n", desc->idVendor);
                                _driver = new GenericDriver();
                                _ups_data.has.upsType = true; _ups_data.upsType = "Generic";
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
                            self->_is_fetching = true;
                            usb_transfer_t *transfer = NULL;
                            if (usb_host_transfer_alloc(8 + 4096, 0, &transfer) == ESP_OK) {
                                uint16_t report_len = 4096;
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
                                            if (c->len > c->capacity) c->len = c->capacity;
                                            if (c->len > 0) memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
                                        }
                                        c->done = true;
                                    };
                                    transfer->num_bytes = 8 + 512;
                                    transfer->timeout_ms = 1000;
                                    
                                    if (usb_host_transfer_submit_control(self->_client_handle, transfer) == ESP_OK) {
                                        while (!ctx_cfg.done) {
                                            vTaskDelay(pdMS_TO_TICKS(10));
                                        }
                                        if (ctx_cfg.len >= 9) {
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
                                        } else if (type == 0x05 && len >= 7 && offset + 6 < max_len) { // Endpoint Descriptor
                                            uint8_t ep_addr = p[offset + 2];
                                            uint8_t ep_attr = p[offset + 3];
                                            uint16_t ep_mps = p[offset + 4] | (p[offset + 5] << 8);
                                            if ((ep_addr & 0x80) && (ep_attr & 0x03) == 0x03) { // IN and Interrupt
                                                self->_int_in_ep = ep_addr;
                                                self->_int_in_mps = ep_mps;
                                            }
                                        }
                                        offset += len;
                                    }
                                }
                                if (report_len == 0 || report_len > 4000) report_len = 4000;


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
                                        if (c->len > c->capacity) c->len = c->capacity;
                                        if (c->len > 0) memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
                                    }
                                    c->done = true;
                                };
                                transfer->num_bytes = 8 + report_len;
                                transfer->timeout_ms = 5000;
                                
                                esp_err_t err_transfer = usb_host_transfer_submit_control(self->_client_handle, transfer);
                                if (err_transfer == ESP_OK) {
                                    while (!ctx.done) {
                                        vTaskDelay(pdMS_TO_TICKS(10));
                                    }
                                    if (ctx.len > 0) {
                                        Serial.printf("[USBHostUPS] Report Descriptor fetched, len %d\n", ctx.len);
                                        self->_hid_parser.parseReportDescriptor(ctx.data, ctx.len);
                                        
                                        DynamicJsonDocument temp_doc(4096);
                                        JsonArray arr = temp_doc.to<JsonArray>();
                                        for (size_t i = 0; i < ctx.len; i++) {
                                            char hexb[5];
                                            sprintf(hexb, "0x%02X", ctx.data[i]);
                                            arr.add(hexb);
                                        }
                                        serializeJson(temp_doc, self->_cached_report_descriptor_hex);
                                    } else {
                                        Serial.printf("[USBHostUPS] Report Descriptor fetch failed. status=%d, len=%d\n", ctx.done, ctx.len);
                                    }
                                }
                                usb_host_transfer_free(transfer);
                            }
                            if (self->_driver) {
                                self->_driver->setup();
                            }
                            if (self->_int_in_ep != 0 && self->_int_in_mps != 0) {
                                Serial.printf("[USBHostUPS] Starting Interrupt IN polling on EP 0x%02X (MPS: %d)\n", self->_int_in_ep, self->_int_in_mps);
                                if (usb_host_transfer_alloc(self->_int_in_mps, 0, &self->_int_in_transfer) == ESP_OK) {
                                    self->_int_in_transfer->device_handle = self->_dev_handle;
                                    self->_int_in_transfer->bEndpointAddress = self->_int_in_ep;
                                    self->_int_in_transfer->callback = USBHostUPS::int_in_cb;
                                    self->_int_in_transfer->context = self;
                                    self->_int_in_transfer->num_bytes = self->_int_in_mps;
                                    if (usb_host_transfer_submit(self->_int_in_transfer) != ESP_OK) {
                                        Serial.println("[USBHostUPS] Failed to submit Interrupt IN transfer");
                                        usb_host_transfer_free(self->_int_in_transfer);
                                        self->_int_in_transfer = NULL;
                                    }
                                }
                            }
                            self->_is_ready_to_poll = true;
                            self->_is_fetching = false;
                            vTaskDelete(NULL);
                        }, "fetch_desc", 8192, this, 5, NULL);
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
                _dev_to_close = _dev_handle;
                _pending_dev_close = true;
                _dev_handle = NULL;
            } else {
                _dev_to_close = event_msg->dev_gone.dev_hdl;
                _pending_dev_close = true;
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
    if (_pending_dev_close && !_is_fetching) {
        if (_int_in_transfer != NULL) return; // Wait until transfer is freed
        
        esp_err_t release_err = usb_host_interface_release(_client_handle, _dev_to_close, 0);
        if (release_err == ESP_ERR_INVALID_STATE) {
            // Endpoints might still have transfers in flight
            return;
        }
        
        if (release_err == ESP_OK) {
            Serial.println("[USBHostUPS] HID Interface 0 released successfully.");
        } else {
            Serial.printf("[USBHostUPS] Error releasing interface 0: %d\n", release_err);
        }

        usb_host_device_close(_client_handle, _dev_to_close);
        _pending_dev_close = false;
        _dev_to_close = NULL;
    }

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
    
    uint16_t exact_len = 0;
    for (const auto& u : _hid_parser.getUsages()) {
        if (u.report_id == report_id && u.report_type == report_type) {
            uint16_t byte_end = (u.bit_offset + u.bit_size + 7) / 8;
            if (byte_end > exact_len) exact_len = byte_end;
        }
    }
    if (exact_len > 0) expected_length = exact_len + 1;
    
    uint16_t alloc_length = (expected_length + 63) & ~63;
    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + alloc_length, 0, &transfer);
    if (err != ESP_OK) return false;

    if (_control_pending) {
        usb_host_transfer_free(transfer);
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
    transfer->num_bytes = 8 + alloc_length;
    transfer->timeout_ms = 1000;



    _control_pending = true;
    err = usb_host_transfer_submit_control(_client_handle, transfer);
    if (err != ESP_OK) {
        _control_pending = false;
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

bool USBHostUPS::requestStringDescriptor(uint8_t string_index) {
    if (_dev_handle == NULL) return false;
    
    uint16_t alloc_length = 256;
    usb_transfer_t *transfer = NULL;
    esp_err_t err = usb_host_transfer_alloc(8 + alloc_length, 0, &transfer);
    if (err != ESP_OK) return false;

    if (_control_pending) {
        usb_host_transfer_free(transfer);
        return false;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = 0x80; 
    setup->bRequest = 0x06;      
    setup->wValue = (0x03 << 8) | string_index;
    setup->wIndex = 0x0409;      
    setup->wLength = 255;

    transfer->device_handle = _dev_handle;
    transfer->callback = control_transfer_cb;
    transfer->context = this;
    transfer->num_bytes = 8 + alloc_length;
    transfer->timeout_ms = 1000;



    _control_pending = true;
    err = usb_host_transfer_submit_control(_client_handle, transfer);
    if (err != ESP_OK) {
        _control_pending = false;
        usb_host_transfer_free(transfer);
        return false;
    }
    return true;
}

void USBHostUPS::control_transfer_cb(usb_transfer_t *transfer) {
    USBHostUPS *self = static_cast<USBHostUPS*>(transfer->context);
    if (self != NULL) {
        self->_control_pending = false;
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
                if (actual_length > 0 && self->_driver) {
                    uint8_t report_id = setup->wValue & 0xFF;
                    uint8_t rep_type = (setup->wValue >> 8) & 0xFF; 
                    self->_driver->decodeReport(self, report_id, rep_type, data, actual_length, self->_ups_data);
                }
            }
        }
    }
    usb_host_transfer_free(transfer);
}

struct AsyncDiagContext {
    std::atomic<int> state; // 0: PENDING, 1: COMPLETED, 2: TIMED_OUT
    size_t len;
    uint8_t* data;
    size_t capacity;

    AsyncDiagContext(size_t cap = 4096) : state(0), len(0), capacity(cap) {
        data = (uint8_t*)malloc(capacity);
    }
    
    ~AsyncDiagContext() {
        if (data) free(data);
    }
};

String USBHostUPS::dumpUSBDiagnostics() {
    JsonDocument doc;
    doc["driver"] = _ups_data.upsType.length() > 0 ? _ups_data.upsType : "Unknown";
    
    if (_dev_handle == NULL) {
        doc["error"] = "No device connected";
        String out;
        serializeJson(doc, out);
        return out;
    }

    struct PollingPause {
        USBHostUPS* host;
        bool was_ready;
        PollingPause(USBHostUPS* h) : host(h) {
            was_ready = host->_is_ready_to_poll;
            host->_is_ready_to_poll = false;
            uint32_t start = millis();
            while (host->_control_pending && (millis() - start < 2000)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        ~PollingPause() {
            host->_is_ready_to_poll = was_ready;
        }
    } pause(this);

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
    esp_err_t err = usb_host_transfer_alloc(8 + 4096, 0, &transfer);
    if (err == ESP_OK) {
        uint16_t report_len = 4096;
        const usb_config_desc_t *config_desc = NULL;
        esp_err_t cfg_err = usb_host_get_active_config_descriptor(_dev_handle, &config_desc);
        
        AsyncDiagContext *ctx_cfg = new AsyncDiagContext();
        ctx_cfg->state = 0;
        ctx_cfg->len = 0;
        
        if (cfg_err != ESP_OK || config_desc == NULL) {
            usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
            setup->bmRequestType = 0x80;
            setup->bRequest = 0x06;
            setup->wValue = 0x0200; // Config Descriptor
            setup->wIndex = 0;
            setup->wLength = 512;
            
            transfer->device_handle = _dev_handle;
            transfer->context = ctx_cfg;
            transfer->callback = [](usb_transfer_t *t) {
                AsyncDiagContext *c = (AsyncDiagContext*)t->context;
                if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
                    c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
                    if (c->len > c->capacity) c->len = c->capacity;
                    memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
                }
                int expected = 0;
                if (!c->state.compare_exchange_strong(expected, 1)) {
                    delete c;
                    usb_host_transfer_free(t);
                }
            };
            transfer->num_bytes = 8 + 512;
            transfer->timeout_ms = 1000;
            
            if (usb_host_transfer_submit_control(_client_handle, transfer) == ESP_OK) {
                uint32_t start = millis();
                while (ctx_cfg->state == 0 && (millis() - start < 1500)) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                int expected = 0;
                if (ctx_cfg->state.compare_exchange_strong(expected, 2)) {
                    doc["cfg_err"] = "Config Descriptor timeout";
                    String out;
                    serializeJson(doc, out);
                    return out; // Transfer is leaked safely to the callback
                }
                if (ctx_cfg->state == 1 && ctx_cfg->len >= 9) {
                    config_desc = (const usb_config_desc_t *)ctx_cfg->data;
                }
            }
        }
        
        doc["cfg_err"] = cfg_err;
        uint16_t setup_wIndex = 0;
        if (config_desc != NULL) {
            size_t max_len = (cfg_err == ESP_OK) ? config_desc->wTotalLength : ctx_cfg->len;
            doc["cfg_total_len"] = max_len;
            const uint8_t* p = (const uint8_t*)config_desc;
            size_t offset = 0;
            uint8_t current_iface = 0;
            while (offset < max_len && offset + 1 < max_len) {
                uint8_t len = p[offset];
                uint8_t type = p[offset + 1];
                if (len == 0) break;
                if (type == 0x04 && len >= 9) { // Interface Descriptor
                    current_iface = p[offset + 2]; // bInterfaceNumber
                }
                if (type == 0x21 && len >= 9 && offset + 8 < max_len) {
                    report_len = p[offset + 7] | (p[offset + 8] << 8);
                    break;
                }
                offset += len;
            }
            // Use the interface number where we found the HID descriptor (or default 0)
            setup_wIndex = current_iface;
        }
        if (report_len == 0 || report_len > 4000) report_len = 4000;
        doc["report_len_req"] = report_len;

        usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
        setup->bmRequestType = 0x81;
        setup->bRequest = 0x06;
        setup->wValue = 0x2200; // Report Descriptor
        setup->wIndex = setup_wIndex;
        setup->wLength = report_len;

        AsyncDiagContext *ctx = new AsyncDiagContext();
        ctx->state = 0;
        ctx->len = 0;
        
        transfer->device_handle = _dev_handle;
        transfer->context = ctx;
        transfer->callback = [](usb_transfer_t *t) {
            AsyncDiagContext *c = (AsyncDiagContext*)t->context;
            if (t->status == USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes > sizeof(usb_setup_packet_t)) {
                c->len = t->actual_num_bytes - sizeof(usb_setup_packet_t);
                if (c->len > c->capacity) c->len = c->capacity;
                if (c->len > 0) memcpy(c->data, t->data_buffer + sizeof(usb_setup_packet_t), c->len);
            }
            int expected = 0;
            if (!c->state.compare_exchange_strong(expected, 1)) {
                delete c;
                usb_host_transfer_free(t);
            }
        };
        transfer->num_bytes = 8 + report_len;
        transfer->timeout_ms = 5000;

        err = usb_host_transfer_submit_control(_client_handle, transfer);
        if (err == ESP_OK) {
            uint32_t start = millis();
            while (ctx->state == 0 && (millis() - start < 6000)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            int expected = 0;
            if (ctx->state.compare_exchange_strong(expected, 2)) {
                doc["error"] = "Report Descriptor timeout";
                delete ctx_cfg; // Clean up config context before returning
                String out;
                serializeJson(doc, out);
                return out; // Transfer is leaked safely to the callback
            }
            
            if (ctx->state == 1 && ctx->len > 0) {
                JsonArray raw = doc["report_descriptor_hex"].to<JsonArray>();
                for (size_t i = 0; i < ctx->len; i++) {
                    char hexb[5];
                    sprintf(hexb, "0x%02X", ctx->data[i]);
                    raw.add(hexb);
                }
            } else if (_cached_report_descriptor_hex.length() > 0) {
                DynamicJsonDocument temp_doc(4096);
                deserializeJson(temp_doc, _cached_report_descriptor_hex);
                doc["report_descriptor_hex"] = temp_doc.as<JsonArray>();
                doc["error"] = "Report Descriptor live fetch timeout, using cached descriptor";
            } else {
                doc["error"] = "Report Descriptor empty or failed";
            }
        } else {
            doc["error"] = "Failed to submit Report Descriptor transfer";
        }
        
        // Clean up memory only if not leaked to callback
        delete ctx;
        delete ctx_cfg;
        usb_host_transfer_free(transfer);
    } else {
        doc["error"] = "Failed to allocate transfer";
    }

    String out;
    serializeJson(doc, out);
    return out;
}


void USBHostUPS::int_in_cb(usb_transfer_t *transfer) {
    USBHostUPS* self = (USBHostUPS*)transfer->context;
    if (self) self->handle_int_in(transfer);
}

void USBHostUPS::handle_int_in(usb_transfer_t *transfer) {
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0 && _driver) {
        bool has_report_ids = false;
        for (const auto& u : _hid_parser.getUsages()) {
            if (u.report_id != 0) { has_report_ids = true; break; }
        }
        uint8_t report_id = has_report_ids ? transfer->data_buffer[0] : 0;
        _driver->decodeReport(this, report_id, 1, transfer->data_buffer, transfer->actual_num_bytes, _ups_data);
    }
    
    // Resubmit transfer if device is still active
    if (!_pending_dev_close && _dev_handle != NULL) {
        transfer->num_bytes = _int_in_mps;
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
            // Do not permanently free/destroy the transfer on transient errors unless device is disconnecting
            if (err == ESP_ERR_NOT_FOUND || _pending_dev_close) {
                Serial.printf("[USBHostUPS] INT IN stopped: %d\n", err);
                usb_host_transfer_free(transfer);
                _int_in_transfer = NULL;
            } else {
                // For other transient errors, retry submission
                esp_err_t retry_err = usb_host_transfer_submit(transfer);
                if (retry_err != ESP_OK) {
                    Serial.printf("[USBHostUPS] INT IN submit failed: %d\n", retry_err);
                    usb_host_transfer_free(transfer);
                    _int_in_transfer = NULL;
                }
            }
        }
    } else {
        usb_host_transfer_free(transfer);
        _int_in_transfer = NULL;
    }
}
