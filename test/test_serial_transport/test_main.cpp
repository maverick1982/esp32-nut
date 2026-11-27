#include <Arduino.h>
#include <unity.h>
#include <string.h>
#include <string>
#include "FakeSerialPort.h"
#include "QxEngine.h"
#include "QxProtocols.h"
#include "QxSerialTransport.h"
#include "QxTestingTables.h"
#include "SerialRxBuffer.h"
#include "UPSData.h"
#include "XrUartSetup.h"

// ADR 0014 sections 4 and 6: the receive buffer between the CDC-ACM data callback and the poll
// task, the XR21V141x register writes of the datasheet, and the serial path of qx_command()
// (nut_repo/drivers/nutdrv_qx.c:4498-4523) on an ISerialPort

void setUp(void) {}
void tearDown(void) {}

// --- SerialRxBuffer ----------------------------------------------------------------------

void test_rx_buffer_keeps_the_order_across_the_wrap(void) {
    SerialRxBuffer<8> b;
    uint8_t out[8];
    const uint8_t a[] = { 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL(6, b.push(a, 6));
    TEST_ASSERT_EQUAL(4, b.pop(out, 4));
    TEST_ASSERT_EQUAL_UINT8(4, out[3]);
    const uint8_t c[] = { 7, 8, 9, 10, 11, 12 };
    TEST_ASSERT_EQUAL(6, b.push(c, 6));  // wraps
    TEST_ASSERT_EQUAL(8, b.size());
    TEST_ASSERT_EQUAL(8, b.pop(out, 8));
    const uint8_t expected[] = { 5, 6, 7, 8, 9, 10, 11, 12 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 8);
    TEST_ASSERT_EQUAL(0, b.pop(out, 8));
}

void test_rx_buffer_drops_and_counts_on_overflow(void) {
    SerialRxBuffer<4> b;
    const uint8_t a[] = { 1, 2, 3, 4, 5, 6 };
    TEST_ASSERT_EQUAL(4, b.push(a, 6));
    TEST_ASSERT_EQUAL_UINT32(2, b.dropped());
    TEST_ASSERT_EQUAL(0, b.push(a, 1));
    TEST_ASSERT_EQUAL_UINT32(3, b.dropped());
    TEST_ASSERT_EQUAL(4, b.clear());
    TEST_ASSERT_EQUAL(0, b.size());
    TEST_ASSERT_EQUAL(4, b.push(a, 4));
}

// --- XR21V141x: no flow control, in the order of the datasheet ---------------------------

void test_xr_no_flow_control_sequence(void) {
    TEST_ASSERT_EQUAL(6, XrUart::NO_FLOW_CONTROL_COUNT);
    // UART off first: FLOW_CONTROL may only be written with the UART disabled (3.3.6)
    TEST_ASSERT_EQUAL_HEX16(0x0003, XrUart::NO_FLOW_CONTROL[0].wIndex());
    TEST_ASSERT_EQUAL_HEX16(0x0000, XrUart::NO_FLOW_CONTROL[0].wValue());
    TEST_ASSERT_EQUAL_HEX16(0x000C, XrUart::NO_FLOW_CONTROL[1].wIndex());
    TEST_ASSERT_EQUAL_HEX16(0x0000, XrUart::NO_FLOW_CONTROL[1].wValue());
    TEST_ASSERT_EQUAL_HEX16(0x001A, XrUart::NO_FLOW_CONTROL[2].wIndex());
    // Enable order of 3.1.1: FIFO_ENABLE (block 4) 1, UART_ENABLE 3, FIFO_ENABLE 3
    TEST_ASSERT_EQUAL_HEX16(0x0410, XrUart::NO_FLOW_CONTROL[3].wIndex());
    TEST_ASSERT_EQUAL_HEX16(0x0001, XrUart::NO_FLOW_CONTROL[3].wValue());
    TEST_ASSERT_EQUAL_HEX16(0x0003, XrUart::NO_FLOW_CONTROL[4].wIndex());
    TEST_ASSERT_EQUAL_HEX16(0x0003, XrUart::NO_FLOW_CONTROL[4].wValue());
    TEST_ASSERT_EQUAL_HEX16(0x0410, XrUart::NO_FLOW_CONTROL[5].wIndex());
    TEST_ASSERT_EQUAL_HEX16(0x0003, XrUart::NO_FLOW_CONTROL[5].wValue());
    TEST_ASSERT_EQUAL_HEX8(0x40, XrUart::SET_REG_REQUEST_TYPE);
    TEST_ASSERT_EQUAL_HEX8(0x00, XrUart::SET_REG_REQUEST);
}

// --- QxSerialTransport -------------------------------------------------------------------

static int send(QxSerialTransport& t, const char* cmd, char* buf, size_t buflen) {
    memset(buf, 0, buflen);
    return t.command(cmd, strlen(cmd), buf, buflen);
}

void test_line_settings_of_nutdrv_qx(void) {
    const SerialLineSettings s = QxSerialTransport::lineSettings();
    TEST_ASSERT_EQUAL_UINT32(2400, s.baud);
    TEST_ASSERT_EQUAL_UINT8(8, s.data_bits);
    TEST_ASSERT_EQUAL('N', s.parity);
    TEST_ASSERT_EQUAL_UINT8(1, s.stop_bits);
    TEST_ASSERT_TRUE(s.dtr);   // cablepower "normal"
    TEST_ASSERT_FALSE(s.rts);
    TEST_ASSERT_EQUAL_UINT32(1100, QxSerialTransport::SETTLE_MS);
}

void test_flush_send_and_one_line(void) {
    FakeSerialPort port;
    port.arrive("stale\r");  // left from before: ser_flush_io() drops it
    port.answers["QGS\r"] = "(234.9 50.0 229.8 50.0 000 000 000 366 271 000 41.0 000 000 000\r";
    QxSerialTransport t(port);
    char buf[128];
    const int n = send(t, "QGS\r", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(1, port.discards);
    TEST_ASSERT_EQUAL(6, port.discardedBytes);
    TEST_ASSERT_EQUAL_STRING("QGS\r", port.written.c_str());
    TEST_ASSERT_EQUAL((int)strlen(buf), n);
    TEST_ASSERT_EQUAL_STRING("(234.9 50.0 229.8 50.0 000 000 000 366 271 000 41.0 000 000 000\r", buf);
    TEST_ASSERT_EQUAL_STRING("serial", t.name());
}

// Canonical mode: '\n' ends a line too; what follows stays for the next flush
void test_newline_ends_the_line_and_the_rest_stays(void) {
    FakeSerialPort port;
    port.answers["QPI\r"] = "(PI03\nEXTRA\r";
    QxSerialTransport t(port);
    char buf[64];
    TEST_ASSERT_EQUAL(6, send(t, "QPI\r", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("(PI03\n", buf);
    TEST_ASSERT_EQUAL(6, port.rx.size());  // "EXTRA\r" waits for the next ser_flush_io()
}

void test_no_answer_is_a_timeout(void) {
    FakeSerialPort port;
    QxSerialTransport t(port);
    char buf[64];
    const uint32_t start = port.now;
    TEST_ASSERT_EQUAL(0, send(t, "QGS\r", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(QxSerialTransport::READ_WAIT_MS, port.now - start);
    TEST_ASSERT_EQUAL_STRING("timeout", t.lastError());
}

// A line without its end within SER_WAIT_SEC: the canonical read returns nothing
void test_incomplete_line_is_a_timeout(void) {
    FakeSerialPort port;
    port.answers["QGS\r"] = "(234.9 50.0";
    QxSerialTransport t(port);
    char buf[64];
    TEST_ASSERT_EQUAL(0, send(t, "QGS\r", buf, sizeof(buf)));
}

void test_late_answer_is_a_timeout(void) {
    FakeSerialPort port;
    port.answers["QGS\r"] = "(234.9\r";
    port.answerDelayMs = 1200;
    QxSerialTransport t(port);
    char buf[64];
    TEST_ASSERT_EQUAL(0, send(t, "QGS\r", buf, sizeof(buf)));
}

// One read() of buflen bytes: a longer line is cut there
void test_line_longer_than_the_buffer(void) {
    FakeSerialPort port;
    port.answers["QID\r"] = "(12345678901234\r";
    QxSerialTransport t(port);
    char buf[8];
    TEST_ASSERT_EQUAL(8, send(t, "QID\r", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_MEMORY("(1234567", buf, 8);
}

void test_write_and_read_errors(void) {
    FakeSerialPort port;
    QxSerialTransport t(port);
    char buf[16];
    port.writeResult = -1;
    TEST_ASSERT_EQUAL(-1, send(t, "QGS\r", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("write failed", t.lastError());
    port.writeResult = 0;
    port.readFails = true;
    TEST_ASSERT_TRUE(send(t, "QGS\r", buf, sizeof(buf)) < 0);
    TEST_ASSERT_EQUAL_STRING("read failed", t.lastError());
}

// The Q* engine on the serial transport: megatec claims and walks with the answers of its
// upstream testing table, as it does on the USB transports
void test_megatec_claims_over_the_serial_transport(void) {
    FakeSerialPort port;
    for (const QxTesting* r = MEGATEC_TESTING; r->cmd; r++) port.answers[r->cmd] = r->answer;
    QxSerialTransport t(port);
    QxEngine e(QX_MEGATEC, t);
    UPSData d;
    TEST_ASSERT_TRUE(e.claim(d));
    TEST_ASSERT_EQUAL_STRING("Q1\r", port.writes[0].c_str());
    TEST_ASSERT_TRUE(e.initInfo(d, port.now));
    TEST_ASSERT_EQUAL_STRING("215.0", d.get("input.voltage").c_str());
    TEST_ASSERT_EQUAL_STRING("230", d.get("input.voltage.nominal").c_str());  // "%.0f"
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_rx_buffer_keeps_the_order_across_the_wrap);
    RUN_TEST(test_rx_buffer_drops_and_counts_on_overflow);
    RUN_TEST(test_xr_no_flow_control_sequence);
    RUN_TEST(test_line_settings_of_nutdrv_qx);
    RUN_TEST(test_flush_send_and_one_line);
    RUN_TEST(test_newline_ends_the_line_and_the_rest_stays);
    RUN_TEST(test_no_answer_is_a_timeout);
    RUN_TEST(test_incomplete_line_is_a_timeout);
    RUN_TEST(test_late_answer_is_a_timeout);
    RUN_TEST(test_line_longer_than_the_buffer);
    RUN_TEST(test_write_and_read_errors);
    RUN_TEST(test_megatec_claims_over_the_serial_transport);
    return UNITY_END();
}
