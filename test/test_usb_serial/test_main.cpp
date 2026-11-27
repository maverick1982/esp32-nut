#include <Arduino.h>
#include <unity.h>
#include <string.h>
#include <vector>
#include "DriverRegistry.h"
#include "UsbSerialDevice.h"
#include "UsbSerialLink.h"
#include "UsbSerialDiag.h"

// ADR 0014: the serial family next to hid_host. The configuration descriptor parser, the
// device-level classification (DriverRegistry::matchSerial), the claimed device
// (UsbSerialLink) and the "serial" object of the export (UsbSerialDiag::fill).

using DriverRegistry::SerialClaim;

void setUp(void) {}
void tearDown(void) {}

// --- Descriptor builders -----------------------------------------------------------------

struct Cfg {
    std::vector<uint8_t> b;
    Cfg() { b = {9, 0x02, 0, 0, 0, 1, 0, 0x80, 50}; }
    Cfg& iface(uint8_t num, uint8_t alt, uint8_t eps, uint8_t cls, uint8_t sub, uint8_t proto) {
        b.insert(b.end(), {9, 0x04, num, alt, eps, cls, sub, proto, 0});
        return *this;
    }
    Cfg& ep(uint8_t addr, uint8_t attr, uint16_t mps) {
        b.insert(b.end(), {7, 0x05, addr, attr, (uint8_t)(mps & 0xFF), (uint8_t)(mps >> 8), 0});
        return *this;
    }
    Cfg& raw(std::initializer_list<uint8_t> d) {
        b.insert(b.end(), d);
        return *this;
    }
    // Interface association descriptor (USB 2.0 ECN): skipped by the parser
    Cfg& iad(uint8_t first, uint8_t count, uint8_t cls, uint8_t sub) {
        return raw({8, 0x0B, first, count, cls, sub, 0, 0});
    }
    // The CDC functional descriptors of an ACM Communication interface: skipped too
    Cfg& cdcFunctional(uint8_t comm, uint8_t data) {
        return raw({5, 0x24, 0x00, 0x10, 0x01, 5, 0x24, 0x01, 0x00, data, 4, 0x24, 0x02, 0x02, 5, 0x24, 0x06, comm, data});
    }
    std::vector<uint8_t> done() {
        b[2] = (uint8_t)(b.size() & 0xFF);
        b[3] = (uint8_t)(b.size() >> 8);
        return b;
    }
};

static const uint8_t BULK = 0x02;
static const uint8_t INTR = 0x03;

// A CDC-ACM layout: Communication interface 0 (02/02/00) with its interrupt IN endpoint and
// functional descriptors, Data interface 1 (0A/00/00) with bulk OUT and IN. The datasheet
// layout of the XR21V1410; the real unit's layout comes from the "serial" export (ADR 0014)
static std::vector<uint8_t> cdcAcmConfig() {
    return Cfg()
        .iad(0, 2, 0x02, 0x02)
        .iface(0, 0, 1, 0x02, 0x02, 0x00).cdcFunctional(0, 1).ep(0x85, INTR, 10)
        .iface(1, 0, 2, 0x0A, 0x00, 0x00).ep(0x02, BULK, 64).ep(0x83, BULK, 64)
        .done();
}

static UsbSerialDeviceInfo deviceInfo(uint16_t vid, uint16_t pid, const std::vector<uint8_t>& cfg, uint8_t address = 1) {
    UsbSerialDeviceInfo info;
    info.vid = vid;
    info.pid = pid;
    info.address = address;
    TEST_ASSERT_TRUE(UsbConfigParser::parse(cfg.data(), cfg.size(), info));
    return info;
}

// --- Parser ------------------------------------------------------------------------------

