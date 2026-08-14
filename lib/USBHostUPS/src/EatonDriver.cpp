#include "EatonDriver.h"
#include "USBHostUPS.h"

EatonDriver::EatonDriver() :
    _last_poll(0),
    _chemStrIdx(0),
    _poll_step(0),
    _last_step_time(0),
    _last_fast_poll(0),
    _slow_poll_counter(0) {
}

void EatonDriver::setup() {
    _last_poll = 0;
    _chemStrIdx = 0;
    _poll_step = 0;
    _last_step_time = 0;
    _last_fast_poll = 0;
    _slow_poll_counter = 0;
}

void EatonDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    if (_poll_step == 0) {
        if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
            _last_fast_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;

            _slow_poll_counter++;
            if (_slow_poll_counter >= 15) {
                _slow_poll_counter = 0;
            }
        }
    }

    if (_poll_step > 0) {
        if (now - _last_step_time >= 50 || _poll_step == 1) { // Execute first step immediately
            _last_step_time = now;
            switch (_poll_step) {
                // Fast poll
                case 1: host->requestReport(0x01, 0x03, 4); break;
                case 2: host->requestReport(0x06, 0x03, 6); break;
                case 3: host->requestReport(0x07, 0x03, 8); break;

                // Slow poll
                case 4: if (_slow_poll_counter == 0 && data.manufacturer == "") host->requestStringDescriptor(1); break;
                case 5: if (_slow_poll_counter == 0 && data.product == "") host->requestStringDescriptor(2); break;
                case 6: if (_slow_poll_counter == 0 && data.serialNumber == "") host->requestStringDescriptor(4); break;
                case 7: if (_slow_poll_counter == 0) host->requestReport(0x02, 0x03, 3); break;
                case 8: if (_slow_poll_counter == 0) host->requestReport(0x08, 0x03, 2); break;
                case 9: if (_slow_poll_counter == 0) host->requestReport(0x09, 0x03, 5); break;
                case 10: if (_slow_poll_counter == 0) host->requestReport(0x0a, 0x03, 5); break;
                case 11: if (_slow_poll_counter == 0) host->requestReport(0x0c, 0x03, 8); break;
                case 12: if (_slow_poll_counter == 0) host->requestReport(0x0d, 0x03, 4); break;
                case 13: if (_slow_poll_counter == 0) host->requestReport(0x0e, 0x03, 3); break;
                case 14: if (_slow_poll_counter == 0) host->requestReport(0x10, 0x03, 9); break;
                case 15: if (_slow_poll_counter == 0) host->requestReport(0x12, 0x03, 2); break;
                case 16: if (_slow_poll_counter == 0) host->requestReport(0x13, 0x03, 3); break;
                case 17: if (_slow_poll_counter == 0) host->requestReport(0x14, 0x03, 2); break;
                case 18: if (_slow_poll_counter == 0) host->requestReport(0x1f, 0x03, 2); break;
                default: 
                    _poll_step = 0; 
                    return;
            }
            _poll_step++;
        }
    }
}

void EatonDriver::decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL) return;

    size_t offset = 0; // The ESP-IDF driver may or may not strip the Report ID
    if (length > 1 && data[0] == report_id) {
        offset = 1;
    }

    switch (report_id) {
        case 0x01:
            if (length - offset >= 1) {
                ups_data.acPresent = data[offset] & (1 << 0);
                ups_data.belowRemainingCapacityLimit = data[offset] & (1 << 1);
                ups_data.charging = data[offset] & (1 << 2);
                ups_data.communicationLost = data[offset] & (1 << 3);
                ups_data.discharging = data[offset] & (1 << 4);
                ups_data.good = data[offset] & (1 << 5);
                ups_data.internalFailure = data[offset] & (1 << 6);
                ups_data.needReplacement = data[offset] & (1 << 7);
            }
            if (length - offset >= 2) {
                ups_data.overload = data[offset + 1] != 0;
            }
            if (length - offset >= 3) {
                ups_data.shutdownImminent = data[offset + 2] != 0;
            }
            break;

        case 0x02:
            if (length - offset >= 2) {
                ups_data.outlet1Switch = data[offset] != 0;
                ups_data.outlet2Switch = data[offset + 1] != 0;
            }
            break;

        case 0x06:
            if (length - offset >= 1) {
                ups_data.remainingCapacity = data[offset];
                if (ups_data.remainingCapacity > 100) ups_data.remainingCapacity = 100;
            }
            if (length - offset >= 5) {
                ups_data.runTimeToEmpty = (uint32_t)data[offset + 1] |
                                           ((uint32_t)data[offset + 2] << 8) |
                                           ((uint32_t)data[offset + 3] << 16) |
                                           ((uint32_t)data[offset + 4] << 24);
            }
            break;

        case 0x07:
            if (length - offset >= 6) {
                ups_data.load = data[offset + 5];
                if (ups_data.configActivePower > 0) {
                    ups_data.realPower = (uint16_t)(((uint32_t)ups_data.configActivePower * ups_data.load) / 100);
                } else if (ups_data.configApparentPower > 0) {
                    ups_data.realPower = (uint16_t)(((uint32_t)ups_data.configApparentPower * 60 * ups_data.load) / 10000);
                }
            }
            break;

        case 0x08:
            if (length - offset >= 1) {
                ups_data.remainingCapacityLimit = data[offset];
            }
            break;

        case 0x09:
            if (length - offset >= 4) {
                ups_data.delayShutdown = (int32_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24));
            }
            break;

        case 0x0a:
            if (length - offset >= 4) {
                ups_data.delayStart = (int32_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24));
            }
            break;

        case 0x0c:
            if (length - offset >= 6) {
                ups_data.designCapacity = data[offset + 4];
                ups_data.fullChargeCapacity = data[offset + 5];
            }
            break;

        case 0x0d:
            if (length - offset >= 2) {
                ups_data.configApparentPower = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            if (length - offset >= 3) {
                ups_data.configFrequency = data[offset + 2];
            }
            break;

        case 0x0e:
            if (length - offset >= 2) {
                ups_data.outputVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x10:
            if (length - offset >= 1) {
                uint8_t chemIdx = data[offset];
                if (chemIdx > 0 && (chemIdx != _chemStrIdx || ups_data.batteryType == "")) {
                    _chemStrIdx = chemIdx;
                    if (host) host->requestStringDescriptor(chemIdx);
                }
            }
            break;

        case 0x12:
            if (length - offset >= 1) {
                ups_data.configVoltage = data[offset];
            }
            break;

        case 0x13:
            if (length - offset >= 2) {
                ups_data.highVoltageTransfer = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x14:
            if (length - offset >= 1) {
                ups_data.lowVoltageTransfer = data[offset];
            }
            break;

        case 0x1f:
            if (length - offset >= 1) {
                ups_data.beeperEnabled = (data[offset] == 2);
            }
            break;

        default:
            break;
    }
}

void EatonDriver::parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) { // Safety check
            str += (char)data[i];
        }
    }
    if (index == 1) ups_data.manufacturer = str;
    else if (index == 2) ups_data.product = str;
    else if (index == 4) ups_data.serialNumber = str;
    else if (_chemStrIdx > 0 && index == _chemStrIdx) ups_data.batteryType = str;
}
