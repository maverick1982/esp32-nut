#include <unity.h>
#include "FixtureReplayRunner.h"

// Expected NUT commands (US-056), derived from the FEATURE usages of each descriptor:
// Battery/Output.Test -> test.battery.*, APCPanelTest (0xff860072) -> test.panel.*,
// DelayBeforeShutdown (or APC 0xff86007d) -> load.off/.delay + shutdown.stop,
// + DelayBeforeStartup (or APC 0xff86007e) -> load.on/.delay + shutdown.return/stayoff,
// DelayBeforeReboot (or APC 0xff86007c) -> shutdown.reboot. No load.* or shutdown.*
// for Powercom, EcoFlow and OpenUPS (US-058). beeper.* follow the
// AudibleAlarmControl FEATURE plus a decoded ups.beeper.status: fixtures whose
// scenarios replay no beeper report expose none, as LIST CMD did before US-056.
// beeper.mute: the active beeper field is an 8-bit FEATURE (APC/CyberPower range 1..3)
#define CMD_BEEPER "beeper.enable beeper.disable beeper.toggle beeper.mute "
#define CMD_BATTERY_TEST "test.battery.start.quick test.battery.start.deep test.battery.stop "
#define CMD_PANEL_TEST "test.panel.start test.panel.stop "
#define CMD_LOAD_FULL "load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff shutdown.stop"
#define CMD_LOAD_OFF "load.off load.off.delay shutdown.stop"
// shutdown.default (US-058): listed with any of shutdown.return/reboot, load.off.delay, shutdown.stayoff
#define CMD_DEFAULT " shutdown.default"

// ups.delay.shutdown / ups.delay.start (US-058, user decision of 2026-10-09): NUT
// semantics, configured delays set by the host at connection (20/30, CyberPower 60/120),
// not the DelayBefore* timers read from the device (those stay in ups.timer.*). The
// Eaton 3S and APC fixtures therefore expect 20 where they used to expect the timer, -1.

void setUp(void) {}
void tearDown(void) {}

void test_replay_eaton_3s(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        CMD_BEEPER CMD_LOAD_FULL CMD_DEFAULT);
}

void test_replay_eaton_5p(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT);
}

// The CS500 declares the APC Back-UPS CS field UPS.Output.0xFF86007C: apc-hid.c writes
// 1 there for shutdown.return only. shutdown.reboot comes from the standard
// DelayBeforeReboot and must never resolve to that field (US-058).
static void checkCs500ShutdownFields(const std::vector<HIDUsageDef>& usages) {
    const HIDUsageDef* cs = CommandCatalog::apcCsReturnField(usages);
    TEST_ASSERT_NOT_NULL(cs);
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C", cs->path);
    const HIDUsageDef* reboot = CommandCatalog::rebootField(usages);
    TEST_ASSERT_NOT_NULL(reboot);
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeReboot", reboot->path);

    std::vector<CommandCatalog::WriteStep> steps;
    TEST_ASSERT_EQUAL((int)CommandCatalog::StepsResult::OK,
        (int)CommandCatalog::resolveShutdownSteps("shutdown.return", nullptr, usages, 20, 30, steps));
    TEST_ASSERT_EQUAL(1, steps.size());
    TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C", steps[0].def.path);
    TEST_ASSERT_EQUAL(1, steps[0].value);
    TEST_ASSERT_EQUAL((int)CommandCatalog::StepsResult::OK,
        (int)CommandCatalog::resolveShutdownSteps("shutdown.reboot", nullptr, usages, 20, 30, steps));
    TEST_ASSERT_EQUAL(1, steps.size());
    TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeReboot", steps[0].def.path);
    TEST_ASSERT_EQUAL(10, steps[0].value);
}

void test_replay_apc_cs500(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_FULL " shutdown.reboot" CMD_DEFAULT,
        checkCs500ShutdownFields);
}

void test_replay_apc_rs900mi(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_apc_xs700u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_OFF CMD_DEFAULT);
}

void test_replay_apc_smartups750(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json",
        /* no beeper report replayed */ CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_apc_bx1500g_issue55(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_apc_smartups750_issue13(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_apc_cs750rs_issue48(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_apc_bx750mi_issue60(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_apc_es700g_issue71(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json",
        CMD_BEEPER CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT);
}

void test_replay_powercom_spd750u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json",
        /* PowercomDriver: no shutdown commands (US-058) */ "test.battery.start.quick test.battery.start.deep test.battery.stop");
}

void test_replay_cyberpower_cp1350c(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT);
}

void test_replay_cyberpower_cp1500epfclcd(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT);
}

// The W150 reports PresentStatus.BatteryPresent = 0 in its normal state, but
// OpenUPSDriver ignores the unreliable flag, so ups.status stays OL; see the
// fixture "notes" (US-055).
void test_replay_openups_wallecube_w150(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json",
        "");
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
    RUN_TEST(test_replay_apc_cs750rs_issue48);
    RUN_TEST(test_replay_apc_bx1500g_issue55);
    RUN_TEST(test_replay_apc_bx750mi_issue60);
    RUN_TEST(test_replay_apc_es700g_issue71);
    RUN_TEST(test_replay_powercom_spd750u);
    RUN_TEST(test_replay_cyberpower_cp1350c);
    RUN_TEST(test_replay_cyberpower_cp1500epfclcd);
    RUN_TEST(test_replay_openups_wallecube_w150);
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
    RUN_TEST(test_replay_apc_cs750rs_issue48);
    RUN_TEST(test_replay_apc_bx1500g_issue55);
    RUN_TEST(test_replay_apc_bx750mi_issue60);
    RUN_TEST(test_replay_apc_es700g_issue71);
    RUN_TEST(test_replay_powercom_spd750u);
    RUN_TEST(test_replay_cyberpower_cp1350c);
    RUN_TEST(test_replay_cyberpower_cp1500epfclcd);
    RUN_TEST(test_replay_openups_wallecube_w150);
    UNITY_END();
}
void loop() {}
#endif
#endif