void test_parse_cdc_acm_layout(void) {
    std::vector<uint8_t> cfg = cdcAcmConfig();
    UsbSerialDeviceInfo info = deviceInfo(0x04E2, 0x1410, cfg);
    TEST_ASSERT_FALSE(info.truncated);
    TEST_ASSERT_EQUAL(2, info.interface_count);
    const UsbSerialInterface& comm = info.interfaces[0];
    TEST_ASSERT_EQUAL(0, comm.number);
    TEST_ASSERT_EQUAL_HEX8(0x02, comm.cls);
    TEST_ASSERT_EQUAL_HEX8(0x02, comm.subclass);
    TEST_ASSERT_EQUAL(1, comm.endpoint_count);
    TEST_ASSERT_EQUAL_HEX8(0x85, comm.endpoints[0].address);
    TEST_ASSERT_TRUE(comm.endpoints[0].isInterrupt());
    TEST_ASSERT_TRUE(comm.endpoints[0].isIn());
    TEST_ASSERT_EQUAL(10, comm.endpoints[0].max_packet);
    const UsbSerialInterface& data = info.interfaces[1];
    TEST_ASSERT_EQUAL(1, data.number);
    TEST_ASSERT_EQUAL_HEX8(0x0A, data.cls);
    TEST_ASSERT_EQUAL(2, data.endpoint_count);
    TEST_ASSERT_TRUE(data.endpoints[0].isBulk());
    TEST_ASSERT_FALSE(data.endpoints[0].isIn());
    TEST_ASSERT_TRUE(data.endpoints[1].isIn());
    TEST_ASSERT_TRUE(data.hasBulkInOut());
    TEST_ASSERT_FALSE(comm.hasBulkInOut());
}

void test_parse_keeps_alternate_settings(void) {
    // A Data interface whose alternate setting 0 has no endpoints, as some devices offer
    std::vector<uint8_t> cfg = Cfg()
        .iface(0, 0, 1, 0x02, 0x02, 0x01).ep(0x81, INTR, 8)
        .iface(1, 0, 0, 0x0A, 0x00, 0x00)
        .iface(1, 1, 2, 0x0A, 0x00, 0x00).ep(0x02, BULK, 512).ep(0x82, BULK, 512)
        .done();
    UsbSerialDeviceInfo info = deviceInfo(0x1234, 0x5678, cfg);
    TEST_ASSERT_EQUAL(3, info.interface_count);
    TEST_ASSERT_EQUAL(0, info.interfaces[1].endpoint_count);
    TEST_ASSERT_EQUAL(1, info.interfaces[2].alternate);
    TEST_ASSERT_EQUAL(512, info.interfaces[2].endpoints[1].max_packet);
}

void test_parse_short_buffer_is_truncated(void) {
    std::vector<uint8_t> cfg = cdcAcmConfig();
    // The buffer ends inside the second bulk endpoint: wTotalLength says more
    const size_t len = cfg.size() - 3;
    UsbSerialDeviceInfo info;
    TEST_ASSERT_TRUE(UsbConfigParser::parse(cfg.data(), len, info));
    TEST_ASSERT_TRUE(info.truncated);
    TEST_ASSERT_EQUAL(2, info.interface_count);
    TEST_ASSERT_EQUAL(1, info.interfaces[1].endpoint_count); // what was complete is kept
}

void test_parse_zero_length_descriptor_stops(void) {
    std::vector<uint8_t> cfg = Cfg()
        .iface(0, 0, 1, 0x02, 0x02, 0x00).ep(0x81, INTR, 8)
        .raw({0, 0x24, 0, 0})  // bLength 0: the walk would never advance
        .iface(1, 0, 2, 0x0A, 0x00, 0x00).ep(0x02, BULK, 64).ep(0x83, BULK, 64)
        .done();
    UsbSerialDeviceInfo info = deviceInfo(0x1234, 0x5678, cfg);
    TEST_ASSERT_TRUE(info.truncated);
    TEST_ASSERT_EQUAL(1, info.interface_count);
}

