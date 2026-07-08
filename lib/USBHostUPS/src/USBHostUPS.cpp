#include "USBHostUPS.h"

USBHostUPS::USBHostUPS() : 
    _usb_task_handle(NULL), 
    _client_task_handle(NULL), 
    _client_handle(NULL), 
    _dev_handle(NULL), 
    _initialized(false) {
}

USBHostUPS::~USBHostUPS() {
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
}
