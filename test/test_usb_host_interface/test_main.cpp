#include <unity.h>
#include "IUSBHostUPS.h"

// Verifica di coerenza dell'interfaccia IUSBHostUPS
class DerivedMockHost : public IUSBHostUPS {
public:
    UPSData _data;
    std::vector<HIDUsageDef> _usages;

    void lock() const override {}
    void unlock() const override {}
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }
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

#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

class MockThreadSafeHost : public IUSBHostUPS {
public:
    UPSData _data;
    mutable std::recursive_mutex _mutex;

    void lock() const override { _mutex.lock(); }
    void unlock() const override { _mutex.unlock(); }
    UPSDataLock getUPSData() const override { return UPSDataLock(_data, this); }

    String getUPSStatusString() const override { return "OL"; }
    bool setBeeper(bool) override { return true; }
    bool isConnected() const override { return true; }

    const std::vector<HIDUsageDef>& getUsages() const override { static std::vector<HIDUsageDef> dummy; return dummy; }
    const HIDUsageDef* getUsageDef(uint32_t) const override { return nullptr; }
    String getActiveBeeperPath() const override { return ""; }
    uint32_t getQuirks() const override { return 0; }
    bool isControlPending() const override { return false; }
    bool requestReport(uint8_t, uint8_t, uint16_t) override { return true; }
    bool requestStringDescriptor(uint8_t) override { return true; }
};

void test_raii_mutex_concurrency(void) {
    MockThreadSafeHost host;
    std::atomic<bool> thread_started(false);
    std::atomic<bool> worker_got_lock(false);
    std::thread worker;

    {
        // 1. Il Main Thread acquisisce il lock tramite il RAII Proxy per primo
        auto locked_data = host.getUPSData();

        // 2. Lancia il Worker Thread
        worker = std::thread([&host, &thread_started, &worker_got_lock]() {
            thread_started = true;
            // Tenta di acquisire il proxy RAII, che dovrebbe bloccarsi sul mutex
            auto data = host.getUPSData();
            worker_got_lock = true; // Raggiungibile SOLO se il mutex viene rilasciato dal main thread
        });

        // Aspetta che il thread sia in esecuzione
        while (!thread_started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Da tempo al worker di bloccarsi su getUPSData()
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 3. Verifica: Il worker DEVE essere bloccato, altrimenti il mutex non sta proteggendo
        TEST_ASSERT_FALSE_MESSAGE(worker_got_lock.load(), "Il Worker ha ignorato il Mutex (Lock fallito)!");
    } // <-- RAII sblocca il mutex all'uscita dello scope

    // 4. Aspetta il completamento del worker (che ora dovrebbe aver ricevuto il lock)
    worker.join();
    
    // 5. Verifica che il worker si sia regolarmente sbloccato
    TEST_ASSERT_TRUE_MESSAGE(worker_got_lock.load(), "Il Worker non si e' sbloccato dopo la distruzione del proxy!");
}

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
    RUN_TEST(test_raii_mutex_concurrency);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_interface_string_indices_polymorphism);
    RUN_TEST(test_raii_mutex_concurrency);
    UNITY_END();
}
void loop() {}
#endif
#endif


