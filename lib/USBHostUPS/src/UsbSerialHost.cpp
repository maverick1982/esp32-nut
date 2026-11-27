#include "UsbSerialHost.h"

#include <stdarg.h>
#include <stdio.h>
#include "esp_err.h"
#include "DriverRegistry.h"
#include "UsbSerialDiag.h"

UsbSerialHost* UsbSerialHost::s_instance = nullptr;

UsbSerialHost::UsbSerialHost() {}

UsbSerialHost::~UsbSerialHost() {
    end();
    if (_queue) vQueueDelete(_queue);
    if (s_instance == this) s_instance = nullptr;
}

bool UsbSerialHost::begin() {
    if (_installed) return true;
    if (!_queue) _queue = xQueueCreate(QUEUE_LEN, sizeof(Event));
    if (!_queue) {
        log("ERROR", "[USB] Serial event queue allocation failed");
        return false;
    }
    s_instance = this;
    // Priority of the hid_host task: both class drivers serve the same USB host. The poll task
    // (3) only drains what the callbacks posted
    const cdc_acm_host_driver_config_t cfg = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 5,
        .xCoreID = tskNO_AFFINITY,
        .new_dev_cb = UsbSerialHost::onNewDevice,
    };
    esp_err_t err = cdc_acm_host_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log("ERROR", "[USB] cdc_acm_host_install failed: %s", esp_err_to_name(err));
        return false;
    }
    _installed = true;
    return true;
}

void UsbSerialHost::end() {
    closeDevice();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _link = UsbSerialLink();
    }
    if (_installed) {
        cdc_acm_host_uninstall();
        _installed = false;
    }
}

// --- Callbacks: usb_host_cdc_acm task, only copy and post (ADR 0008) --------------------

void UsbSerialHost::onNewDevice(usb_device_handle_t dev) {
    UsbSerialHost* self = s_instance;
    if (!self || !self->_queue || !dev) return;
    // Static: the record is large for the driver task stack, and the driver calls this
    // callback from its single task, one device at a time
    static Event ev;
    ev = Event();
    ev.type = Event::NEW_DEVICE;
    UsbSerialDeviceInfo& info = ev.info;

    const usb_device_desc_t* dd = nullptr;
    if (usb_host_get_device_descriptor(dev, &dd) == ESP_OK && dd) {
        info.vid = dd->idVendor;
        info.pid = dd->idProduct;
        info.device_class = dd->bDeviceClass;
    }
    usb_device_info_t di;
    if (usb_host_device_info(dev, &di) == ESP_OK) {
        info.address = di.dev_addr;
        const usb_str_desc_t* strs[3] = {di.str_desc_manufacturer, di.str_desc_product, di.str_desc_serial_num};
        char* outs[3] = {info.manufacturer, info.product, info.serial};
        for (int i = 0; i < 3; i++) {
            if (!strs[i]) continue;
            UsbConfigParser::stringToAscii(reinterpret_cast<const uint8_t*>(strs[i]), strs[i]->bLength, outs[i],
                                           UsbSerialDeviceInfo::STRING_LEN + 1);
        }
    }
    const usb_config_desc_t* cd = nullptr;
    if (usb_host_get_active_config_descriptor(dev, &cd) == ESP_OK && cd) {
        UsbConfigParser::parse(reinterpret_cast<const uint8_t*>(cd), cd->wTotalLength, info);
    }
    if (xQueueSend(self->_queue, &ev, 0) != pdTRUE) self->_dropped.fetch_add(1);
}

void UsbSerialHost::onDeviceEvent(const cdc_acm_host_dev_event_data_t* event, void* user_ctx) {
    UsbSerialHost* self = static_cast<UsbSerialHost*>(user_ctx);
    if (!self || !self->_queue || !event) return;
    static Event ev;  // driver task only, as onNewDevice()
    ev = Event();
    ev.type = Event::DEVICE_EVENT;
    ev.data = *event;
    if (xQueueSend(self->_queue, &ev, 0) != pdTRUE) self->_dropped.fetch_add(1);
}

