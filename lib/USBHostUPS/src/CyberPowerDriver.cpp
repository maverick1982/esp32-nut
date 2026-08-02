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
        host->requestReport(0x06, 0x03, 6); // Battery
        host->requestReport(0x07, 0x03, 8); // Load
        host->requestReport(0x0F, 0x03, 2); // Input Voltage
        host->requestReport(0x12, 0x03, 2); // Output Voltage
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
                case 4: host->requestReport(0x10, 0x03, 2); break; // High Voltage Transfer (CyberPower)
                case 5: host->requestReport(0x18, 0x03, 2); break; // Config Active Power (CyberPower)
                case 6: host->requestReport(0x02, 0x03, 3); break; // Outlet switches
                case 7: host->requestReport(0x08, 0x03, 2); break; // Capacity Limit
                case 8: host->requestReport(0x09, 0x03, 5); break; // Delay Shutdown
                case 9: host->requestReport(0x0A, 0x03, 5); break; // Delay Start
                case 10: host->requestReport(0x0C, 0x03, 8); break; // Design/Full capacity
                case 11: host->requestReport(0x0D, 0x03, 4); break; // Config freq/power
                case 12: host->requestReport(0x14, 0x03, 2); break; // Low voltage transfer
                case 13: host->requestReport(0x1F, 0x03, 2); break; // Beeper
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

        case 0x06: // Battery Capacity & Runtime
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

        case 0x07: // Load
            if (length - offset >= 6) { // To support Eaton-like format where load is at offset 5
                ups_data.load = data[offset + 5];
            } else if (length - offset >= 1) {
                ups_data.load = data[offset];
            }
            if (ups_data.configApparentPower > 0) {
                ups_data.realPower = (uint16_t)(((uint32_t)ups_data.configApparentPower * 60 * ups_data.load) / 10000);
            }
            break;

        case 0x12: // Output Voltage
        case 0x0E: // Fallback Output Voltage
            if (length - offset >= 2) {
                ups_data.outputVoltage = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            } else if (length - offset >= 1) {
                ups_data.outputVoltage = data[offset];
            }
            break;

        case 0x10: // High Voltage Transfer
            if (length - offset >= 2) {
                ups_data.highVoltageTransfer = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            } else if (length - offset >= 1) {
                ups_data.highVoltageTransfer = data[offset];
            }
            break;

        case 0x18: // Config Active Power
        case 0x0D:
            if (length - offset >= 2) {
                ups_data.configApparentPower = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
            }
            if (report_id == 0x0D && length - offset >= 3) {
                ups_data.configFrequency = data[offset + 2];
            }
            break;

        case 0x02: // Outlet switches
            if (length - offset >= 2) {
                ups_data.outlet1Switch = data[offset] != 0;
                ups_data.outlet2Switch = data[offset + 1] != 0;
            }
            break;

        case 0x08: // Capacity Limit
            if (length - offset >= 1) {
                ups_data.remainingCapacityLimit = data[offset];
            }
            break;

        case 0x09: // Delay Shutdown
            if (length - offset >= 4) {
                ups_data.delayShutdown = (int32_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24));
            }
            break;

        case 0x0A: // Delay Start
            if (length - offset >= 4) {
                ups_data.delayStart = (int32_t)((uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24));
            }
            break;

        case 0x0C: // Design / Full capacity
            if (length - offset >= 6) {
                ups_data.designCapacity = data[offset + 4];
                ups_data.fullChargeCapacity = data[offset + 5];
            } else if (length - offset >= 2) {
                ups_data.designCapacity = data[offset];
                ups_data.fullChargeCapacity = data[offset + 1];
            }
            break;

        case 0x14: // Low Voltage Transfer
            if (length - offset >= 1) {
                ups_data.lowVoltageTransfer = data[offset];
            }
            break;

        case 0x1F: // Beeper
            if (length - offset >= 1) {
                ups_data.beeperEnabled = (data[offset] == 2);
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