void test_parse_more_interfaces_than_the_record(void) {
    Cfg c;
    for (uint8_t i = 0; i < 10; i++) c.iface(i, 0, 1, 0xFF, 0, 0).ep(0x80 | (i + 1), BULK, 64);
    std::vector<uint8_t> cfg = c.done();
    UsbSerialDeviceInfo info = deviceInfo(0x1234, 0x5678, cfg);
    TEST_ASSERT_TRUE(info.truncated);
    TEST_ASSERT_EQUAL(UsbSerialDeviceInfo::MAX_INTERFACES, info.interface_count);
    // The endpoints of the dropped interfaces did not land on the last kept one
    TEST_ASSERT_EQUAL(1, info.interfaces[UsbSerialDeviceInfo::MAX_INTERFACES - 1].endpoint_count);
}

void test_parse_more_endpoints_than_the_record(void) {
    Cfg c;
    c.iface(0, 0, 6, 0xFF, 0, 0);
    for (uint8_t i = 0; i < 6; i++) c.ep(0x81 + i, BULK, 64);
    std::vector<uint8_t> cfg = c.done();
    UsbSerialDeviceInfo info = deviceInfo(0x1234, 0x5678, cfg);
    TEST_ASSERT_TRUE(info.truncated);
    TEST_ASSERT_EQUAL(UsbSerialInterface::MAX_ENDPOINTS, info.interfaces[0].endpoint_count);
}

void test_parse_rejects_other_descriptors(void) {
    const uint8_t device_desc[18] = {18, 0x01, 0x00, 0x02};
    UsbSerialDeviceInfo info;
    TEST_ASSERT_FALSE(UsbConfigParser::parse(device_desc, sizeof(device_desc), info));
    TEST_ASSERT_FALSE(UsbConfigParser::parse(nullptr, 0, info));
    TEST_ASSERT_EQUAL(0, info.interface_count);
}

void test_string_descriptor_to_ascii(void) {
    const uint8_t desc[] = {12, 0x03, 'E', 0, 'x', 0, 'a', 0, 'r', 0, 0xE9, 0x00};
    char out[8];
    UsbConfigParser::stringToAscii(desc, sizeof(desc), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Exar?", out);
    char small[3];
    UsbConfigParser::stringToAscii(desc, sizeof(desc), small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("Ex", small);
    const uint8_t not_string[] = {4, 0x02, 'A', 0};
    UsbConfigParser::stringToAscii(not_string, sizeof(not_string), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// --- Classification ----------------------------------------------------------------------

void test_xr21v1410_is_claimed_on_its_comm_interface(void) {
    UsbSerialDeviceInfo info = deviceInfo(0x04E2, 0x1410, cdcAcmConfig());
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(info);
    TEST_ASSERT_TRUE(m.claim == SerialClaim::CLAIM);
    TEST_ASSERT_EQUAL_STRING("xr21v141x", m.adapter);
    TEST_ASSERT_EQUAL_STRING("XR21V141x", m.chip);
    TEST_ASSERT_EQUAL(0, m.interface_number);
}

void test_xr21v1410_without_cdc_layout_opens_the_bulk_interface(void) {
    // Not CDC compliant: usb_host_cdc_acm falls back to the interface with two bulk endpoints
    std::vector<uint8_t> cfg = Cfg()
        .iface(0, 0, 1, 0xFF, 0x00, 0x00).ep(0x81, INTR, 8)
        .iface(1, 0, 2, 0xFF, 0x00, 0x00).ep(0x02, BULK, 64).ep(0x83, BULK, 64)
        .done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x04E2, 0x1410, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::CLAIM);
    TEST_ASSERT_EQUAL_STRING("xr21v141x", m.adapter);
    TEST_ASSERT_EQUAL(1, m.interface_number);
}

void test_xr21v1410_without_bulk_endpoints_is_unsupported(void) {
    std::vector<uint8_t> cfg = Cfg().iface(0, 0, 1, 0x02, 0x02, 0x00).ep(0x81, INTR, 8).done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x04E2, 0x1410, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::UNSUPPORTED);
    TEST_ASSERT_NULL(m.adapter);
    TEST_ASSERT_EQUAL_STRING("XR21V141x", m.chip);
    TEST_ASSERT_NOT_NULL(m.reason);
}

void test_other_cdc_acm_device_takes_the_class_fallback(void) {
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x2341, 0x0043, cdcAcmConfig()));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::CLAIM);
    TEST_ASSERT_EQUAL_STRING("cdc-acm", m.adapter);
    TEST_ASSERT_EQUAL_STRING("CDC-ACM", m.chip);
    TEST_ASSERT_EQUAL(0, m.interface_number);
}

