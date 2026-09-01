#include <unity.h>
#include "FixtureReplayRunner.h"

void setUp(void) {}
void tearDown(void) {}

void test_replay_eaton_3s(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json");
}

void test_replay_eaton_5p(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json");
}

void test_replay_apc_cs500(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json");
}

void test_replay_apc_rs900mi(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json");
}

void test_replay_apc_xs700u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json");
}

void test_replay_apc_smartups750(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json");
}

void test_replay_apc_smartups750_issue13(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json");
}

void test_replay_powercom_spd750u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json");
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_replay_eaton_3s);
    RUN_TEST(test_replay_eaton_5p);
    RUN_TEST(test_replay_apc_cs500);
    RUN_TEST(test_replay_apc_rs900mi);
    RUN_TEST(test_replay_apc_xs700u);
    RUN_TEST(test_replay_apc_smartups750);
    RUN_TEST(test_replay_apc_smartups750_issue13);
    RUN_TEST(test_replay_powercom_spd750u);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_replay_eaton_3s);
    RUN_TEST(test_replay_eaton_5p);
    RUN_TEST(test_replay_apc_cs500);
    RUN_TEST(test_replay_apc_rs900mi);
    RUN_TEST(test_replay_apc_xs700u);
    RUN_TEST(test_replay_apc_smartups750);
    RUN_TEST(test_replay_apc_smartups750_issue13);
    RUN_TEST(test_replay_powercom_spd750u);
    UNITY_END();
}
void loop() {}
#endif
#endif
