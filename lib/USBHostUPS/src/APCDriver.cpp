#include "APCDriver.h"
#include "USBHostUPS.h"

APCDriver::APCDriver() : 
    _last_poll(0),
    _last_fast_poll(0),
    _last_step_time(0),
    _poll_step(0),
    _slow_poll_counter(0) {
}

void APCDriver::setup() {
    _last_poll = 0;
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _slow_poll_counter = 0;
    Serial.println("[APCDriver] Setup started.");
}

void APCDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
            
            _slow_poll_counter++;
            if (_slow_poll_counter >= 15) { // 30s / 2s = 15
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (now - _last_step_time >= 50 || _poll_step == 1) {
            _last_step_time = now;
            switch (_poll_step) {
                // Fast poll
                case 1: host->requestReport(0x06, 0x03, 4); break; // Fallback status
                case 2: host->requestReport(0x16, 0x03, 3); break; // PresentStatus
                case 3: host->requestReport(0x0c, 0x03, 8); break; // RemainingCapacity e RunTimeToEmpty
                case 4: host->requestReport(0x31, 0x03, 3); break; // Input Voltage
                case 5: host->requestReport(0x09, 0x03, 3); break; // Battery Voltage
                case 6: host->requestReport(0x50, 0x03, 2); break; // PercentLoad

                // Slow poll
                case 7: if (_slow_poll_counter == 0 && data.manufacturer == "") host->requestStringDescriptor(1); break;
                case 8: if (_slow_poll_counter == 0 && data.product == "") host->requestStringDescriptor(2); break;
                case 9: if (_slow_poll_counter == 0 && data.serialNumber == "") host->requestStringDescriptor(3); break;
                case 10: if (_slow_poll_counter == 0) host->requestReport(0x08, 0x03, 3); break; // Config Voltage
                case 11: if (_slow_poll_counter == 0) host->requestReport(0x0d, 0x03, 2); break; // Design Capacity
                case 12: if (_slow_poll_counter == 0) host->requestReport(0x0e, 0x03, 2); break; // Full Charge Capacity
                case 13: if (_slow_poll_counter == 0) host->requestReport(0x11, 0x03, 2); break; // Remaining Capacity Limit
                case 14: if (_slow_poll_counter == 0) host->requestReport(0x18, 0x03, 2); break; // Audible Alarm Control
                case 15: if (_slow_poll_counter == 0) host->requestReport(0x42, 0x03, 3); break; // Delay Before Shutdown
                case 16: if (_slow_poll_counter == 0) host->requestReport(0x40, 0x03, 2); break; // Delay Before Reboot
                case 17: if (_slow_poll_counter == 0) host->requestReport(0x32, 0x03, 3); break; // Low Voltage Transfer
                case 18: if (_slow_poll_counter == 0) host->requestReport(0x33, 0x03, 3); break; // High Voltage Transfer
                case 19: if (_slow_poll_counter == 0) host->requestReport(0x52, 0x03, 3); break; // Config Active Power
                default: 
                    _poll_step = 0; 
                    return;
            }
            _poll_step++;
        }
    }
}

void APCDriver::decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL) return;

    size_t offset = 0;
    if (length > 1 && data[0] == report_id) {
        offset = 1;
    }

    switch (report_id) {
        case 0x06: // Fallback status
            if (length - offset >= 1) ups_data.charging = data[offset] != 0;
            if (length - offset >= 2) ups_data.discharging = data[offset + 1] != 0;
            break;

        case 0x16: // PresentStatus (Bitfield)
            if (length - offset >= 1) {
                ups_data.charging = data[offset] & (1 << 0);
                ups_data.discharging = data[offset] & (1 << 1);
                ups_data.acPresent = data[offset] & (1 << 2);
                ups_data.belowRemainingCapacityLimit = data[offset] & (1 << 4);
                ups_data.shutdownImminent = data[offset] & (1 << 5);
                ups_data.communicationLost = data[offset] & (1 << 7);
            }
            if (length - offset >= 2) {
                ups_data.needReplacement = data[offset + 1] & (1 << 0);
                ups_data.overload = data[offset + 1] & (1 << 1);
            }
            break;

        case 0x0C: // Battery Capacity & Runtime
            if (length - offset >= 1) {
                ups_data.remainingCapacity = data[offset];
                if (ups_data.remainingCapacity > 100) ups_data.remainingCapacity = 100;
            }
            if (length - offset >= 3) {
                // RunTimeToEmpty is at offset 1 (byte 1), size 16 bits
                ups_data.runTimeToEmpty = (uint32_t)data[offset + 1] | ((uint32_t)data[offset + 2] << 8);
            }
            break;

        case 0x31: // Input Voltage
            if (length - offset >= 2) {
                ups_data.inputVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x50: // PercentLoad
            if (length - offset >= 1) {
                ups_data.load = data[offset];
            }
            break;

        case 0x08: // Config Voltage
            if (length - offset >= 2) {
                ups_data.configVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x0D: // Design Capacity
            if (length - offset >= 1) {
                ups_data.designCapacity = data[offset];
            }
            break;

        case 0x0E: // Full Charge Capacity
            if (length - offset >= 1) {
                ups_data.fullChargeCapacity = data[offset];
            }
            break;

        case 0x11: // Remaining Capacity Limit
            if (length - offset >= 1) {
                ups_data.remainingCapacityLimit = data[offset];
            }
            break;

        case 0x18: // Audible Alarm Control
            if (length - offset >= 1) {
                ups_data.beeperEnabled = (data[offset] != 0);
            }
            break;

        case 0x42: // Delay Before Shutdown
            if (length - offset >= 2) {
                ups_data.delayShutdown = (int16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8));
            }
            break;

        case 0x40: // Delay Before Reboot (start)
            if (length - offset >= 1) {
                ups_data.delayStart = data[offset];
            }
            break;

        case 0x09: // Battery Voltage
            if (length - offset >= 2) {
                ups_data.batteryVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x32: // Low Voltage Transfer
            if (length - offset >= 2) {
                ups_data.lowVoltageTransfer = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x33: // High Voltage Transfer
            if (length - offset >= 2) {
                ups_data.highVoltageTransfer = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x52: // Config Active Power
            if (length - offset >= 2) {
                ups_data.realPower = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        default:
            break;
    }
}

void APCDriver::parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) {
            str += (char)data[i];
        }
    }
    if (index == 1) ups_data.manufacturer = str;
    else if (index == 2) ups_data.product = str;
    else if (index == 3 || index == 4) ups_data.serialNumber = str;
}