void test_cdc_acm_on_a_later_interface_number(void) {
    std::vector<uint8_t> cfg = Cfg()
        .iface(0, 0, 0, 0xFE, 0x01, 0x01)  // DFU runtime interface first
        .iface(2, 0, 1, 0x02, 0x02, 0x01).ep(0x81, INTR, 8)
        .iface(3, 0, 2, 0x0A, 0x00, 0x00).ep(0x02, BULK, 64).ep(0x82, BULK, 64)
        .done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x1234, 0x5678, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::CLAIM);
    TEST_ASSERT_EQUAL(2, m.interface_number);
}

void test_composite_hid_and_cdc_stays_hid(void) {
    std::vector<uint8_t> cfg = Cfg()
        .iface(0, 0, 1, 0x03, 0x00, 0x00).ep(0x81, INTR, 8)
        .iface(1, 0, 1, 0x02, 0x02, 0x00).ep(0x82, INTR, 8)
        .iface(2, 0, 2, 0x0A, 0x00, 0x00).ep(0x03, BULK, 64).ep(0x83, BULK, 64)
        .done();
    // Even with the VID/PID of a serial row: an HID interface wins
    TEST_ASSERT_TRUE(DriverRegistry::matchSerial(deviceInfo(0x04E2, 0x1410, cfg)).claim == SerialClaim::HID_DEVICE);
    TEST_ASSERT_TRUE(DriverRegistry::matchSerial(deviceInfo(0x0463, 0xFFFF, cfg)).claim == SerialClaim::HID_DEVICE);
}

void test_hid_ups_is_left_to_hid_host(void) {
    std::vector<uint8_t> cfg = Cfg().iface(0, 0, 1, 0x03, 0x00, 0x00).ep(0x81, INTR, 8).done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x0463, 0xFFFF, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::HID_DEVICE);
    TEST_ASSERT_NULL(m.adapter);
}

void test_hub_is_ignored(void) {
    std::vector<uint8_t> cfg = Cfg().iface(0, 0, 1, 0x09, 0x00, 0x00).ep(0x81, INTR, 1).done();
    UsbSerialDeviceInfo info = deviceInfo(0x05E3, 0x0608, cfg);
    TEST_ASSERT_TRUE(DriverRegistry::matchSerial(info).claim == SerialClaim::HUB);
    info.device_class = 0x09;
    info.interface_count = 0;
    TEST_ASSERT_TRUE(DriverRegistry::matchSerial(info).claim == SerialClaim::HUB);
}

void test_vendor_bridge_without_a_row_is_unsupported(void) {
    // CH340 (1a86:7523, issue #34): vendor class, no row yet
    std::vector<uint8_t> cfg = Cfg()
        .iface(0, 0, 3, 0xFF, 0x01, 0x02).ep(0x82, BULK, 32).ep(0x02, BULK, 32).ep(0x81, INTR, 8)
        .done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x1A86, 0x7523, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::UNSUPPORTED);
    TEST_ASSERT_NULL(m.adapter);
    TEST_ASSERT_NOT_NULL(m.reason);
}

void test_comm_interface_without_data_is_unsupported(void) {
    std::vector<uint8_t> cfg = Cfg().iface(0, 0, 1, 0x02, 0x02, 0x00).ep(0x81, INTR, 8).done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x1234, 0x5678, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::UNSUPPORTED);
}

