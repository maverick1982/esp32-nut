#include "USBHostUPS.h"

USBHostUPS::USBHostUPS() : 
    _usb_task_handle(NULL), 
    _client_task_handle(NULL), 
    _client_handle(NULL), 
    _dev_handle(NULL), 
    _initialized(false),
    _last_poll(0),
    _chemStrIdx(0),
    _poll_step(0),
    _last_step_time(0) {
}

USBHostUPS::~USBHostUPS() {
}

const UPSData& USBHostUPS::getUPSData() const {
    return _ups_data;
}

String USBHostUPS::getUPSStatusString() const {
    String status = "";
    if (_ups_data.acPresent && !_ups_data.discharging) status += "OL ";
    if (_ups_data.discharging) status += "OB ";
    if (_ups_data.belowRemainingCapacityLimit) status += "LB ";
    if (_ups_data.charging) status += "CHRG ";
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

void USBHostUPS::usb_host_lib_task(void *arg) {
    USBHostUPS *self = static_cast<USBHostUPS*>(arg);
    (void)self;

    Serial.println("[USBHostUPS] Task gestione eventi USB Host avviato.");
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
    Serial.println("[USBHostUPS] Task gestione eventi Client USB Host avviato.");
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
            Serial.printf("[USBHostUPS] Nuovo dispositivo USB connesso. Indirizzo: %d\n", event_msg->new_dev.address);
            usb_device_handle_t dev_hdl;
            esp_err_t err = usb_host_device_open(_client_handle, event_msg->new_dev.address, &dev_hdl);
            if (err == ESP_OK) {
                const usb_device_desc_t *desc;
                err = usb_host_get_device_descriptor(dev_hdl, &desc);
                if (err == ESP_OK) {
                    Serial.printf("[USBHostUPS] Info Dispositivo: Address %d, VID %04X, PID %04X\n",
                                  event_msg->new_dev.address, desc->idVendor, desc->idProduct);
                    if (desc->idVendor == 0x0463 && desc->idProduct == 0xFFFF) {
                        Serial.println("[USBHostUPS] UPS Eaton 3S 700 rilevato correttamente! Registrato come HID.");
                        _dev_handle = dev_hdl;
                        esp_err_t claim_err = usb_host_interface_claim(_client_handle, _dev_handle, 0, 0);
                        if (claim_err == ESP_OK) {
                            Serial.println("[USBHostUPS] Interfaccia 0 HID reclamata con successo.");
                            _last_poll = 0;
                        } else {
                            Serial.printf("[USBHostUPS] Errore claim interfaccia 0: %d\n", claim_err);
                            usb_host_device_close(_client_handle, dev_hdl);
                            _dev_handle = NULL;
                        }
                    } else {
                        Serial.println("[USBHostUPS] Dispositivo non compatibile.");
                        usb_host_device_close(_client_handle, dev_hdl);
                    }
                } else {
                    Serial.printf("[USBHostUPS] Errore lettura descrittore: %d\n", err);
                    usb_host_device_close(_client_handle, dev_hdl);
                }
            } else {
                Serial.printf("[USBHostUPS] Errore apertura dispositivo: %d\n", err);
            }
            break;
        }
        case USB_HOST_CLIENT_EVENT_DEV_GONE: {
            Serial.println("[USBHostUPS] Dispositivo USB rimosso.");
            _ups_data = UPSData(); // reset
            if (_dev_handle != NULL && event_msg->dev_gone.dev_hdl == _dev_handle) {
                esp_err_t release_err = usb_host_interface_release(_client_handle, _dev_handle, 0);
                if (release_err == ESP_OK) {
                    Serial.println("[USBHostUPS] Interfaccia 0 HID rilasciata con successo.");
                } else {
                    Serial.printf("[USBHostUPS] Errore rilascio interfaccia 0: %d\n", release_err);
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
        Serial.printf("[USBHostUPS] Errore: usb_host_install fallito: %d\n", err);
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
        Serial.println("[USBHostUPS] Errore: Impossibile creare il task FreeRTOS per eventi di libreria");
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
        Serial.printf("[USBHostUPS] Errore: usb_host_client_register fallito: %d\n", err);
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
        Serial.println("[USBHostUPS] Errore: Impossibile creare il task FreeRTOS per eventi client");
        return false;
    }

    _initialized = true;
    Serial.println("[USBHostUPS] Inizializzazione USB Host e Client completata.");
    return true;
}

void USBHostUPS::loop() {
    if (!_initialized || _dev_handle == NULL) {
        return;
    }

    uint32_t now = millis();
    if (_poll_step == 0) {
        if (now - _last_poll >= 5000 || _last_poll == 0) {
            _last_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
        }
    }

    if (_poll_step > 0) {
        if (now - _last_step_time >= 50 || _poll_step == 1) { // Execute first step immediately
            _last_step_time = now;
            switch (_poll_step) {
                case 1: if (_ups_data.manufacturer == "") requestStringDescriptor(1); break;
                case 2: if (_ups_data.product == "") requestStringDescriptor(2); break;
                case 3: if (_ups_data.serialNumber == "") requestStringDescriptor(4); break;
                case 4: requestReport(0x01, 0x03, 4); break;
                case 5: requestReport(0x02, 0x03, 3); break;
                case 6: requestReport(0x06, 0x03, 6); break;
                case 7: requestReport(0x07, 0x03, 8); break;
                case 8: requestReport(0x08, 0x03, 2); break;
                case 9: requestReport(0x09, 0x03, 5); break;
                case 10: requestReport(0x0a, 0x03, 5); break;
                case 11: requestReport(0x0c, 0x03, 8); break;
                case 12: requestReport(0x0d, 0x03, 4); break;
                case 13: requestReport(0x0e, 0x03, 3); break;
                case 14: requestReport(0x10, 0x03, 9); break;
                case 15: requestReport(0x12, 0x03, 2); break;
                case 16: requestReport(0x13, 0x03, 3); break;
                case 17: requestReport(0x14, 0x03, 2); break;
                case 18: requestReport(0x1f, 0x03, 2); break;
                default: 
                    _poll_step = 0; 
                    return;
            }
            _poll_step++;
        }
    }
}
void USBHostUPS::decodeReport(uint8_t report_id, const uint8_t *data, size_t length) {
    if (length == 0 || data == NULL) return;

    size_t offset = 0; // The ESP-IDF driver may or may not strip the Report ID
    if (length > 1 && data[0] == report_id) {
        offset = 1;
    }

    switch (report_id) {
        case 0x01:
            if (length - offset >= 1) {
                _ups_data.acPresent = data[offset] & (1 << 0);
                _ups_data.belowRemainingCapacityLimit = data[offset] & (1 << 1);
                _ups_data.charging = data[offset] & (1 << 2);
                _ups_data.communicationLost = data[offset] & (1 << 3);
                _ups_data.discharging = data[offset] & (1 << 4);
                _ups_data.good = data[offset] & (1 << 5);
                _ups_data.internalFailure = data[offset] & (1 << 6);
                _ups_data.needReplacement = data[offset] & (1 << 7);
            }
            if (length - offset >= 2) {
                _ups_data.overload = data[offset + 1] != 0;
            }
            if (length - offset >= 3) {
                _ups_data.shutdownImminent = data[offset + 2] != 0;
            }
            break;

        case 0x02:
            if (length - offset >= 2) {
                _ups_data.outlet1Switch = data[offset] != 0;
                _ups_data.outlet2Switch = data[offset + 1] != 0;
            }
            break;

        case 0x06:
            if (length - offset >= 1) {
                _ups_data.remainingCapacity = data[offset];
                if (_ups_data.remainingCapacity > 100) _ups_data.remainingCapacity = 100;
            }
            if (length - offset >= 5) {
                _ups_data.runTimeToEmpty = (uint32_t)data[offset + 1] |
                                           ((uint32_t)data[offset + 2] << 8) |
                                           ((uint32_t)data[offset + 3] << 16) |
                                           ((uint32_t)data[offset + 4] << 24);
            }
            break;

        case 0x07:
            if (length - offset >= 6) {
                _ups_data.load = data[offset + 5];
                if (_ups_data.configApparentPower > 0) {
                    _ups_data.realPower = (uint16_t)(((uint32_t)_ups_data.configApparentPower * 60 * _ups_data.load) / 10000);
                }
            }
            break;

        case 0x08:
            if (length - offset >= 1) {
                _ups_data.remainingCapacityLimit = data[offset];
            }
            break;

        case 0x09:
            if (length - offset >= 4) {
                _ups_data.delayShutdown = (int32_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24));
            }
            break;

        case 0x0a:
            if (length - offset >= 4) {
                _ups_data.delayStart = (int32_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24));
            }
            break;

        case 0x0c:
            if (length - offset >= 6) {
                _ups_data.designCapacity = data[offset + 4];
                _ups_data.fullChargeCapacity = data[offset + 5];
            }
            break;

        case 0x0d:
            if (length - offset >= 2) {
                _ups_data.configApparentPower = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            if (length - offset >= 3) {
                _ups_data.configFrequency = data[offset + 2];
            }
            break;

        case 0x0e:
            if (length - offset >= 2) {
                _ups_data.outputVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x10:
            if (length - offset >= 1) {
                uint8_t chemIdx = data[offset];
                if (chemIdx > 0 && chemIdx != _chemStrIdx) {
                    _chemStrIdx = chemIdx;
                    requestStringDescriptor(chemIdx);
                }
            }
            break;

        case 0x12:
            if (length - offset >= 1) {
                _ups_data.configVoltage = data[offset];
            }
            break;

        case 0x13:
            if (length - offset >= 2) {
                _ups_data.highVoltageTransfer = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x14:
            if (length - offset >= 1) {
                _ups_data.lowVoltageTransfer = data[offset];
            }
            break;

        case 0x1f:
            if (length - offset >= 1) {
                _ups_data.beeperEnabled = (data[offset] == 2);
            }
            break;

        default:
            break;
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
                    self->parseStringDescriptor(setup->wValue & 0xFF, data, actual_length);
                }
            } else if (setup->bmRequestType == 0xA1 && setup->bRequest == 0x01) {
                uint8_t report_id = setup->wValue & 0xFF;
                self->decodeReport(report_id, data, actual_length);
            }
        }
    }
    usb_host_transfer_free(transfer);
}

void USBHostUPS::parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) { // Safety check to skip null characters if they happen to appear in the low byte, though UTF-16LE has ascii in low byte.
            str += (char)data[i];
        }
    }
    if (index == 1) _ups_data.manufacturer = str;
    else if (index == 2) _ups_data.product = str;
    else if (index == 4) _ups_data.serialNumber = str;
    else if (_chemStrIdx > 0 && index == _chemStrIdx) _ups_data.batteryType = str;
}
