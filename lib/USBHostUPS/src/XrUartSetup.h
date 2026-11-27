#ifndef XR_UART_SETUP_H
#define XR_UART_SETUP_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Vendor requests of the MaxLinear/Exar XR21V141x (ADR 0014, section 4), written from
 * the XR21V1410 datasheet (rev. 1.4.1), not from any driver code.
 *
 * With a CDC-ACM host driver the chip takes the standard SET_LINE_CODING and
 * SET_CONTROL_LINE_STATE (datasheet section 1.2 and table 4), but it starts with automatic
 * RTS/CTS hardware flow control (table 1: FLOW_CONTROL 0x01, GPIO_MODE 0x01). A NUT serial
 * driver opens its port without flow control, so the adapter turns it off with XR_SET_REG:
 * FLOW_CONTROL may only be written with the UART disabled (section 3.3.6), and the UART is
 * enabled again in the order of section 3.1.1 (FIFO_ENABLE 1, UART_ENABLE 3, FIFO_ENABLE 3).
 */
namespace XrUart {

// Table 4: XR_SET_REG, host to device, vendor, device; wValue = value, wIndex = register |
// (block << 8), no data stage
static const uint8_t SET_REG_REQUEST_TYPE = 0x40;
static const uint8_t SET_REG_REQUEST = 0x00;

// Table 5: control blocks
static const uint8_t BLOCK_UART = 0x00;
static const uint8_t BLOCK_UART_MANAGER = 0x04;

// Table 7: UART registers (block 0); table 6: UART manager registers (block 4)
static const uint8_t REG_UART_ENABLE = 0x03;   // bit 0 TX, bit 1 RX
static const uint8_t REG_FLOW_CONTROL = 0x0C;  // [2:0] mode, 0 = none
static const uint8_t REG_GPIO_MODE = 0x1A;     // [2:0] 0 = every pin a GPIO
static const uint8_t REG_FIFO_ENABLE = 0x10;   // bit 0 TX FIFO, bit 1 RX FIFO

struct RegWrite {
    uint8_t block;
    uint8_t reg;
    uint8_t value;
    uint16_t wIndex() const { return (uint16_t)(reg | (block << 8)); }
    uint16_t wValue() const { return value; }
};

// The writes that leave the UART enabled without flow control, in order
static const RegWrite NO_FLOW_CONTROL[] = {
    { BLOCK_UART, REG_UART_ENABLE, 0x00 },          // UART off before FLOW_CONTROL (3.3.6)
    { BLOCK_UART, REG_FLOW_CONTROL, 0x00 },         // no flow control (table 13, mode 0)
    { BLOCK_UART, REG_GPIO_MODE, 0x00 },            // RTS#/CTS# back to GPIOs (table 14)
    { BLOCK_UART_MANAGER, REG_FIFO_ENABLE, 0x01 },  // enable order of 3.1.1: TX FIFO,
    { BLOCK_UART, REG_UART_ENABLE, 0x03 },          // TX and RX,
    { BLOCK_UART_MANAGER, REG_FIFO_ENABLE, 0x03 },  // then the RX FIFO
};
static const size_t NO_FLOW_CONTROL_COUNT = sizeof(NO_FLOW_CONTROL) / sizeof(NO_FLOW_CONTROL[0]);

}  // namespace XrUart

#endif // XR_UART_SETUP_H