void test_data_interface_without_comm_is_unsupported(void) {
    std::vector<uint8_t> cfg = Cfg().iface(0, 0, 2, 0x0A, 0x00, 0x00).ep(0x02, BULK, 64).ep(0x83, BULK, 64).done();
    DriverRegistry::SerialMatch m = DriverRegistry::matchSerial(deviceInfo(0x1234, 0x5678, cfg));
    TEST_ASSERT_TRUE(m.claim == SerialClaim::UNSUPPORTED);
}

void test_device_without_interfaces_is_unsupported(void) {
    UsbSerialDeviceInfo info;
    info.vid = 0x1234;
    info.pid = 0x5678;
    TEST_ASSERT_TRUE(DriverRegistry::matchSerial(info).claim == SerialClaim::UNSUPPORTED);
}

// The serial rows never shadow the HID or Q* families of match() (ADR 0013): 04e2:1410 is
// in neither, so the HID path keeps whatever it did for it
void test_serial_row_is_not_a_hid_or_qx_row(void) {
    DriverRegistry::Match m = DriverRegistry::match(0x04E2, 0x1410, "", "");
    TEST_ASSERT_TRUE(m.family == DriverRegistry::Family::HID);
    TEST_ASSERT_FALSE(m.explicit_hid);
    TEST_ASSERT_NULL(m.qx_transport);
}

// --- Claimed device ----------------------------------------------------------------------

static DriverRegistry::SerialMatch xrMatch() {
    return DriverRegistry::matchSerial(deviceInfo(0x04E2, 0x1410, cdcAcmConfig()));
}

void test_link_starts_empty(void) {
    UsbSerialLink link;
    TEST_ASSERT_FALSE(link.present());
    TEST_ASSERT_TRUE(link.state() == UsbSerialLink::State::NONE);
    char text[64] = "x";
    link.unsupportedText(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);
}

void test_link_open_success(void) {
    UsbSerialLink link;
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig(), 3), xrMatch(), 1000);
    TEST_ASSERT_TRUE(link.present());
    TEST_ASSERT_EQUAL_STRING("not opened", link.openResult());
    link.opened();
    TEST_ASSERT_TRUE(link.state() == UsbSerialLink::State::OPEN);
    TEST_ASSERT_EQUAL_STRING("open", link.stateName());
    TEST_ASSERT_EQUAL_STRING("ESP_OK", link.openResult());
    TEST_ASSERT_EQUAL_STRING("xr21v141x", link.adapter());
    TEST_ASSERT_EQUAL(0, link.interfaceNumber());
    TEST_ASSERT_EQUAL(1000, link.attachedAt());
    TEST_ASSERT_EQUAL(0, link.txBytes());
    char text[64];
    link.unsupportedText(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("USB serial: XR21V141x (04E2:1410)", text);
}

void test_link_open_failure_keeps_the_device_reported(void) {
    UsbSerialLink link;
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig()), xrMatch(), 0);
    link.openFailed("ESP_ERR_NOT_FOUND");
    TEST_ASSERT_TRUE(link.present());
    TEST_ASSERT_TRUE(link.state() == UsbSerialLink::State::OPEN_FAILED);
    TEST_ASSERT_EQUAL_STRING("open_failed", link.stateName());
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_NOT_FOUND", link.openResult());
    TEST_ASSERT_EQUAL_STRING("ESP_ERR_NOT_FOUND", link.lastError());
    char text[64];
    link.unsupportedText(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("USB serial: XR21V141x (04E2:1410)", text);
    // No bytes are counted on a device that is not open
    link.noteReceived(10, 1);
    TEST_ASSERT_EQUAL(0, link.rxBytes());
}

