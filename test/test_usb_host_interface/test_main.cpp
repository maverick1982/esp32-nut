#include <unity.h>
#include "IUSBHostUPS.h"

// Verifica di coerenza dell'interfaccia IUSBHostUPS
class DerivedMockHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;

    const UPSData& getUPSData() const override { return _data; }
    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    const std::vector<HIDUsageDef>& getUsages() const override { return _usages; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

void setUp(void) {}
void tearDown(void) {}

void test_interface_string_indices_polymorphism(void) {
    DerivedMockHost derived;
    
    // Set via derived
    derived._iManufacturer = 1;
    derived._iProduct = 2;
    derived._iSerialNumber = 3;

    // Access via base pointer
    IUSBHostUPS* basePtr = &derived;
    TEST_ASSERT_EQUAL_UINT8(1, basePtr->_iManufacturer);
    TEST_ASSERT_EQUAL_UINT8(2, basePtr->_iProduct);
    TEST_ASSERT_EQUAL_UINT8(3, basePtr->_iSerialNumber);

    // Verify exact memory addresses to guarantee no field shadowing occurred
    TEST_ASSERT_EQUAL_PTR(&derived._iManufacturer, &basePtr->_iManufacturer);
    TEST_ASSERT_EQUAL_PTR(&derived._iProduct, &basePtr->_iProduct);
    TEST_ASSERT_EQUAL_PTR(&derived._iSerialNumber, &basePtr->_iSerialNumber);
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_interface_string_indices_polymorphism);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_interface_string_indices_polymorphism);
    UNITY_END();
}
void loop() {}
#endif
#endif
