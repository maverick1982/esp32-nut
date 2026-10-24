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

// LIST RW (US-067), "name:aux" sorted as upsd: derived from the descriptor bytes and the
// replayed reports with the RW rows of the upstream subdrivers (info_len = aux), not from
// the firmware. A row is listed when its first value converts and no earlier row with the
// same name is still unread; ups.delay.* (aux 10, Powercom 8) when one of the paths of its
// HU_FLAG_ABSENT rows is in the descriptor. The first list is right after the replay, the
// second after completeInitialPoll(), the end of the first full poll: rows never read are
// dropped there. Fixtures without reports bind no row: only ups.delay.* is listed.
#define RW_DELAYS "ups.delay.shutdown:10 ups.delay.start:10"
#define RW_DELAY_OFF "ups.delay.shutdown:10"

void setUp(void) {}
void tearDown(void) {}

// mge-hid.c rows: battery.charge.low (2013, aux 5) FEATURE 0x22 [34, 20] = 20;
// input.transfer.high (2186, aux 5) FEATURE 0x13 [19, 8, 1] = 264 (16 bits, V exp 7);
// input.transfer.low (2179, aux 5) FEATURE 0x14 [20, 161] = 161; outlet.desc (2247) and
// outlet.1.desc (2261), HU_FLAG_ABSENT with aux 20, paths in FEATURE 0x0B.
// output.voltage.nominal (2232) is read (FEATURE 0x12 [18, 230]) but waits for mge_type,
// which the driver learns in loop(): the replay never calls it, so the row is listed
// neither before nor after completeInitialPoll() (DEFER, decided by its next value).
#define RW_EATON_3S "battery.charge.low:5 input.transfer.high:5 input.transfer.low:5 outlet.1.desc:20 outlet.desc:20 " RW_DELAYS

void test_replay_eaton_3s(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        CMD_BEEPER CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low input.transfer.high input.transfer.low outlet.1.desc outlet.desc output.voltage.nominal",
        {}, RW_EATON_3S, RW_EATON_3S);
}

void test_replay_eaton_5p(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.charge.restart battery.protection output.voltage.nominal ups.shutdown ups.start.reboot ups.test.interval",
        /* descriptor only: PowerSummary.DelayBeforeShutdown/Startup */ {}, RW_DELAYS, RW_DELAYS);
}

// The six apc-hid.c rows bound by the Back-UPS descriptors, all read from the replay
#define RW_APC_6 "battery.charge.low:10 battery.mfr.date:10 battery.runtime.low:10 input.sensitivity:10 input.transfer.high:10 input.transfer.low:10"
// The five cps-hid.c rows of the CP1500/CP1600 descriptors, all read from the replay
#define RW_CPS_5 "battery.charge.low:10 battery.runtime.low:10 input.sensitivity:0 input.transfer.high:10 input.transfer.low:10"

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
        checkCs500ShutdownFields, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        // The only replayed report, FEATURE 0x45, carries none of the rows; ups.delay.* from
        // the DelayBeforeShutdown/Startup paths
        {}, RW_DELAYS, RW_DELAYS);
}