void test_link_counts_received_bytes(void) {
    UsbSerialLink link;
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig()), xrMatch(), 0);
    link.opened();
    link.noteReceived(7, 1);
    link.noteReceived(5, 2);
    TEST_ASSERT_EQUAL(12, link.rxBytes());
    TEST_ASSERT_EQUAL(3, link.rxChunks());
}

void test_link_detach_of_another_device_is_ignored(void) {
    UsbSerialLink link;
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig(), 2), xrMatch(), 0);
    link.opened();
    TEST_ASSERT_FALSE(link.detach(5));
    TEST_ASSERT_TRUE(link.present());
    TEST_ASSERT_TRUE(link.detach(2));
    TEST_ASSERT_FALSE(link.present());
    char text[64] = "x";
    link.unsupportedText(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);
    TEST_ASSERT_FALSE(link.detach(2)); // nothing left
}

void test_link_reconnection_starts_fresh(void) {
    UsbSerialLink link;
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig(), 2), xrMatch(), 100);
    link.opened();
    link.noteReceived(40, 4);
    link.noteError("USB host error 3");
    TEST_ASSERT_TRUE(link.detach(2));
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig(), 4), xrMatch(), 9000);
    link.opened();
    TEST_ASSERT_EQUAL(4, link.info().address);
    TEST_ASSERT_EQUAL(0, link.rxBytes());
    TEST_ASSERT_EQUAL_STRING("", link.lastError());
    TEST_ASSERT_EQUAL(9000, link.attachedAt());
}

