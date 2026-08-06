#include "CyberPowerDriver.h"
#include "USBHostUPS.h"

CyberPowerDriver::CyberPowerDriver() : 
    _last_poll(0), 
    _last_fast_poll(0), 
    _last_step_time(0), 
    _poll_step(0) {
}

void CyberPowerDriver::setup() {
    _last_poll = 0;
    _last_fast_poll = 0;
    _poll_step = 0;
    _last_step_time = 0;
    Serial.println("[CyberPowerDriver] Setup started.");
}

void CyberPowerDriver::loop(USBHostUPS* host, UPSData& data, uint32_t now) {
    if (!host) return;

    // Polling Veloce (Dinamico)
    if (now - _last_fast_poll >= 2000 || _last_fast_poll == 0) {
        _last_fast_poll = now != 0 ? now : 1;
        host->requestReport(0x01, 0x03, 4); // Eaton/Generic status
        host->requestReport(0x0B, 0x03, 2); // CyberPower status
        host->requestReport(0x08, 0x03, 6); // Battery Capacity & Runtime (CyberPower)
        host->requestReport(0x13, 0x03, 2); // PercentLoad (CyberPower)
        host->requestReport(0x0F, 0x03, 3); // Input Voltage (CyberPower)
        host->requestReport(0x12, 0x03, 3); // Output Voltage (CyberPower)
        host->requestReport(0x0a, 0x03, 2); // Battery Voltage (CyberPower)
    }

    // Polling Lento (Statico)
    if (_poll_step == 0) {
        if (now - _last_poll >= 30000 || _last_poll == 0) {
            _last_poll = now != 0 ? now : 1;
            _poll_step = 1;
            _last_step_time = now;
        }
    }

    if (_poll_step > 0) {
        if (now - _last_step_time >= 50 || _poll_step == 1) {
            _last_step_time = now;
            switch (_poll_step) {
                case 1: if (data.manufacturer == "") host->requestStringDescriptor(1); break;
                case 2: if (data.product == "") host->requestStringDescriptor(2); break;
                case 3: if (data.serialNumber == "") host->requestStringDescriptor(3); break;
                case 4: host->requestReport(0x10, 0x03, 5); break; // Low/High Voltage Transfer
                case 5: host->requestReport(0x18, 0x03, 3); break; // Config Active Power
                case 6: host->requestReport(0x07, 0x03, 7); break; // Capacity Info
                case 7: host->requestReport(0x15, 0x03, 3); break; // Delay Shutdown
                case 8: host->requestReport(0x16, 0x03, 3); break; // Delay Start
                case 9: host->requestReport(0x09, 0x03, 2); break; // Config Voltage
                case 10: host->requestReport(0x0C, 0x03, 2); break; // Audible Alarm
                case 11: host->requestReport(0x0E, 0x03, 2); break; // Input Config Voltage
                case 12: host->requestReport(0x17, 0x03, 2); break; // Boost / Overload
                case 13: host->requestReport(0x0D, 0x03, 2); break; // Config Frequency
                default: 
                    _poll_step = 0; 
                    return;
            }
            _poll_step++;
        }
    }
}

void CyberPowerDriver::decodeReport(USBHostUPS* host, uint8_t report_id, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL) return;

    size_t offset = 0;
    if (length > 1 && data[0] == report_id) {
        offset = 1;
    }

    switch (report_id) {
        case 0x01: // Status fallback (Eaton-like)
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
            break;

        case 0x0B: // Status CyberPower specific
            if (length - offset >= 1) {
                ups_data.acPresent = data[offset] & (1 << 0);
                ups_data.charging = data[offset] & (1 << 1);
                ups_data.discharging = data[offset] & (1 << 2);
                ups_data.belowRemainingCapacityLimit = data[offset] & (1 << 3);
            }
            break;

        case 0x08: // Battery Capacity & Runtime (CyberPower)
            if (length - offset >= 1) {
                ups_data.remainingCapacity = data[offset];
                if (ups_data.remainingCapacity > 100) ups_data.remainingCapacity = 100;
            }
            if (length - offset >= 3) {
                ups_data.runTimeToEmpty = (uint32_t)data[offset + 1] | ((uint32_t)data[offset + 2] << 8);
            }
            break;

        case 0x13: // PercentLoad
            if (length - offset >= 1) {
                ups_data.load = data[offset];
            }
            if (ups_data.configApparentPower > 0) {
                ups_data.realPower = (uint16_t)(((uint32_t)ups_data.configApparentPower * ups_data.load) / 100);
            }
            break;

        case 0x0F: // Input Voltage
            if (length - offset >= 2) {
                ups_data.inputVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            } else if (length - offset >= 1) {
                ups_data.inputVoltage = data[offset];
            }
            break;

        case 0x12: // Output Voltage
            if (length - offset >= 2) {
                ups_data.outputVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            } else if (length - offset >= 1) {
                ups_data.outputVoltage = data[offset];
            }
            break;

        case 0x0a: // Battery Voltage
            if (length - offset >= 1) {
                ups_data.batteryVoltage = data[offset];
            }
            break;

        case 0x10: // Low & High Voltage Transfer
            if (length - offset >= 2) {
                ups_data.lowVoltageTransfer = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            if (length - offset >= 4) {
                ups_data.highVoltageTransfer = (uint16_t)data[offset + 2] | ((uint16_t)data[offset + 3] << 8);
            }
            break;

        case 0x18: // Config Active Power
            if (length - offset >= 2) {
                ups_data.configApparentPower = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            break;

        case 0x07: // Capacity Info (CyberPower)
            if (length - offset >= 5) {
                ups_data.remainingCapacityLimit = data[offset + 4];
            }
            break;

        case 0x09: // Config Voltage
            if (length - offset >= 1) {
                ups_data.configVoltage = data[offset];
            }
            break;

        case 0x0E: // Input Config Voltage
            if (length - offset >= 2) {
                ups_data.configVoltage = (uint8_t)data[offset];
            } else if (length - offset >= 1) {
                ups_data.configVoltage = data[offset];
            }
            break;

        case 0x0D: // Report 0x0D is iManufacturer (string index), not frequency
            break;

        case 0x15: // Delay Shutdown
            if (length - offset >= 2) {
                ups_data.delayShutdown = (int16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8));
            }
            break;

        case 0x16: // Delay Start
            if (length - offset >= 2) {
                ups_data.delayStart = (int16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8));
            }
            break;

        case 0x0C: // Audible Alarm
            if (length - offset >= 1) {
                ups_data.beeperEnabled = (data[offset] == 1);
            }
            break;

        default:
            break;
    }
}

void CyberPowerDriver::parseStringDescriptor(uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
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