void test_replay_apc_rs900mi(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_xs700u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_OFF CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low",
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_smartups750(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json",
        /* no beeper report replayed */ CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.runtime.low",
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_bx1500g_issue55(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_smartups750_issue13(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low",
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_cs750rs_issue48(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* replayed FEATURE 0x35, APCSensitivity = 2 -> apc_sensitivity_info */ { { "input.sensitivity", "high" } },
        // apc-hid.c rows, aux 10: RemainingCapacityLimit FEATURE 0x11 [17, 10]; ManufacturerDate
        // FEATURE 0x20 [32, 104, 87] = 2023/11/08; Battery.RemainingTimeLimit FEATURE 0x24
        // [36, 120, 0]; APCSensitivity FEATURE 0x35 [53, 2]; Input.HighVoltageTransfer FEATURE
        // 0x33 [51, 0, 1] = 256; Input.LowVoltageTransfer FEATURE 0x32 [50, 160, 0] = 160
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

void test_replay_apc_bx750mi_issue60(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* replayed FEATURE 0x35, APCSensitivity = 1 -> apc_sensitivity_info */ { { "input.sensitivity", "medium" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; 0x20 [32, 33, 42] = 2001/01/01; 0x24
        // [36, 120, 0]; 0x35 [53, 1]; 0x33 [51, 39, 1] = 295; 0x32 [50, 145, 0] = 145
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

void test_replay_apc_es700g_issue71(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json",
        CMD_BEEPER CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* replayed FEATURE 0x35, APCSensitivity = 1 -> apc_sensitivity_info */ { { "input.sensitivity", "medium" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; 0x20 [32, 143, 76] = 2018/04/15; 0x24
        // [36, 120, 0]; 0x35 [53, 1]; 0x33 [51, 10, 1] = 266; 0x32 [50, 180, 0] = 180
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

void test_replay_apc_smartups750_issue78(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_FULL " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.mfr.date battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* replayed INPUT 0x22, APCSensitivity = 2 -> apc_sensitivity_info */ { { "input.sensitivity", "high" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; INPUT 0x15 [21, 63, 86] = 2023/01/31;
        // INPUT 0x1A [26, 120, 0] (Battery.RemainingTimeLimit); INPUT 0x22 [34, 2]; INPUT 0x23
        // [35, 253, 0] (Output.HighVoltageTransfer, 409-410); INPUT 0x24 [36, 208, 0]. Both
        // DelayBeforeShutdown and DelayBeforeStartup paths
        RW_APC_6 " " RW_DELAYS, RW_APC_6 " " RW_DELAYS);
}

// US-087: Legrand Keor PDU 800 from the #90 dump. legrand-hid.c: beeper.enable 2 /
// beeper.disable 3 (no beeper.mute), test.battery.* on UPS.Output.Test, load.* and
// shutdown.* on UPS.Output.DelayBefore*, no RW rows besides ups.delay.*
void test_replay_legrand_keor_pdu800_issue90(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json",
        "beeper.enable beeper.disable beeper.toggle test.battery.start.quick test.battery.start.deep test.battery.stop "
        "load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff shutdown.stop shutdown.default", nullptr, /* no NUT table rows */ "",
        { { "ups.mfr", "Legrand" }, { "ups.beeper.status", "enabled" }, { "battery.voltage", "13.70" },
          { "battery.voltage.nominal", "12" }, { "ups.delay.shutdown", "20" }, { "ups.delay.start", "30" },
          /* FEATURE 0x17 [23, 0]: UPS.Output.Overload */ { "ups.status.overload", "0" },
          /* FEATURE 0x07 byte 4: PowerSummary.WarningCapacityLimit */ { "battery.charge.warning", "20" } },
        RW_DELAYS, RW_DELAYS);
}

void test_replay_powercom_spd750u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json",
        /* PowercomDriver: no shutdown commands (US-058) */ "test.battery.start.quick test.battery.start.deep test.battery.stop", nullptr, /* NUT table rows (US-066) */ "",
        /* powercom-hid.c:620-637: ups.delay.start "60", ups.delay.shutdown DEFAULT_OFFDELAY */
        { { "ups.delay.start", "60" }, { "ups.delay.shutdown", "20" } },
        /* no table rows; powercom-hid.c:620-639 delays with aux 8 */ "ups.delay.shutdown:8 ups.delay.start:8", "ups.delay.shutdown:8 ups.delay.start:8");
}

// Descriptor only, no scenarios: checks the NUT table rows (US-066)
void test_replay_cyberpower_br700elcd(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json", nullptr, nullptr,
        "battery.charge.low battery.runtime.low input.transfer.high input.transfer.low",
        /* Output.DelayBeforeShutdown/Startup */ {}, RW_DELAYS, RW_DELAYS);
}

void test_replay_cyberpower_cp1350c(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.runtime.low input.transfer.high input.transfer.low",
        // cps-hid.c rows, aux 10: RemainingCapacityLimit FEATURE 0x07 byte 4 [7, 100, 5, 10, ...]
        // = 10; RemainingTimeLimit INPUT 0x08 bytes 4-5 [.., 44, 1] = 300; Input.Low/High
        // VoltageTransfer FEATURE 0x10 [16, 90, 140] (8 bits each)
        // cps_battvolt (issue #43): PID 0x0501, 20.8 V on a 12 V battery is above 1.4 x
        // nominal, so 2/3 of it. Timers as the device reports them (0x0800, 0x0353)
        { { "battery.voltage", "13.9" }, { "battery.charge.warning", "20" }, { "battery.status", "100%" },
          { "ups.timer.shutdown", "2048" }, { "ups.timer.start", "851" }, { "ups.realpower", "0" } },
        "battery.charge.low:10 battery.runtime.low:10 input.transfer.high:10 input.transfer.low:10 " RW_DELAYS,
        "battery.charge.low:10 battery.runtime.low:10 input.transfer.high:10 input.transfer.low:10 " RW_DELAYS);
}

void test_replay_cyberpower_cp1500epfclcd(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* replayed FEATURE 0x1A, CPSInputSensitivity = 2 -> cps_sensitivity_info */ { { "input.sensitivity", "high" },
          // issue #43: FEATURE 0x07 [7, 100, 5, 10, 20, 10, 100] FullChargeCapacity / WarningCapacityLimit,
          // FEATURE 0x15/0x16 [.., 255, 255] = -1, battery.voltage "%.1f" as cps_battvolt
          { "battery.charge.warning", "20" }, { "battery.status", "100%" }, { "battery.voltage", "24.0" },
          { "ups.timer.shutdown", "-1" }, { "ups.timer.start", "-1" } },
        // cps-hid.c rows: FEATURE 0x07 byte 4 = 10; FEATURE 0x08 bytes 4-5 [.., 44, 1] = 300;
        // FEATURE 0x10 [16, 180, 0, 4, 1] = 180 / 260 (Input.*VoltageTransfer); FEATURE 0x1A
        // [26, 2]. input.sensitivity has info_len 0 upstream (cps-hid.c:416)
        RW_CPS_5 " " RW_DELAYS, RW_CPS_5 " " RW_DELAYS);
}

void test_replay_cyberpower_cp1600epfclcd_issue48(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json", nullptr, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.runtime.low input.sensitivity input.transfer.high input.transfer.low",
        /* replayed FEATURE 0x1A, CPSInputSensitivity = 1 -> cps_sensitivity_info */ { { "input.sensitivity", "normal" },
          // issue #43: Output.ConfigApparentPower (FEATURE 0x18 [24, 232, 3, 64, 6] = 1600), and
          // ups.realpower measured by Output.ActivePower (INPUT 0x19 [25, 153, 0], the last
          // replayed), never replaced by the estimate (16% of 1000 W = 160)
          { "ups.power.nominal", "1600" }, { "ups.realpower", "153" }, { "battery.voltage", "27.2" },
          { "battery.charge.warning", "20" }, { "ups.timer.shutdown", "-1" }, { "ups.timer.start", "-1" } },
        // cps-hid.c rows: FEATURE 0x07 byte 4 = 10; INPUT 0x08 bytes 4-5 [.., 44, 1] = 300;
        // FEATURE 0x10 [16, 195, 0, 1, 1] = 195 / 257 (Output.*VoltageTransfer, 414-415: no
        // Input.*VoltageTransfer in this descriptor); FEATURE 0x1A [26, 1]
        RW_CPS_5 " " RW_DELAYS, RW_CPS_5 " " RW_DELAYS);
}

void test_replay_cyberpower_ut850eg_discussion95(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_ut850eg_vid0764_pid0501_discussion95.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066) */ "battery.charge.low battery.runtime.low input.transfer.high input.transfer.low",
        // issue #43: FEATURE 0x2A [42, 245, 1] Input.Frequency = 501, ten times 50 Hz ->
        // cps_adjust_frequency_scale 0.1 (v1.8.0 served 501.0); battery.voltage "%.1f" as
        // cps_battvolt. No UPS.Battery collection, so no battery.mfr.date row (discussion #95)
        { { "input.frequency", "50.1" }, { "battery.voltage", "13.2" }, { "battery.charge.warning", "20" } },
        // cps-hid.c rows: FEATURE 0x07 byte 4 = 10; INPUT 0x82 [130, 44, 1] = 300.
        // Input.*VoltageTransfer (FEATURE 0x83/0x84) is bound but not in the dump: not listed
        "battery.charge.low:10 battery.runtime.low:10 " RW_DELAYS,
        "battery.charge.low:10 battery.runtime.low:10 " RW_DELAYS);
}

// US-097: liebert-hid.c commands only (test.battery.start, beeper.toggle, load.*.delay,
// shutdown.stop and what usbhid-ups derives), shutdown.default (US-058). liebert_hid2nut
// has no RW row besides ups.delay.* (PowerSummary.DelayBefore*, aux 10)
void test_replay_phoenixtec_powerwalker_vi2200shl_issue96(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json",
        "beeper.toggle test.battery.start load.off load.on load.off.delay load.on.delay "
        "shutdown.return shutdown.stayoff shutdown.stop shutdown.default",
        nullptr, /* no hid2nut() table: ups.delay.* only */ "",
        // FEATURE 0x72 [114, 230] UPS.Flow.[4].ConfigVoltage, FEATURE 0x36 [54, 50] ConfigFrequency
        { { "output.voltage.nominal", "230" }, { "output.frequency.nominal", "50" } },
        RW_DELAYS, RW_DELAYS);
}