void test_link_class_fallback_text(void) {
    UsbSerialLink link;
    UsbSerialDeviceInfo info = deviceInfo(0x2341, 0x0043, cdcAcmConfig());
    link.attach(info, DriverRegistry::matchSerial(info), 0);
    char text[64];
    link.unsupportedText(text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("USB serial: CDC-ACM (2341:0043)", text);
}

void test_link_events_and_long_texts(void) {
    UsbSerialLink link;
    link.noteEvent("ignored without a device");
    TEST_ASSERT_EQUAL_STRING("", link.lastEvent());
    link.attach(deviceInfo(0x04E2, 0x1410, cdcAcmConfig()), xrMatch(), 0);
    link.noteEvent("serial state 0x0003");
    TEST_ASSERT_EQUAL_STRING("serial state 0x0003", link.lastEvent());
    std::string longText(200, 'e');
    link.noteError(longText.c_str());
    TEST_ASSERT_EQUAL(UsbSerialLink::TEXT_LEN, strlen(link.lastError()));
}

// --- Export ------------------------------------------------------------------------------

void test_diag_absent_without_a_device(void) {
    UsbSerialLink link;
    UsbDiagSerial d;
    d.present = true;
    d.chip = "stale";
    UsbSerialDiag::fill(link, d);
    TEST_ASSERT_FALSE(d.present);
    TEST_ASSERT_EQUAL_STRING("", d.chip.c_str());
    TEST_ASSERT_EQUAL(0, (int)d.interfaces.size());
}

void test_diag_fill_from_the_link(void) {
    UsbSerialDeviceInfo info = deviceInfo(0x04E2, 0x1410, cdcAcmConfig(), 7);
    snprintf(info.manufacturer, sizeof(info.manufacturer), "%s", "Exar Corp.");
    snprintf(info.product, sizeof(info.product), "%s", "XR21V1410 USB-UART IC");
    UsbSerialLink link;
    link.attach(info, DriverRegistry::matchSerial(info), 1234);
    link.opened();
    link.noteReceived(3, 1);
    UsbDiagSerial d;
    UsbSerialDiag::fill(link, d);
    TEST_ASSERT_TRUE(d.present);
    TEST_ASSERT_EQUAL_STRING("04E2", d.vid.c_str());
    TEST_ASSERT_EQUAL_STRING("1410", d.pid.c_str());
    TEST_ASSERT_EQUAL_STRING("Exar Corp.", d.manufacturer.c_str());
    TEST_ASSERT_EQUAL_STRING("XR21V1410 USB-UART IC", d.product.c_str());
    TEST_ASSERT_EQUAL(7, d.address);
    TEST_ASSERT_EQUAL_STRING("XR21V141x", d.chip.c_str());
    TEST_ASSERT_EQUAL_STRING("xr21v141x", d.adapter.c_str());
    TEST_ASSERT_EQUAL_STRING("open", d.state.c_str());
    TEST_ASSERT_EQUAL_STRING("ESP_OK", d.open_result.c_str());
    TEST_ASSERT_EQUAL(3, d.rx_bytes);
    TEST_ASSERT_EQUAL(1, d.rx_chunks);
    TEST_ASSERT_EQUAL(0, d.tx_bytes);
    TEST_ASSERT_EQUAL(1234, d.attached_ms);
    TEST_ASSERT_FALSE(d.descriptor_truncated);
    TEST_ASSERT_EQUAL(2, (int)d.interfaces.size());
    TEST_ASSERT_EQUAL(0x02, d.interfaces[0].cls);
    TEST_ASSERT_EQUAL(1, (int)d.interfaces[0].endpoints.size());
    TEST_ASSERT_EQUAL_STRING("interrupt", d.interfaces[0].endpoints[0].type.c_str());
    TEST_ASSERT_EQUAL_STRING("in", d.interfaces[0].endpoints[0].direction.c_str());
    TEST_ASSERT_EQUAL(2, (int)d.interfaces[1].endpoints.size());
    TEST_ASSERT_EQUAL_STRING("bulk", d.interfaces[1].endpoints[0].type.c_str());
    TEST_ASSERT_EQUAL_STRING("out", d.interfaces[1].endpoints[0].direction.c_str());
    TEST_ASSERT_EQUAL(0x83, d.interfaces[1].endpoints[1].address);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_cdc_acm_layout);
    RUN_TEST(test_parse_keeps_alternate_settings);
    RUN_TEST(test_parse_short_buffer_is_truncated);
    RUN_TEST(test_parse_zero_length_descriptor_stops);
    RUN_TEST(test_parse_more_interfaces_than_the_record);
    RUN_TEST(test_parse_more_endpoints_than_the_record);
    RUN_TEST(test_parse_rejects_other_descriptors);
    RUN_TEST(test_string_descriptor_to_ascii);
    RUN_TEST(test_xr21v1410_is_claimed_on_its_comm_interface);
    RUN_TEST(test_xr21v1410_without_cdc_layout_opens_the_bulk_interface);
    RUN_TEST(test_xr21v1410_without_bulk_endpoints_is_unsupported);
    RUN_TEST(test_other_cdc_acm_device_takes_the_class_fallback);
    RUN_TEST(test_cdc_acm_on_a_later_interface_number);
    RUN_TEST(test_composite_hid_and_cdc_stays_hid);
    RUN_TEST(test_hid_ups_is_left_to_hid_host);
    RUN_TEST(test_hub_is_ignored);
    RUN_TEST(test_vendor_bridge_without_a_row_is_unsupported);
    RUN_TEST(test_comm_interface_without_data_is_unsupported);
    RUN_TEST(test_data_interface_without_comm_is_unsupported);
    RUN_TEST(test_device_without_interfaces_is_unsupported);
    RUN_TEST(test_serial_row_is_not_a_hid_or_qx_row);
    RUN_TEST(test_link_starts_empty);
    RUN_TEST(test_link_open_success);
    RUN_TEST(test_link_open_failure_keeps_the_device_reported);
    RUN_TEST(test_link_counts_received_bytes);
    RUN_TEST(test_link_detach_of_another_device_is_ignored);
    RUN_TEST(test_link_reconnection_starts_fresh);
    RUN_TEST(test_link_class_fallback_text);
    RUN_TEST(test_link_events_and_long_texts);
    RUN_TEST(test_diag_absent_without_a_device);
    RUN_TEST(test_diag_fill_from_the_link);
    return UNITY_END();
}