bool UsbSerialHost::onData(const uint8_t* data, size_t len, void* user_ctx) {
    (void)data;
    UsbSerialHost* self = static_cast<UsbSerialHost*>(user_ctx);
    if (self) {
        // No protocol yet: the bytes are counted and dropped (ADR 0014, section 4)
        self->_rx_bytes.fetch_add((uint32_t)len);
        self->_rx_chunks.fetch_add(1);
    }
    return true;  // flush the RX buffer of the driver
}

// --- Poll task ---------------------------------------------------------------------------

bool UsbSerialHost::service(uint32_t now) {
    if (!_queue) return false;
    bool enumerated = false;
    static Event ev;  // poll task only
    for (UBaseType_t i = 0; i < QUEUE_LEN && xQueueReceive(_queue, &ev, 0) == pdTRUE; i++) {
        if (ev.type == Event::NEW_DEVICE) {
            enumerated = true;
            handleNewDevice(ev.info, now);
        } else {
            handleDeviceEvent(ev.data);
        }
    }

    const uint32_t bytes = _rx_bytes.exchange(0);
    const uint32_t chunks = _rx_chunks.exchange(0);
    if (bytes || chunks) {
        std::lock_guard<std::mutex> lock(_mutex);
        _link.noteReceived(bytes, chunks);
    }

    const uint32_t dropped = _dropped.load();
    if (dropped != _reported_dropped) {
        log("WARN", "[USB] Serial event queue full: %u events dropped so far", (unsigned)dropped);
        _reported_dropped = dropped;
    }

    checkPresence(now);
    return enumerated;
}

void UsbSerialHost::handleNewDevice(const UsbSerialDeviceInfo& info, uint32_t now) {
    const DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(info);
    if (m.claim == DriverRegistry::SerialClaim::HID_DEVICE || m.claim == DriverRegistry::SerialClaim::HUB) return;

    // The interface classes, for the log: "0:02/02/01 1:0A/00/00"
    char classes[96] = "";
    size_t o = 0;
    for (uint8_t i = 0; i < info.interface_count && o + 16 < sizeof(classes); i++) {
        const UsbSerialInterface& it = info.interfaces[i];
        o += snprintf(classes + o, sizeof(classes) - o, "%s%u:%02X/%02X/%02X", i ? " " : "", (unsigned)it.number,
                      (unsigned)it.cls, (unsigned)it.subclass, (unsigned)it.protocol);
    }
    if (info.truncated) {
        log("WARN", "[USB] Configuration descriptor of %04X:%04X longer than the record or malformed: "
                    "interfaces or endpoints missing", (unsigned)info.vid, (unsigned)info.pid);
    }

    if (m.claim == DriverRegistry::SerialClaim::UNSUPPORTED) {
        log("INFO", "[USB] USB device %04X:%04X not supported (%s), interfaces %s", (unsigned)info.vid,
            (unsigned)info.pid, m.reason ? m.reason : "", classes[0] ? classes : "none");
        return;
    }

    // One UPS port: a new serial device replaces the one before
    closeDevice();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _link.attach(info, m, now);
    }
    log("INFO", "[USB] Serial device %04X:%04X recognized: %s (adapter %s), \"%s\" \"%s\", interfaces %s",
        (unsigned)info.vid, (unsigned)info.pid, m.chip, m.adapter, info.manufacturer, info.product,
        classes[0] ? classes : "none");

    // Read only: out_buffer_size 0 allocates no OUT transfer, and nothing is sent. The IN
    // transfer has the max packet size of the IN endpoint
    cdc_acm_host_open_config_t cfg = {};
    cfg.vid = info.vid;
    cfg.pid = info.pid;
    cfg.interface_idx = m.interface_number;
    cfg.dev_addr = info.address;
    cfg.connection_timeout_ms = OPEN_TIMEOUT_MS;
    cfg.out_buffer_size = 0;
    cfg.in_buffer_size = 0;
    cfg.event_cb = UsbSerialHost::onDeviceEvent;
    cfg.data_cb = UsbSerialHost::onData;
    cfg.user_arg = this;
    cdc_acm_dev_hdl_t hdl = nullptr;
    const esp_err_t err = cdc_acm_host_open(&cfg, &hdl);
    if (err != ESP_OK || !hdl) {
        const char* name = err != ESP_OK ? esp_err_to_name(err) : "no handle";
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _link.openFailed(name);
        }
        log("ERROR", "[USB] Serial device %04X:%04X could not be opened (interface %u): %s", (unsigned)info.vid,
            (unsigned)info.pid, (unsigned)m.interface_number, name);
        return;
    }
    _cdc = hdl;
    _rx_bytes.store(0);
    _rx_chunks.store(0);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _link.opened();
    }

    char eps[96] = "";
    o = 0;
    for (uint8_t i = 0; i < info.interface_count; i++) {
        const UsbSerialInterface& it = info.interfaces[i];
        for (uint8_t j = 0; j < it.endpoint_count && o + 24 < sizeof(eps); j++) {
            const UsbSerialEndpoint& ep = it.endpoints[j];
            o += snprintf(eps + o, sizeof(eps) - o, "%s0x%02X %s %s %u", o ? ", " : "", (unsigned)ep.address,
                          UsbSerialDiag::endpointType(ep), ep.isIn() ? "in" : "out", (unsigned)ep.max_packet);
        }
    }
    log("INFO", "[USB] Serial device opened (interface %u), endpoints %s; no protocol yet, nothing is sent",
        (unsigned)m.interface_number, eps[0] ? eps : "none");
}

