#include <Arduino.h>
#include <unity.h>
#define private public
#include "USBHostUPS.h"
#undef private
USBHostUPS test_usb_ups;

void setUp(void) {
    // Predisposizione per ciascun test
}

void tearDown(void) {
    // Pulizia dopo ciascun test
}

void test_usb_host_initialization(void) {
    // Verifica che l'inizializzazione dello stack USB Host vada a buon fine
    TEST_ASSERT_TRUE(test_usb_ups.begin());
}

void test_decode_battery_charge(void) {
    // Test con Report ID incluso nel buffer (85%)
    uint8_t data_with_id[] = {0x01, 85};
    test_usb_ups.decodeReport(0x01, data_with_id, sizeof(data_with_id));
    TEST_ASSERT_EQUAL_UINT8(85, test_usb_ups.getBatteryCharge());

    // Test senza Report ID nel buffer (solo payload) (92%)
    uint8_t data_raw[] = {92};
    test_usb_ups.decodeReport(0x01, data_raw, sizeof(data_raw));
    TEST_ASSERT_EQUAL_UINT8(92, test_usb_ups.getBatteryCharge());
}

void test_decode_status(void) {
    // Test OL con Report ID incluso nel buffer
    uint8_t data_ol[] = {0x02, 1};
    test_usb_ups.decodeReport(0x02, data_ol, sizeof(data_ol));
    TEST_ASSERT_EQUAL_STRING("OL", test_usb_ups.getUPSStatus().c_str());

    // Test OB senza Report ID nel buffer (solo payload)
    uint8_t data_ob[] = {2};
    test_usb_ups.decodeReport(0x02, data_ob, sizeof(data_ob));
    TEST_ASSERT_EQUAL_STRING("OB", test_usb_ups.getUPSStatus().c_str());
}

void test_decode_input_voltage(void) {
    // Test tensione 230.0V (2300 decivolt) con Report ID incluso. 2300 = 0x08FC.
    uint8_t data_volt_id[] = {0x03, 0xFC, 0x08};
    test_usb_ups.decodeReport(0x03, data_volt_id, sizeof(data_volt_id));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f, test_usb_ups.getInputVoltage());

    // Test tensione 225.5V (2255 decivolt) senza Report ID. 2255 = 0x08CF.
    uint8_t data_volt_raw[] = {0xCF, 0x08};
    test_usb_ups.decodeReport(0x03, data_volt_raw, sizeof(data_volt_raw));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 225.5f, test_usb_ups.getInputVoltage());
}

void test_dev_gone(void) {
    // Impostiamo dei valori
    uint8_t data_ol[] = {0x02, 1};
    test_usb_ups.decodeReport(0x02, data_ol, sizeof(data_ol));
    uint8_t data_charge[] = {0x01, 85};
    test_usb_ups.decodeReport(0x01, data_charge, sizeof(data_charge));
    
    TEST_ASSERT_EQUAL_STRING("OL", test_usb_ups.getUPSStatus().c_str());
    TEST_ASSERT_EQUAL_UINT8(85, test_usb_ups.getBatteryCharge());

    // Simuliamo disconnessione
    usb_host_client_event_msg_t event_msg;
    event_msg.event = USB_HOST_CLIENT_EVENT_DEV_GONE;
    event_msg.dev_gone.dev_hdl = NULL;
    test_usb_ups.handle_client_event(&event_msg);

    // Verifichiamo reset
    TEST_ASSERT_EQUAL_STRING("Unknown", test_usb_ups.getUPSStatus().c_str());
    TEST_ASSERT_EQUAL_UINT8(0, test_usb_ups.getBatteryCharge());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, test_usb_ups.getInputVoltage());
}

void setup() {
    // Delay per stabilizzare la connessione seriale e permettere al monitor di agganciarsi
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_usb_host_initialization);
    RUN_TEST(test_decode_battery_charge);
    RUN_TEST(test_decode_status);
    RUN_TEST(test_decode_input_voltage);
    RUN_TEST(test_dev_gone);
    UNITY_END();
}

void loop() {
    // Loop vuoto
}
