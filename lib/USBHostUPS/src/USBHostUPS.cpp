#include "USBHostUPS.h"

USBHostUPS::USBHostUPS() : 
    _usb_task_handle(NULL), 
    _client_task_handle(NULL), 
    _client_handle(NULL), 
    _dev_handle(NULL), 
    _initialized(false),
    _battery_charge(0),
    _ups_status("Unknown"),
    _input_voltage(0.0f) {
}

USBHostUPS::~USBHostUPS() {
}

uint8_t USBHostUPS::getBatteryCharge() const {
    return _battery_charge;
}

String USBHostUPS::getUPSStatus() const {
    return _ups_status;
}

float USBHostUPS::getInputVoltage() const {
    return _input_voltage;
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

    // Registrazione del client
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

    // Creazione del task FreeRTOS per gestire gli eventi del client
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
    static uint32_t last_poll = 0;
    if (now - last_poll >= 5000) {
        last_poll = now;
        Serial.println("[USBHostUPS] Esecuzione polling dei report dell'UPS...");
        // Invia richieste per i report 0x01 (carica), 0x02 (stato), 0x03 (tensione)
        requestReport(0x01, 0x03); // Feature Report
        requestReport(0x02, 0x03); // Feature Report
        requestReport(0x03, 0x03); // Feature Report
    }
}

void USBHostUPS::decodeReport(uint8_t report_id, const uint8_t *data, size_t length) {
    if (length == 0 || data == NULL) return;

    size_t offset = 0;
    // Se il primo byte coincide con il report ID (tipico per dispositivi HID multi-report),
    // avanziamo l'indice dei dati utili di 1.
    if (data[0] == report_id && length > 1) {
        offset = 1;
    }

    if (report_id == 0x01) {
        _battery_charge = data[offset];
        if (_battery_charge > 100) _battery_charge = 100;
        Serial.printf("[USBHostUPS] Carica batteria aggiornata: %d%%\n", _battery_charge);
    } else if (report_id == 0x02) {
        uint8_t status_val = data[offset];
        if (status_val == 1) {
            _ups_status = "OL";
        } else if (status_val == 2) {
            _ups_status = "OB";
        } else {
            _ups_status = "Unknown";
        }
        Serial.printf("[USBHostUPS] Stato UPS aggiornato: %s\n", _ups_status.c_str());
    } else if (report_id == 0x03) {
        if (length - offset >= 2) {
            uint16_t raw_val = data[offset] | (data[offset + 1] << 8);
            _input_voltage = raw_val / 10.0f;
            Serial.printf("[USBHostUPS] Tensione ingresso aggiornata: %.1f V\n", _input_voltage);
        }
    }
}

bool USBHostUPS::requestReport(uint8_t report_id, uint8_t report_type) {
    if (_dev_handle == NULL) return false;

    usb_transfer_t *transfer = NULL;
    // Buffer per contenere setup packet (8 byte) + payload dati (8 byte)
    esp_err_t err = usb_host_transfer_alloc(8 + 8, 0, &transfer);
    if (err != ESP_OK) {
        Serial.printf("[USBHostUPS] Allocazione transfer HID fallita: %d\n", err);
        return false;
    }

    usb_setup_packet_t *setup = (usb_setup_packet_t *)transfer->data_buffer;
    setup->bmRequestType = 0xA1; // Device-to-Host | Class | Interface
    setup->bRequest = 0x01;      // GET_REPORT
    setup->wValue = (report_type << 8) | report_id;
    setup->wIndex = 0;           // Interfaccia 0
    setup->wLength = 8;

    transfer->device_handle = _dev_handle;
    transfer->callback = control_transfer_cb;
    transfer->context = this;
    transfer->num_bytes = 8 + 8;

    err = usb_host_transfer_submit_control(_client_handle, transfer);
    if (err != ESP_OK) {
        Serial.printf("[USBHostUPS] Invio transfer fallito: %d\n", err);
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
            uint8_t report_id = setup->wValue & 0xFF;
            uint8_t *data = transfer->data_buffer + sizeof(usb_setup_packet_t);
            size_t actual_length = transfer->actual_num_bytes - sizeof(usb_setup_packet_t);
            self->decodeReport(report_id, data, actual_length);
        } else {
            Serial.printf("[USBHostUPS] Control transfer non completato correttamente. Status: %d\n", transfer->status);
        }
    }
    usb_host_transfer_free(transfer);
}