void UsbSerialHost::handleDeviceEvent(const cdc_acm_host_dev_event_data_t& data) {
    switch (data.type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED: {
        if (!_cdc || data.data.cdc_hdl != _cdc) return;
        closeDevice();
        uint16_t vid = 0, pid = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            vid = _link.info().vid;
            pid = _link.info().pid;
            _link.detach(_link.info().address);
        }
        log("INFO", "[USB] Serial device %04X:%04X disconnected", (unsigned)vid, (unsigned)pid);
        break;
    }
    case CDC_ACM_HOST_ERROR: {
        char text[UsbSerialLink::TEXT_LEN + 1];
        snprintf(text, sizeof(text), "USB host error %d", data.data.error);
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _link.noteError(text);
        }
        log("WARN", "[USB] Serial device: %s", text);
        break;
    }
    case CDC_ACM_HOST_SERIAL_STATE: {
        char text[UsbSerialLink::TEXT_LEN + 1];
        snprintf(text, sizeof(text), "serial state 0x%04X", (unsigned)data.data.serial_state.val);
        std::lock_guard<std::mutex> lock(_mutex);
        _link.noteEvent(text);
        break;
    }
    default:
        break;
    }
}

void UsbSerialHost::closeDevice() {
    if (!_cdc) return;
    cdc_acm_dev_hdl_t hdl = _cdc;
    _cdc = nullptr;
    const esp_err_t err = cdc_acm_host_close(hdl);
    if (err != ESP_OK) log("WARN", "[USB] cdc_acm_host_close failed: %s", esp_err_to_name(err));
}

// A device that could not be opened gets no disconnection event from the driver (only open
// devices do): its address leaving the bus releases it
void UsbSerialHost::checkPresence(uint32_t now) {
    if (now - _presence_at < PRESENCE_CHECK_MS) return;
    _presence_at = now;
    uint8_t address = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_link.state() != UsbSerialLink::State::OPEN_FAILED) return;
        address = _link.info().address;
    }
    uint8_t addrs[8];
    int num = 0;
    if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &num) != ESP_OK) return;
    for (int i = 0; i < num; i++) {
        if (addrs[i] == address) return;
    }
    uint16_t vid = 0, pid = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        vid = _link.info().vid;
        pid = _link.info().pid;
        _link.detach(address);
    }
    log("INFO", "[USB] Serial device %04X:%04X disconnected", (unsigned)vid, (unsigned)pid);
}

// --- Any task ----------------------------------------------------------------------------

void UsbSerialHost::unsupportedText(char* out, size_t out_size) const {
    std::lock_guard<std::mutex> lock(_mutex);
    _link.unsupportedText(out, out_size);
}

void UsbSerialHost::fillDiagnostics(UsbDiagSerial& d) const {
    std::lock_guard<std::mutex> lock(_mutex);
    UsbSerialDiag::fill(_link, d);
}

void UsbSerialHost::log(const char* level, const char* fmt, ...) const {
    if (!_log) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log(_log_ctx, level, buf);
}
