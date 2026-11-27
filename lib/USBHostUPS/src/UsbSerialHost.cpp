#include "UsbSerialHost.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "usb/cdc_acm_host_ops.h"
#include "XrUartSetup.h"
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
    UsbSerialHost* self = static_cast<UsbSerialHost*>(user_ctx);
    if (self) {
        // Into the buffer the protocol reads in the poll task; never waits (ADR 0008)
        self->_rx.push(data, len);
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
    const uint32_t sent = _tx_bytes.exchange(0);
    if (bytes || chunks || sent) {
        std::lock_guard<std::mutex> lock(_mutex);
        _link.noteReceived(bytes, chunks);
        _link.noteSent(sent);
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

    // The IN transfer has the max packet size of the IN endpoint; the OUT one carries the
    // commands of the protocol (ADR 0014, section 6)
    cdc_acm_host_open_config_t cfg = {};
    cfg.vid = info.vid;
    cfg.pid = info.pid;
    cfg.interface_idx = m.interface_number;
    cfg.dev_addr = info.address;
    cfg.connection_timeout_ms = OPEN_TIMEOUT_MS;
    cfg.out_buffer_size = OUT_BUFFER_LEN;
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
    _rx.clear();
    _gone.store(false);
    _port_device = info;
    _port_gen++;
    _rx_bytes.store(0);
    _rx_chunks.store(0);
    _tx_bytes.store(0);
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
    log("INFO", "[USB] Serial device opened (interface %u), endpoints %s", (unsigned)m.interface_number,
        eps[0] ? eps : "none");
}

void UsbSerialHost::handleDeviceEvent(const cdc_acm_host_dev_event_data_t& data) {
    switch (data.type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED: {
        if (!_cdc || data.data.cdc_hdl != _cdc) return;
        _gone.store(true);
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
    _port_gen++;  // the host releases the protocol of this port
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

// --- ISerialPort (poll task) ---------------------------------------------------------------

void UsbSerialHost::Port::setError(const char* what, int err) {
    snprintf(_error, sizeof(_error), "%s: %s", what, esp_err_to_name((esp_err_t)err));
    char text[UsbSerialLink::TEXT_LEN + 1];
    snprintf(text, sizeof(text), "%s", _error);
    std::lock_guard<std::mutex> lock(_h._mutex);
    _h._link.noteError(text);
}

bool UsbSerialHost::Port::configure(const SerialLineSettings& s) {
    if (!_h._cdc) {
        snprintf(_error, sizeof(_error), "no device");
        return false;
    }
    // CDC-ACM SET_LINE_CODING: the XR21V141x takes it too (datasheet section 1.2, table 4)
    cdc_acm_line_coding_t coding = {};
    coding.dwDTERate = s.baud;
    coding.bCharFormat = s.stop_bits == 2 ? 2 : 0;
    coding.bParityType = s.parity == 'O' ? 1 : (s.parity == 'E' ? 2 : 0);
    coding.bDataBits = s.data_bits;
    esp_err_t err = cdc_acm_host_line_coding_set(_h._cdc, &coding);
    if (err != ESP_OK) {
        setError("line coding", err);
        return false;
    }
    const char* adapter;
    {
        std::lock_guard<std::mutex> lock(_h._mutex);
        adapter = _h._link.adapter();
    }
    const bool xr = strcmp(adapter, "xr21v141x") == 0;
#if defined(NUT_TEST_XR_LOOPBACK) && NUT_TEST_XR_LOOPBACK
    if (xr) logXrRegisters();  // test build: after SET_LINE_CODING
#endif
    err = cdc_acm_host_set_control_line_state(_h._cdc, s.dtr, s.rts);
    if (err != ESP_OK) {
        setError("control lines", err);
        return false;
    }
#if defined(NUT_TEST_XR_LOOPBACK) && NUT_TEST_XR_LOOPBACK
    if (xr) logXrRegisters();  // test build: after SET_CONTROL_LINE_STATE
#endif
    // XR21V141x: off with the RTS/CTS flow control it starts with under CDC-ACM, as a NUT serial
    // port has none (XrUartSetup.h). Last: on the unit, a CDC-ACM request sent after the
    // register writes brought the flow control back (FLOW_CONTROL 0x01 read back)
    if (xr) {
        for (size_t i = 0; i < XrUart::NO_FLOW_CONTROL_COUNT; i++) {
            const XrUart::RegWrite& w = XrUart::NO_FLOW_CONTROL[i];
            err = cdc_acm_host_send_custom_request(_h._cdc, XrUart::SET_REG_REQUEST_TYPE, XrUart::SET_REG_REQUEST,
                                                   w.wValue(), w.wIndex(), 0, nullptr);
            if (err != ESP_OK) {
                setError("XR register", err);
                return false;
            }
        }
    }
    _h.log("INFO", "[USB] Serial line %u %u%c%u, DTR %s, RTS %s%s", (unsigned)s.baud, (unsigned)s.data_bits,
           s.parity, (unsigned)s.stop_bits, s.dtr ? "on" : "off", s.rts ? "on" : "off",
           xr ? ", flow control off" : "");
    if (xr) {
        logXrRegisters();
#if defined(NUT_TEST_XR_LOOPBACK) && NUT_TEST_XR_LOOPBACK
        xrLoopbackTest();
#endif
    }
    return true;
}

bool UsbSerialHost::Port::readXrRegister(uint16_t wIndex, uint8_t& value) {
    uint8_t buf[1] = {0};
    const esp_err_t err = cdc_acm_host_send_custom_request(_h._cdc, XrUart::GET_REG_REQUEST_TYPE,
                                                           XrUart::GET_REG_REQUEST, 0, wIndex, 1, buf);
    value = buf[0];
    return err == ESP_OK;
}

void UsbSerialHost::Port::logXrRegisters() {
    char line[200] = "";
    size_t o = 0;
    for (size_t i = 0; i < XrUart::READBACK_COUNT && o + 32 < sizeof(line); i++) {
        const XrUart::RegRead& r = XrUart::READBACK[i];
        uint8_t v = 0;
        if (readXrRegister(r.wIndex(), v)) {
            o += snprintf(line + o, sizeof(line) - o, "%s%s 0x%02X", i ? ", " : "", r.name, (unsigned)v);
        } else {
            o += snprintf(line + o, sizeof(line) - o, "%s%s ?", i ? ", " : "", r.name);
        }
    }
    _h.log("INFO", "[USB] XR21V141x registers: %s", line);
}

#if defined(NUT_TEST_XR_LOOPBACK) && NUT_TEST_XR_LOOPBACK
void UsbSerialHost::Port::xrLoopbackTest() {
    const uint16_t idx = (uint16_t)(XrUart::REG_LOOPBACK_CTL | (XrUart::BLOCK_UART << 8));
    esp_err_t err = cdc_acm_host_send_custom_request(_h._cdc, XrUart::SET_REG_REQUEST_TYPE, XrUart::SET_REG_REQUEST,
                                                     0x04, idx, 0, nullptr);
    if (err != ESP_OK) {
        _h.log("WARN", "[TEST] XR loopback not enabled: %s", esp_err_to_name(err));
        return;
    }
    discardInput();
    static const uint8_t probe[] = { 'L', 'O', 'O', 'P', '\r' };
    const int w = write(probe, sizeof(probe), 500);
    uint8_t buf[16] = {0};
    size_t got = 0;
    const uint32_t start = millis();
    while (got < sizeof(probe) && millis() - start < 1000) {
        const int n = read(buf + got, sizeof(buf) - got, 1000 - (millis() - start));
        if (n <= 0) break;
        got += (size_t)n;
    }
    cdc_acm_host_send_custom_request(_h._cdc, XrUart::SET_REG_REQUEST_TYPE, XrUart::SET_REG_REQUEST, 0x00, idx, 0,
                                     nullptr);
    _h.log("WARN", "[TEST] XR loopback: wrote %d, read %u byte(s) \"%.*s\": the UART %s", w, (unsigned)got,
           (int)(got > 4 ? 4 : got), (const char*)buf,
           got == sizeof(probe) ? "sends and receives" : "does not send (or receive)");
    logXrRegisters();
}
#endif

int UsbSerialHost::Port::write(const uint8_t* data, size_t len, uint32_t timeoutMs) {
    if (!_h._cdc || _h._gone.load()) {
        snprintf(_error, sizeof(_error), "no device");
        return -1;
    }
    const esp_err_t err = cdc_acm_host_data_tx_blocking(_h._cdc, data, len, timeoutMs);
    if (err == ESP_ERR_TIMEOUT) {
        setError("write", err);
        return 0;
    }
    if (err != ESP_OK) {
        setError("write", err);
        return -1;
    }
    _h._tx_bytes.fetch_add((uint32_t)len);
    return (int)len;
}

int UsbSerialHost::Port::read(uint8_t* buf, size_t len, uint32_t timeoutMs) {
    const uint32_t start = millis();
    for (;;) {
        const size_t n = _h._rx.pop(buf, len);
        if (n > 0) return (int)n;
        if (!_h._cdc || _h._gone.load()) {
            snprintf(_error, sizeof(_error), "device gone");
            return -1;
        }
        if (millis() - start >= timeoutMs) return 0;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

size_t UsbSerialHost::Port::discardInput() {
    return _h._rx.clear();
}

void UsbSerialHost::Port::sleepMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t UsbSerialHost::Port::nowMs() const {
    return millis();
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
