#ifndef USB_SERIAL_LINK_H
#define USB_SERIAL_LINK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "DriverRegistry.h"
#include "UsbSerialDevice.h"

/**
 * @brief The serial device the serial path has claimed (ADR 0014, sections 2 and 5).
 *
 * Pure state, like EnumWatchdog: UsbSerialHost feeds it from the poll task (attach, open
 * result, driver events, received bytes) and reads it, under its lock, for the System Log,
 * the "Not supported yet" banner and the "serial" object of the USB diagnostics export.
 * There is no protocol yet, so there is no supervision: no state here asks for a port cycle
 * or a restart.
 */
class UsbSerialLink {
public:
    enum class State : uint8_t {
        NONE,        // no serial device claimed
        OPEN,        // claimed and opened through usb_host_cdc_acm
        OPEN_FAILED  // claimed, cdc_acm_host_open_v2() failed (the device stays reported)
    };
    static const size_t TEXT_LEN = 47;

    // A new claimed device replaces the one before (one UPS port, as on the HID path)
    void attach(const UsbSerialDeviceInfo& info, const DriverRegistry::SerialMatch& match, uint32_t now) {
        *this = UsbSerialLink();
        _info = info;
        _adapter = match.adapter ? match.adapter : "";
        _chip = match.chip ? match.chip : "";
        _interface = match.interface_number;
        _attached_at = now;
        _state = State::OPEN_FAILED;
        copy(_open_result, "not opened");
    }
    void opened() {
        if (_state == State::NONE) return;
        _state = State::OPEN;
        copy(_open_result, "ESP_OK");
    }
    void openFailed(const char* error) {
        if (_state == State::NONE) return;
        _state = State::OPEN_FAILED;
        copy(_open_result, error);
        copy(_last_error, error);
    }
    // The device at this address is gone. True when it was the claimed one (now released)
    bool detach(uint8_t address) {
        if (_state == State::NONE || address != _info.address) return false;
        *this = UsbSerialLink();
        return true;
    }
    // Received bytes, folded in by the poll task from the counters of the data callback
    void noteReceived(uint32_t bytes, uint32_t chunks) {
        if (_state != State::OPEN) return;
        _rx_bytes += bytes;
        _rx_chunks += chunks;
    }
    // Driver event of the opened device ("error 3", "serial state 0x0003")
    void noteEvent(const char* text) {
        if (_state != State::NONE) copy(_last_event, text);
    }
    void noteError(const char* text) {
        if (_state != State::NONE) copy(_last_error, text);
    }

    bool present() const { return _state != State::NONE; }
    State state() const { return _state; }
    const char* stateName() const {
        switch (_state) {
            case State::OPEN:        return "open";
            case State::OPEN_FAILED: return "open_failed";
            default:                 return "none";
        }
    }
    const UsbSerialDeviceInfo& info() const { return _info; }
    const char* adapter() const { return _adapter; }
    const char* chip() const { return _chip; }
    uint8_t interfaceNumber() const { return _interface; }
    uint32_t attachedAt() const { return _attached_at; }
    const char* openResult() const { return _open_result; }
    const char* lastEvent() const { return _last_event; }
    const char* lastError() const { return _last_error; }
    uint32_t rxBytes() const { return _rx_bytes; }
    uint32_t rxChunks() const { return _rx_chunks; }
    // Nothing is sent in the first delivery (ADR 0014, section 4)
    uint32_t txBytes() const { return 0; }

    /**
     * @brief Text of IUpsSource::unsupportedDevice() (US-132 pattern, ADR 0014 section 5):
     * "USB serial: XR21V141x (04E2:1410)"; empty with no serial device.
     */
    void unsupportedText(char* out, size_t out_size) const {
        if (!out || out_size == 0) return;
        out[0] = '\0';
        if (_state == State::NONE) return;
        snprintf(out, out_size, "USB serial: %s (%04X:%04X)", _chip[0] ? _chip : "unknown bridge",
                 (unsigned)_info.vid, (unsigned)_info.pid);
    }

private:
    static void copy(char (&dst)[TEXT_LEN + 1], const char* src) {
        snprintf(dst, sizeof(dst), "%s", src ? src : "");
    }

    State _state = State::NONE;
    UsbSerialDeviceInfo _info;
    const char* _adapter = "";  // static strings of the registry rows
    const char* _chip = "";
    uint8_t _interface = 0;
    uint32_t _attached_at = 0;
    char _open_result[TEXT_LEN + 1] = {0};
    char _last_event[TEXT_LEN + 1] = {0};
    char _last_error[TEXT_LEN + 1] = {0};
    uint32_t _rx_bytes = 0;
    uint32_t _rx_chunks = 0;
};

#endif // USB_SERIAL_LINK_H