// The W150 reports PresentStatus.BatteryPresent = 0 in its normal state, but
// OpenUPSDriver ignores the unreliable flag, so ups.status stays OL; see the
// fixture "notes" (US-055).
void test_replay_openups_wallecube_w150(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json",
        "", nullptr, /* NUT table rows (US-066) */ "",
        /* openups-hid.c has no RW rows and no ups.delay.* */ {}, "", "");
}

// Write/read-back cycles of SET VAR on the reports of the fixtures (US-067). Raw value and
// bytes from the field of the descriptor (report ID, offset, size, exponent) and the last
// replayed FEATURE report with that ID, the field rewritten.

// mge-hid.c:2013 UPS.PowerSummary.RemainingCapacityLimitSetting (0xFFFF004D): FEATURE 0x22,
// bits 0-7, logical 0..255, no unit. Last report [34, 20]
void test_set_cycle_eaton_3s_charge_low(void) {
    FixtureReplayRunner::runSetCycle("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        "battery.charge.low", "30", 30, { 34, 30 }, "30");
}

// apc-hid.c:335 UPS.PowerSummary.RemainingCapacityLimit: FEATURE 0x11, bits 0-7, logical
// 1..100. Last report [17, 10]
void test_set_cycle_apc_cs750rs_charge_low(void) {
    FixtureReplayRunner::runSetCycle("test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
        "battery.charge.low", "25", 25, { 17, 25 }, "25");
}

// apc-hid.c:347 UPS.Battery.ManufacturerDate, date_conversion: FEATURE 0x20, bits 0-15.
// 2024/03/15 = (44 << 9) + (3 << 5) + 15 = 22639 = 0x586F. Last report [32, 104, 87]
void test_set_cycle_apc_cs750rs_mfr_date(void) {
    FixtureReplayRunner::runSetCycle("test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
        "battery.mfr.date", "2024/03/15", 22639, { 32, 0x6F, 0x58 }, "2024/03/15");
}

// cps-hid.c:414 UPS.Output.LowVoltageTransfer (the descriptor has no Input.LowVoltageTransfer):
// FEATURE 0x10, bits 0-15, logical 189..207, volt with exponent 7 (net 0). Last report
// [16, 195, 0, 1, 1]: input.transfer.high (bits 16-31) stays 257
void test_set_cycle_cp1600_transfer_low(void) {
    FixtureReplayRunner::runSetCycle("test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json",
        "input.transfer.low", "200", 200, { 16, 200, 0, 1, 1 }, "200");
}

// apc-hid.c:414 UPS.Input.APCSensitivity (0xFF860061), apc_sensitivity_info "high" = 2:
// FEATURE 0x35, bits 0-7, logical 0..2, declared as volt with exponent 7 (net 0). Last
// report [53, 1] (medium)
void test_set_cycle_apc_bx750mi_sensitivity(void) {
    FixtureReplayRunner::runSetCycle("test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json",
        "input.sensitivity", "high", 2, { 53, 2 }, "high");
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
    RUN_TEST(test_replay_apc_smartups750_issue78);
    RUN_TEST(test_replay_powercom_spd750u);
    RUN_TEST(test_replay_legrand_keor_pdu800_issue90);
    RUN_TEST(test_replay_cyberpower_br700elcd);
    RUN_TEST(test_replay_cyberpower_cp1350c);
    RUN_TEST(test_replay_cyberpower_cp1500epfclcd);
    RUN_TEST(test_replay_cyberpower_cp1600epfclcd_issue48);
    RUN_TEST(test_replay_cyberpower_ut850eg_discussion95);
    RUN_TEST(test_replay_phoenixtec_powerwalker_vi2200shl_issue96);
    RUN_TEST(test_replay_openups_wallecube_w150);
    RUN_TEST(test_set_cycle_eaton_3s_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_mfr_date);
    RUN_TEST(test_set_cycle_cp1600_transfer_low);
    RUN_TEST(test_set_cycle_apc_bx750mi_sensitivity);
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
    RUN_TEST(test_replay_apc_smartups750_issue78);
    RUN_TEST(test_replay_powercom_spd750u);
    RUN_TEST(test_replay_legrand_keor_pdu800_issue90);
    RUN_TEST(test_replay_cyberpower_br700elcd);
    RUN_TEST(test_replay_cyberpower_cp1350c);
    RUN_TEST(test_replay_cyberpower_cp1500epfclcd);
    RUN_TEST(test_replay_cyberpower_cp1600epfclcd_issue48);
    RUN_TEST(test_replay_cyberpower_ut850eg_discussion95);
    RUN_TEST(test_replay_phoenixtec_powerwalker_vi2200shl_issue96);
    RUN_TEST(test_replay_openups_wallecube_w150);
    RUN_TEST(test_set_cycle_eaton_3s_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_mfr_date);
    RUN_TEST(test_set_cycle_cp1600_transfer_low);
    RUN_TEST(test_set_cycle_apc_bx750mi_sensitivity);
    UNITY_END();
}
void loop() {}
#endif
#endif

