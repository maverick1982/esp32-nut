#include <unity.h>
#include "NetLinkSelector.h"

// One active network link at a time (US-090, PDR 0002 D3)

class FakeLink : public INetLink {
public:
    explicit FakeLink(NetLinkKind k) : k(k) {}
    NetLinkKind kind() const override { return k; }
    void begin() override { begins++; }
    void end() override { ends++; }
    void loop(uint32_t now) override { loops++; lastNow = now; }
    bool isConnected() const override { return connected; }
    String statusText() const override { return "fake"; }
    String localIP() const override { return "0.0.0.0"; }
    void macAddress(uint8_t mac[6]) const override { for (int i = 0; i < 6; i++) mac[i] = 0; }

    NetLinkKind k;
    int begins = 0, ends = 0, loops = 0;
    uint32_t lastNow = 0;
    bool connected = false;
};

void setUp(void) {}
void tearDown(void) {}

void test_empty_selector_is_safe(void) {
    NetLinkSelector s;
    TEST_ASSERT_NULL(s.active());
    TEST_ASSERT_FALSE(s.isConnected());
    s.loop(100);
}

void test_activate_starts_the_link(void) {
    NetLinkSelector s;
    FakeLink a(NetLinkKind::STATION);
    s.activate(&a);
    TEST_ASSERT_EQUAL_PTR(&a, s.active());
    TEST_ASSERT_EQUAL_INT(1, a.begins);
    TEST_ASSERT_EQUAL_INT(0, a.ends);
}

void test_switching_stops_the_previous_link(void) {
    NetLinkSelector s;
    FakeLink a(NetLinkKind::STATION), b(NetLinkKind::ETHERNET);
    s.activate(&a);
    s.activate(&b);
    TEST_ASSERT_EQUAL_INT(1, a.ends);
    TEST_ASSERT_EQUAL_INT(1, b.begins);
    TEST_ASSERT_EQUAL_PTR(&b, s.active());
}

void test_reactivating_does_not_restart(void) {
    NetLinkSelector s;
    FakeLink a(NetLinkKind::ACCESS_POINT);
    s.activate(&a);
    s.activate(&a);
    TEST_ASSERT_EQUAL_INT(1, a.begins);
    TEST_ASSERT_EQUAL_INT(0, a.ends);
}

void test_loop_and_state_follow_the_active_link(void) {
    NetLinkSelector s;
    FakeLink a(NetLinkKind::STATION), b(NetLinkKind::ETHERNET);
    s.activate(&a);
    s.activate(&b);
    s.loop(1234);
    TEST_ASSERT_EQUAL_INT(0, a.loops);
    TEST_ASSERT_EQUAL_INT(1, b.loops);
    TEST_ASSERT_EQUAL_UINT32(1234, b.lastNow);

    a.connected = true;
    TEST_ASSERT_FALSE(s.isConnected());
    b.connected = true;
    TEST_ASSERT_TRUE(s.isConnected());
}

void test_type_names(void) {
    TEST_ASSERT_EQUAL_STRING("wifi", FakeLink(NetLinkKind::STATION).typeName());
    TEST_ASSERT_EQUAL_STRING("wifi", FakeLink(NetLinkKind::ACCESS_POINT).typeName());
    TEST_ASSERT_EQUAL_STRING("ethernet", FakeLink(NetLinkKind::ETHERNET).typeName());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_selector_is_safe);
    RUN_TEST(test_activate_starts_the_link);
    RUN_TEST(test_switching_stops_the_previous_link);
    RUN_TEST(test_reactivating_does_not_restart);
    RUN_TEST(test_loop_and_state_follow_the_active_link);
    RUN_TEST(test_type_names);
    return UNITY_END();
}
