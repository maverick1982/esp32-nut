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

// mgeHid2Nut() rows bound by the Eaton 3S descriptor (US-073), derived from its usages and
// mge-hid.c, beyond the RW rows above. Read-only: battery.charge (2012, RemainingCapacity,
// FEATURE 0x06), battery.charge.low (2014, RemainingCapacityLimit, 0x08, dropped: 2013 wins),
// battery.runtime (2017, RunTimeToEmpty, 0x06), battery.type (2022, iDeviceChemistry, 0x10),
// ups.firmware (2063, PowerSummary.iVersion 0xFFFF00F1, 0x10), device.usb.version (2007,
// System.USB.iVersion 0xFFFF00E5.0xFFFF0005.0xFFFF00F1, 0x10), ups.load (2064, PercentLoad,
// 0x07), ups.timer.start/shutdown (2068/2069, DelayBeforeStartup/Shutdown, 0x0A/0x09),
// battery.charger.status (2052, Charger.Status 0xFFFF00E9, 0x07). Internal: device.country
// (2006, Country 0xFFFF0095, 0x15), battery.charger.type.status (2048, Charger.Status). BOOL:
// ACPresent (2106), Discharging (2113), Charging (2114), Charger.Status (2117),
// BelowRemainingCapacityLimit (2119), Overload (2125), NeedReplacement (2126), Good (2133),
// Charger.PresentStatus.VoltageTooHigh/Low 0xFFFF0074/75 (2144/2148), InternalFailure (2150),
// ShutdownImminent (2152), all in FEATURE 0x01 but the Charger ones (0x03).
//
// Differences US-073 (3S fixture): ups.timer.start added (-1). With the strings, on the
// device: ups.firmware (index 6) and device.usb.version (index 8) added; battery.type (index
// 5) from the table instead of the hand mapping. Internal keys: ups.status.good became
// ups.status.off (inverted), ups.status.internal_failure became ups.status.commfault,
// ups.status.comm_lost is gone (mge-hid reads no CommunicationLost), battvolthi/battvoltlo
// added. battery.charger.status stays unpublished: no ABMEnable, ABM unknown. ups.status
// stays "OL"; ups.status.charging becomes 0 (it was 1): Charging is 1, but every walk applies
// eaton_abm_check_chrg_fun again, and once battery.charge (0x06) is 100 it gives "!chrg".
//
// US-074 rows bound by the 3S, derived the same way: ups.beeper.status (2075-2076, Battery and
// PowerSummary AudibleAlarmControl, FEATURE 0x20/0x1F), ups.power.nominal and ups.realpower
// (2083, 2087: Flow.[4].ConfigApparentPower, 0x0D), ups.type (2103, ConverterType 0xFFFF0041,
// 0x0C), output.voltage (2225, 0x0E), output.frequency.nominal (2240, Flow.[4], 0x0D),
// outlet.id / outlet.switchable (2246, 2248: Outlet.[1], 0x0B / 0x0C), outlet.1.id,
// outlet.1.switchable, outlet.1.status, outlet.1.protect.status (2260, 2262, 2264, 2265:
// Outlet.[2], 0x0B / 0x19 / 0x02 / 0x07; 2268 on ACPresent too), and the command rows of
// AudibleAlarmControl (beeper.off/on/mute/disable/enable, 2333-2342) and
// DelayBeforeShutdown/Startup (load.off.delay, load.on.delay, shutdown.stop, 2329-2331).
//
// Differences US-074 (3S fixture): outlet.1.switch and outlet.2.switch become outlet.1.status
// "on" (mge-hid.c:2264, ADR 0011 rule 2); input.voltage.nominal and input.frequency.nominal are
// gone (upstream reads them from Flow.[1]; the 3S has Flow.[4] only, the output: rule 2);
// ups.realpower 21 -> 28 (eaton_compute_realpower_info, powerfactor 0.80 instead of the 0.60
// estimate); added outlet.id "0", outlet.switchable "yes", outlet.1.id "1", outlet.1.switchable
// "no", outlet.1.protect.status "not protected", ups.type "offline / line interactive" (the old
// hand mapping looked for "UPS.PowerConverter.ConverterType", a name HIDParser never gives to
// the vendor usage 0xFFFF0041: ups.type was never published). Same output.voltage,
// output.frequency.nominal, ups.power.nominal, ups.beeper.status, LIST CMD and LIST RW.
//
// US-107 rows bound by the 3S: the vendor ups.alarm row of Output.PresentStatus.ShortCircuit
// (mge-hid.c:2160, 0xFFFF004A, FEATURE 0x03). Differences US-107 (every fixture, see
// test_status_baseline_all_fixtures): none. No fixture has a key of US-107 set (trim, boost,
// bypass*, off, cal, vrange, frange, the ups_alarm_set() bits, a vendor ups.alarm row) nor
// LB+CHRG or RB+OVER together: ups.status, ups.alarm (absent) and input.transfer.reason
// (absent) are the same as before. The new behavior is in test_eaton_3s_* below.
#define NUT_ROWS_EATON_3S "BOOL battery.charge battery.charge.low battery.charger.status battery.charger.type.status " \
    "battery.runtime battery.type beeper.disable beeper.enable beeper.mute beeper.off beeper.on device.country " \
    "device.usb.version input.transfer.high input.transfer.low load.off.delay load.on.delay outlet.1.desc outlet.1.id " \
    "outlet.1.protect.status outlet.1.status outlet.1.switchable outlet.desc outlet.id outlet.switchable " \
    "output.frequency.nominal output.voltage output.voltage.nominal shutdown.stop ups.alarm ups.beeper.status ups.firmware " \
    "ups.load ups.power.nominal ups.realpower ups.timer.shutdown ups.timer.start ups.type"

void test_replay_eaton_3s(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        CMD_BEEPER CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-073, US-074, US-107) */ NUT_ROWS_EATON_3S,
        { { "ups.timer.start", "-1" }, { "ups.timer.shutdown", "-1" }, { "battery.runtime", "3360" },
          { "ups.load", "5" }, { "ups.status.ac_present", "1" }, { "ups.status.off", "0" },
          { "ups.status.commfault", "0" }, { "ups.status.battvolthi", "0" }, { "ups.status.battvoltlo", "0" },
          // US-074
          { "outlet.id", "0" }, { "outlet.switchable", "yes" }, { "outlet.1.id", "1" }, { "outlet.1.switchable", "no" },
          { "outlet.1.protect.status", "not protected" }, { "ups.type", "offline / line interactive" } },
        RW_EATON_3S, RW_EATON_3S);
}

// mgeHid2Nut() rows bound by the Eaton 5P descriptor (US-073), beyond the RW rows: the 3S
// ones but device.usb.version (no System.USB.iVersion), plus battery.capacity (2016,
// BatterySystem.Battery.DesignCapacity), battery.voltage (2024, PowerSummary.Voltage) and
// battery.voltage.nominal (2026, PowerSummary.ConfigVoltage), only for the 5P series,
// battery.charger.abm.status (2040, ABMEnable 0xFFFF0087), battery.charger.mode.status (2047)
// and battery.charger.status (2051) on Charger.Mode 0xFFFF0094, battery.charger.type (2059,
// ChargerType 0xFFFF006B), ups.test.result (2071, BatterySystem.Battery.Test), and the BOOL
// rows of Charger.Mode (2116), FanFailure 0xFFFF0077 (2141), Battery.PresentStatus.Present
// (2142), Charger.PresentStatus.InternalFailure (2143) and OverTemperature (2151).
//
// Differences US-073 (5P, on the device): battery.capacity with two decimals (Ah),
// battery.capacity.full no longer published (ADR 0011 rule 2), battery.voltage and
// battery.voltage.nominal only for the 5P/9E/Pulsar M/Evolution series, battery.charger.status
// and battery.charger.type added with ABM, RB when Battery.PresentStatus.Present is 0.
//
// US-074 rows bound by the 5P: input.voltage.nominal / input.frequency.nominal (2170, 2177:
// Flow.[1]), ups.efficiency (2062, 0xFFFF0086), ups.power (2079), ups.realpower (2084
// ActivePower, 2087), ups.power.nominal / ups.realpower.nominal (2083, 2091: Flow.[4]),
// ups.type (2103), ups.beeper.status (2076, PowerSummary.AudibleAlarmControl), output.voltage,
// output.current, output.frequency, output.frequency.nominal, output.powerfactor (2225, 2233,
// 2238, 2240, 2241: PowerFactor 0xFFFF0069), outlet.1.status (2268, ACPresent: no
// OutletSystem), and the command rows of Battery.Test, AudibleAlarmControl and
// DelayBeforeShutdown/Startup. Descriptor only: no value to compare.
//
// US-107 rows bound by the 5P: the vendor ups.alarm rows of PresentStatus.EmergencyStop and
// ConfigurationFailure (mge-hid.c:2155, 2157: 0xFFFF0068, 0xFFFF002D, FEATURE 0x03).
#define NUT_ROWS_EATON_5P "BOOL battery.capacity battery.charge battery.charge.low battery.charge.restart " \
    "battery.charger.abm.status battery.charger.mode.status battery.charger.status battery.charger.type " \
    "battery.charger.type.status battery.protection battery.runtime battery.type battery.voltage " \
    "battery.voltage.nominal beeper.disable beeper.enable beeper.mute beeper.off beeper.on device.country " \
    "input.frequency.nominal input.voltage.nominal load.off.delay load.on.delay outlet.1.status output.current " \
    "output.frequency output.frequency.nominal output.powerfactor output.voltage output.voltage.nominal " \
    "shutdown.stop test.battery.start.deep test.battery.start.quick test.battery.stop ups.alarm ups.beeper.status " \
    "ups.efficiency ups.firmware ups.load ups.power ups.power.nominal ups.realpower ups.realpower.nominal " \
    "ups.shutdown ups.start.reboot ups.test.interval ups.test.result ups.timer.shutdown ups.timer.start ups.type"

void test_replay_eaton_5p(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-073, US-074, US-107) */ NUT_ROWS_EATON_5P,
        /* descriptor only: PowerSummary.DelayBeforeShutdown/Startup */ {}, RW_DELAYS, RW_DELAYS);
}

// mgeHid2Nut() rows bound by the Eaton Ellipse PRO 650 descriptor (issue #110), derived the same
// way: the 3S rows but device.usb.version, battery.charger.*, outlet.1.protect.status and ups.type,
// plus battery.voltage (2024, PowerSummary.Voltage: mge_battery_voltage, published only for the
// Evolution, Pulsar M, 5P and 9E series), input.voltage / input.frequency (PowerConverter.Input.[1]),
// input.voltage.extended (0xFFFF004B), output.frequency, ups.power / ups.realpower
// (PowerConverter.Output.ApparentPower / ActivePower), outlet.power (2252, pegasus_threshold_info)
// and the outlet.2.* / outlet.3.* rows of Outlet.[3] / Outlet.[4]. No UPS.Flow.[1]: no
// input.voltage.nominal / input.frequency.nominal (v1.11.0 published the Flow.[4] values).
#define NUT_ROWS_EATON_ELLIPSE_PRO_650 "BOOL battery.charge battery.charge.low battery.runtime battery.type " \
    "battery.voltage beeper.disable beeper.enable beeper.mute beeper.off beeper.on device.country input.frequency " \
    "input.transfer.high input.transfer.low input.voltage input.voltage.extended load.off.delay load.on.delay " \
    "outlet.1.desc outlet.1.id outlet.1.status outlet.1.switchable outlet.2.desc outlet.2.ecocontrol outlet.2.id " \
    "outlet.2.status outlet.2.switchable outlet.3.desc outlet.3.ecocontrol outlet.3.id outlet.3.status " \
    "outlet.3.switchable outlet.desc outlet.id outlet.power outlet.switchable output.frequency " \
    "output.frequency.nominal output.voltage output.voltage.nominal shutdown.stop ups.alarm ups.beeper.status " \
    "ups.firmware ups.load ups.power ups.power.nominal ups.realpower ups.timer.shutdown ups.timer.start"

// mge-hid.c RW rows of the Ellipse PRO: the 3S ones plus input.voltage.extended (2197, aux 5,
// FEATURE 0x1A [26, 0] = no) and outlet.2.desc / outlet.3.desc (2282, 2303, HU_FLAG_ABSENT, aux 20;
// outlet.3.desc defaults to "PowerShare Outlet 2" upstream too)
#define RW_EATON_ELLIPSE_PRO "battery.charge.low:5 input.transfer.high:5 input.transfer.low:5 input.voltage.extended:5 " \
    "outlet.1.desc:20 outlet.2.desc:20 outlet.3.desc:20 outlet.desc:20 " RW_DELAYS

// Live dump of v1.11.0 attached to issue #110. Differences from the dump, all v1.12.0 changes (see
// the fixture "notes"): outlet.1.switch / outlet.2.switch become outlet.N.status (outlet.1 on
// ACPresent, 2268; outlet.2 / outlet.3 on Outlet.[3] / [4] SwitchOn/Off, FEATURE 0x02 [2, 1, 1, 0, 0]);
// input.*.nominal gone; battery.voltage gone (MGE_DEFAULT: "Ellipse PRO" 650 is not in
// mge_model_names, mge-hid.c:714-730); ups.status.charging 0 (eaton_abm_check_chrg_fun,
// mge-hid.c:556-576: Charging 1 but battery.charge 100, no ABM usage); ups.timer.start and the
// outlet ids / switchable / ecocontrol rows added. battery.type (string), output.voltage.nominal
// and outlet.2.switchable / outlet.3.switchable (mge_type) are decided on the device only.
void test_replay_eaton_ellipse_pro_650_issue110(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_ellipse_pro_650_vid0463_pidffff_issue110.json",
        CMD_BEEPER CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-073, US-074, US-107) */ NUT_ROWS_EATON_ELLIPSE_PRO_650,
        { { "ups.status.charging", "0" }, { "ups.status.off", "0" }, { "ups.status.commfault", "0" },
          { "ups.timer.start", "-1" }, { "outlet.1.status", "on" }, { "outlet.2.status", "on" }, { "outlet.3.status", "off" },
          { "outlet.id", "1" }, { "outlet.switchable", "no" }, { "outlet.1.id", "2" }, { "outlet.1.switchable", "no" },
          { "outlet.2.id", "3" }, { "outlet.3.id", "4" }, { "outlet.2.ecocontrol", "The outlet is ECO controlled" },
          { "outlet.3.ecocontrol", "The outlet is not ECO controlled" } },
        RW_EATON_ELLIPSE_PRO, RW_EATON_ELLIPSE_PRO);
}

// mgeHid2Nut() rows bound by the MGE Protection Center descriptor (issue #111): the 3S rows but
// device.country, device.usb.version, battery.charger.*, outlet.1.protect.status, ups.alarm,
// ups.firmware and ups.type. No UPS.Flow.[1]: no input.*.nominal. LIST RW is the 3S one.
#define NUT_ROWS_MGE_PROTECTION_CENTER "BOOL battery.charge battery.charge.low battery.runtime battery.type " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.transfer.high input.transfer.low " \
    "load.off.delay load.on.delay outlet.1.desc outlet.1.id outlet.1.status outlet.1.switchable outlet.desc " \
    "outlet.id outlet.switchable output.frequency.nominal output.voltage output.voltage.nominal shutdown.stop " \
    "ups.beeper.status ups.load ups.power.nominal ups.realpower ups.timer.shutdown ups.timer.start"

// Live dump of v1.11.0 attached to issue #111. Differences from the dump: ups.beeper.status
// "disabled": mge-hid.c:2075 UPS.BatterySystem.Battery.AudibleAlarmControl (FEATURE 0x20 [32, 1])
// comes before 2076 PowerSummary.AudibleAlarmControl (FEATURE 0x1F [31, 2], "enabled", what
// v1.11.0 served) and the first row wins (usbhid-ups.c:2327-2334), as on the 3S, whose reports are
// the same; ups.status.charging 0 (eaton_abm_check_chrg_fun at 100 %); input.*.nominal gone.
// DelayBeforeStartup (FEATURE 0x0A [10, 255, 255, 255, 255]) has Unit Exponent 1 in this
// descriptor: ups.timer.start is -10, as libhid.c:544 gives it (DelayBeforeShutdown has exponent 0)
void test_replay_mge_protection_center_issue111(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/mge_protection_center_vid0463_pidffff_issue111.json",
        CMD_BEEPER CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-073, US-074, US-107) */ NUT_ROWS_MGE_PROTECTION_CENTER,
        { { "ups.beeper.status", "disabled" }, { "ups.status.charging", "0" }, { "ups.status.off", "0" },
          { "ups.status.commfault", "0" }, { "ups.timer.start", "-10" }, { "outlet.1.status", "on" },
          { "outlet.id", "1" }, { "outlet.switchable", "no" }, { "outlet.1.id", "2" }, { "outlet.1.switchable", "no" } },
        RW_EATON_3S, RW_EATON_3S);
}

// mgeHid2Nut() rows bound by the Eaton Ellipse ECO 650 descriptor (issue #112): the 3S rows but
// device.usb.version, battery.charger.*, outlet.1.protect.status and ups.type, plus outlet.power
// (2252), outlet.1.ecocontrol and the outlet.2.* rows of Outlet.[3]. No UPS.Flow.[1].
#define NUT_ROWS_EATON_ELLIPSE_ECO_650 "BOOL battery.charge battery.charge.low battery.runtime battery.type " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on device.country input.transfer.high " \
    "input.transfer.low load.off.delay load.on.delay outlet.1.desc outlet.1.ecocontrol outlet.1.id outlet.1.status " \
    "outlet.1.switchable outlet.2.desc outlet.2.ecocontrol outlet.2.id outlet.2.status outlet.2.switchable " \
    "outlet.desc outlet.id outlet.power outlet.switchable output.frequency.nominal output.voltage " \
    "output.voltage.nominal shutdown.stop ups.alarm ups.beeper.status ups.firmware ups.load ups.power.nominal " \
    "ups.realpower ups.timer.shutdown ups.timer.start"

// The 3S RW rows plus outlet.2.desc (2282, HU_FLAG_ABSENT, aux 20). On the device, where mge_type
// is MGE_PEGASUS ("Ellipse ECO" 650), outlet.power (2252, aux 6) and outlet.2.switchable (2285,
// aux 3) are RW too: the replay never learns mge_type, so they stay undecided
#define RW_EATON_ELLIPSE_ECO "battery.charge.low:5 input.transfer.high:5 input.transfer.low:5 outlet.1.desc:20 " \
    "outlet.2.desc:20 outlet.desc:20 " RW_DELAYS

// Live dump of v1.11.0 attached to issue #112. Differences from the dump: outlet.N.switch become
// outlet.N.status (Outlet.[2] / [3] SwitchOn/Off, FEATURE 0x02 [2, 1, 1, 0, 1]); input.*.nominal
// gone; outlet.desc / outlet.1.desc / outlet.2.desc are the mge-hid defaults (2247, 2261, 2282),
// the dump had names the user set; outlet.power and outlet.2.switchable wait for mge_type.
// ups.status OL CHRG: charge 80 %, so eaton_abm_check_chrg_fun keeps "chrg"
void test_replay_eaton_ellipse_eco_650_issue112(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/eaton/eaton_ellipse_eco_650_vid0463_pidffff_issue112.json",
        CMD_BEEPER CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-073, US-074, US-107) */ NUT_ROWS_EATON_ELLIPSE_ECO_650,
        { { "battery.runtime", "1725" }, { "ups.status.charging", "1" }, { "ups.status.off", "0" },
          { "ups.status.commfault", "0" }, { "ups.timer.start", "-1" }, { "outlet.1.status", "on" },
          { "outlet.2.status", "on" }, { "outlet.id", "1" }, { "outlet.switchable", "no" }, { "outlet.1.id", "2" },
          { "outlet.1.switchable", "no" }, { "outlet.2.id", "3" },
          { "outlet.1.ecocontrol", "The outlet is ECO controlled" }, { "outlet.2.ecocontrol", "The outlet is not ECO controlled" } },
        RW_EATON_ELLIPSE_ECO, RW_EATON_ELLIPSE_ECO);
}

// The six apc-hid.c rows bound by the Back-UPS descriptors, all read from the replay
#define RW_APC_6 "battery.charge.low:10 battery.mfr.date:10 battery.runtime.low:10 input.sensitivity:10 input.transfer.high:10 input.transfer.low:10"
// The five cps-hid.c rows of the CP1500/CP1600 descriptors, all read from the replay
#define RW_CPS_5 "battery.charge.low:10 battery.runtime.low:10 input.sensitivity:0 input.transfer.high:10 input.transfer.low:10"

// apcHid2Nut() rows bound by each APC descriptor (US-076), derived from the parsed usages of the
// descriptor and the paths of apc_hid2nut (apc-hid.c:334-467, vendor names as hex), not from the
// firmware. The ups.delay.* rows are excluded (applyDefaultDelays). The BGM2200 dumps of #98
// (on mains, on battery) share the descriptor of the first one.
// apc-hid.c rows bound by the descriptor: 334 335 337 338 339 340 341 343 346 347 353 358 371 372
// 373 374 376 384 385 386 387 389 390 391 392 396 397 403 404 407 408 412 413 414 435 437 439 446
// 448 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_BGM2200_PID0002_ISSUE98 \
    "BOOL battery.charge battery.charge.low battery.mfr.date battery.runtime battery.runtime.low " \
    "battery.type battery.voltage battery.voltage.nominal beeper.disable beeper.enable beeper.mute " \
    "beeper.off beeper.on input.sensitivity input.transfer.high input.transfer.low input.voltage " \
    "input.voltage.nominal load.off.delay shutdown.reboot shutdown.stop test.battery.start.deep " \
    "test.battery.start.quick test.battery.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 337 338 339 340 341 343 346 347 353 358 371 372
// 373 374 376 384 385 386 387 389 390 391 392 396 397 403 404 407 408 412 413 414 435 437 439 446
// 448 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_BGM2200_PID0002_ISSUE98_ONBATTERY \
    "BOOL battery.charge battery.charge.low battery.mfr.date battery.runtime battery.runtime.low " \
    "battery.type battery.voltage battery.voltage.nominal beeper.disable beeper.enable beeper.mute " \
    "beeper.off beeper.on input.sensitivity input.transfer.high input.transfer.low input.voltage " \
    "input.voltage.nominal load.off.delay shutdown.reboot shutdown.stop test.battery.start.deep " \
    "test.battery.start.quick test.battery.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 337 338 339 340 341 343 346 347 353 358 371 372
// 373 374 376 384 385 386 387 389 390 391 392 396 397 403 404 407 408 412 413 414 435 437 439 446
// 448 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_BGM2200_PID0002_ISSUE98_ONLINE \
    "BOOL battery.charge battery.charge.low battery.mfr.date battery.runtime battery.runtime.low " \
    "battery.type battery.voltage battery.voltage.nominal beeper.disable beeper.enable beeper.mute " \
    "beeper.off beeper.on input.sensitivity input.transfer.high input.transfer.low input.voltage " \
    "input.voltage.nominal load.off.delay shutdown.reboot shutdown.stop test.battery.start.deep " \
    "test.battery.start.quick test.battery.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 346 347 349
// 353 358 370 371 372 373 374 375 376 384 385 386 387 388 389 390 391 392 394 395 396 397 398 399
// 403 404 407 408 412 413 414 435 437 439 440 441 446 448 456 458 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_BX1500G_PID0002_ISSUE55 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.sensitivity " \
    "input.transfer.high input.transfer.low input.voltage input.voltage.nominal load.off.delay " \
    "shutdown.reboot shutdown.stop test.battery.start.deep test.battery.start.quick " \
    "test.battery.stop test.panel.start test.panel.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 337 338 339 340 341 343 346 347 353 358 371 372
// 373 374 376 384 385 386 387 389 390 391 392 396 397 403 404 407 408 412 413 414 435 437 439 446
// 448 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_BX750MI_PID0002_ISSUE60 \
    "BOOL battery.charge battery.charge.low battery.mfr.date battery.runtime battery.runtime.low " \
    "battery.type battery.voltage battery.voltage.nominal beeper.disable beeper.enable beeper.mute " \
    "beeper.off beeper.on input.sensitivity input.transfer.high input.transfer.low input.voltage " \
    "input.voltage.nominal load.off.delay shutdown.reboot shutdown.stop test.battery.start.deep " \
    "test.battery.start.quick test.battery.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 345 346 347
// 349 352 358 359 363 364 365 372 373 374 375 377 384 385 386 387 388 389 390 391 392 403 404 407
// 408 409 410 414 417 418 420 435 437 439 440 441 446 448 449 451 452 453 454 461 463 464 465 466
// 467
#define NUT_ROWS_APC_BACKUPS_CS500_PID0002_ISSUE18 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.temperature battery.type battery.voltage " \
    "battery.voltage.nominal beeper.disable beeper.enable beeper.mute beeper.off beeper.on " \
    "input.sensitivity input.transfer.high input.transfer.low input.voltage input.voltage.nominal " \
    "load.off.delay load.on.delay output.frequency output.voltage output.voltage.nominal " \
    "shutdown.reboot shutdown.return shutdown.stop test.battery.start.deep test.battery.start.quick " \
    "test.battery.stop test.panel.start test.panel.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown ups.timer.start"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 346 347 349
// 353 358 370 371 372 373 374 375 376 384 385 386 387 388 389 390 391 392 394 395 396 397 398 399
// 403 404 407 408 412 413 414 435 437 439 440 441 446 448 456 458 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_CS750RS_PID0002_ISSUE48 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.sensitivity " \
    "input.transfer.high input.transfer.low input.voltage input.voltage.nominal load.off.delay " \
    "shutdown.reboot shutdown.stop test.battery.start.deep test.battery.start.quick " \
    "test.battery.stop test.panel.start test.panel.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 346 347 349
// 353 370 371 373 374 375 384 385 386 387 388 389 390 391 392 399 403 404 407 408 412 413 414 440
// 441 456 458 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_ES700G_PID0002_ISSUE71 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.sensitivity " \
    "input.transfer.high input.transfer.low input.voltage input.voltage.nominal load.off.delay " \
    "shutdown.reboot shutdown.stop test.panel.start test.panel.stop ups.beeper.status ups.load " \
    "ups.mfr.date ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 346 347 349
// 353 358 370 371 372 373 374 375 376 384 385 386 387 388 389 390 391 392 394 395 396 397 398 399
// 403 404 407 408 412 413 414 435 437 439 440 441 446 448 456 458 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_RS900MI_PID0002_ISSUE18 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.sensitivity " \
    "input.transfer.high input.transfer.low input.voltage input.voltage.nominal load.off.delay " \
    "shutdown.reboot shutdown.stop test.battery.start.deep test.battery.start.quick " \
    "test.battery.stop test.panel.start test.panel.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 346 347 349
// 353 358 370 371 372 373 374 375 376 384 385 386 387 388 389 390 391 392 394 395 396 397 398 399
// 403 404 407 408 412 413 414 435 437 439 440 441 446 448 456 458 459 463 464 465 466 467
#define NUT_ROWS_APC_BACKUPS_XS1500M_PID0002_ISSUE94 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.sensitivity " \
    "input.transfer.high input.transfer.low input.voltage input.voltage.nominal load.off.delay " \
    "shutdown.reboot shutdown.stop test.battery.start.deep test.battery.start.quick " \
    "test.battery.stop test.panel.start test.panel.stop ups.beeper.status ups.load ups.mfr.date " \
    "ups.realpower.nominal ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 338 340 342 344 346 347 349 358 372 373 375
// 384 385 386 387 388 389 390 391 392 394 395 396 397 398 399 435 437 439 446 448 463 464 465 466
// 467
#define NUT_ROWS_APC_BACKUPS_XS700U_PID0002_ISSUE20 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.date battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on load.off.delay shutdown.stop " \
    "test.battery.start.deep test.battery.start.quick test.battery.stop ups.beeper.status " \
    "ups.mfr.date ups.test.result ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 338 339 340 342 344 346 347 358 359 372 373
// 375 384 385 386 387 388 389 390 391 392 435 437 439 446 448 449 463 464 465 466 467
#define NUT_ROWS_APC_SMARTUPS_750_PID0002_ISSUE13 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.mfr.date battery.runtime " \
    "battery.runtime.low battery.type battery.voltage battery.voltage.nominal beeper.disable " \
    "beeper.enable beeper.mute beeper.off beeper.on load.off.delay shutdown.reboot shutdown.stop " \
    "test.battery.start.deep test.battery.start.quick test.battery.stop ups.beeper.status " \
    "ups.mfr.date ups.test.result ups.timer.reboot ups.timer.shutdown"
// apc-hid.c rows bound by the descriptor: 334 335 336 337 338 339 340 341 342 343 344 345 346 347
// 352 358 359 363 364 365 372 373 374 375 384 385 386 387 388 389 390 391 392 403 404 407 409 410
// 414 417 418 419 420 435 437 439 440 441 446 448 449 451 452 453 454 463 464 465 466 467
#define NUT_ROWS_APC_SMARTUPS_750_PID0002_ISSUE78 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.mfr.date battery.runtime " \
    "battery.runtime.low battery.temperature battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on input.sensitivity " \
    "input.transfer.high input.transfer.low input.voltage load.off.delay load.on.delay " \
    "output.current output.frequency output.voltage output.voltage.nominal shutdown.reboot " \
    "shutdown.stop test.battery.start.deep test.battery.start.quick test.battery.stop " \
    "test.panel.start test.panel.stop ups.beeper.status ups.load ups.mfr.date ups.test.result " \
    "ups.timer.reboot ups.timer.shutdown ups.timer.start"
// apc-hid.c rows bound by the descriptor: 334 335 336 338 340 342 344 346 358 359 373 375 384 385
// 386 387 388 389 390 391 392 446 448 449 463 464 465 466 467
#define NUT_ROWS_APC_SMARTUPS_750_PID0003_ISSUE22 \
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.runtime " \
    "battery.runtime.low battery.type battery.voltage battery.voltage.nominal beeper.disable " \
    "beeper.enable beeper.mute beeper.off beeper.on load.off.delay shutdown.reboot shutdown.stop " \
    "ups.beeper.status ups.mfr.date ups.timer.reboot ups.timer.shutdown"

// The CS500 declares the APC Back-UPS CS field UPS.Output.0xFF86007C: apc-hid.c:461 writes
// 1 there for shutdown.return only. shutdown.reboot comes from the standard
// DelayBeforeReboot and must never resolve to that field (US-058). Same with the rows of
// apcHid2Nut() and of genericHid2Nut(), which copies the apc-hid.c command rows (US-079).
static void checkCs500ShutdownFields(const std::vector<HIDUsageDef>& usages) {
    size_t n_apc = 0, n_generic = 0;
    const NutTable::HidInfo* apc = NutTable::apcHid2Nut(n_apc);
    const NutTable::HidInfo* generic = NutTable::genericHid2Nut(n_generic);
    const struct { const NutTable::HidInfo* table; size_t n; } tables[] = { { apc, n_apc }, { generic, n_generic } };
    for (const auto& t : tables) {
        std::vector<HidControl::WriteStep> steps;
        TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
            (int)HidControl::resolveShutdownSteps("shutdown.return", nullptr, usages, 20, 30, steps, t.table, t.n));
        TEST_ASSERT_EQUAL(1, steps.size());
        TEST_ASSERT_EQUAL_STRING("UPS.Output.0xFF86007C", steps[0].def.path);
        TEST_ASSERT_EQUAL(1, steps[0].value);
        TEST_ASSERT_EQUAL((int)HidControl::StepsResult::OK,
            (int)HidControl::resolveShutdownSteps("shutdown.reboot", nullptr, usages, 20, 30, steps, t.table, t.n));
        TEST_ASSERT_EQUAL(1, steps.size());
        TEST_ASSERT_EQUAL_STRING("UPS.PowerSummary.DelayBeforeReboot", steps[0].def.path);
        TEST_ASSERT_EQUAL(10, steps[0].value);
    }
}

void test_replay_apc_cs500(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_FULL " shutdown.reboot" CMD_DEFAULT,
        checkCs500ShutdownFields, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_CS500_PID0002_ISSUE18,
        // The only replayed report, FEATURE 0x45, carries none of the rows; ups.delay.* from
        // the DelayBeforeShutdown/Startup paths
        {}, RW_DELAYS, RW_DELAYS);
}

void test_replay_apc_rs900mi(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_RS900MI_PID0002_ISSUE18,
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_xs700u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_OFF CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_XS700U_PID0002_ISSUE20,
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_smartups750(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json",
        /* no beeper report replayed */ CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_SMARTUPS_750_PID0003_ISSUE22,
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

// US-108: the APC 5G (051d:0003, issue #22) has QUIRK_NO_INTERRUPT_PIPE, disable_interrupt_pipe of
// apc-hid.c:51-62: every value is read with a FEATURE GET_REPORT, the INPUT reports are dropped
static const char* const kIssue22 = "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json";

void test_apc_5g_reads_feature_reports_issue22(void) {
    // Poll lists: FEATURE reports only, the INPUT report 0x89 (proprietary protocol) left out
    std::string lists = FixtureReplayRunner::pollLists<APCDriver>(kIssue22);
    TEST_ASSERT_TRUE_MESSAGE(lists.find("/1") == std::string::npos, lists.c_str());
    // US-109: 0x90 (144) is the OUTPUT report of the proprietary protocol, typed 2 and not polled
    TEST_ASSERT_TRUE_MESSAGE(lists.find(" 144/") == std::string::npos, lists.c_str());

    ReplaySession s;
    if (!FixtureReplayRunner::replay(kIssue22, s)) return;
    TEST_ASSERT_TRUE(s.host._quirks & QUIRK_NO_INTERRUPT_PIPE);
    bool output_144 = false;
    for (const auto& u : s.host.getUsages()) {
        if (u.report_id == 0x90) {
            TEST_ASSERT_EQUAL_UINT8(2, u.report_type);
            output_144 = true;
        }
    }
    TEST_ASSERT_TRUE(output_144);
    GenericDriver* drv = static_cast<GenericDriver*>(s.driver);
    // The fixture has only the descriptor: every GET_REPORT is answered with zeros
    s.host._answer_all = true;
    s.host._answer_driver = s.driver;
    s.host._answer_data = &s.ups_data;
    for (uint32_t now = 1; now <= 35000; now += 25) s.driver->loop(&s.host, s.ups_data, now);

    // Bound rows of the thresholds UPS Settings writes
    bool charge_low = false, runtime_low = false;
    for (const auto& b : drv->nutBindings()) {
        if (strcmp(b.info->info_type, "battery.charge.low") == 0) charge_low = true;
        if (strcmp(b.info->info_type, "battery.runtime.low") == 0) runtime_low = true;
    }
    TEST_ASSERT_TRUE(charge_low);
    TEST_ASSERT_TRUE(runtime_low);
    for (const auto& r : s.host._requestedReports) TEST_ASSERT_EQUAL_UINT8(3, r.second);

    std::string rw = FixtureReplayRunner::listRW(s.driver, s.ups_data);
    TEST_ASSERT_TRUE_MESSAGE(rw.find("battery.charge.low:") != std::string::npos, rw.c_str());
    TEST_ASSERT_TRUE_MESSAGE(rw.find("battery.runtime.low:") != std::string::npos, rw.c_str());
    TEST_ASSERT_TRUE_MESSAGE(rw.find("ups.delay.shutdown:") != std::string::npos, rw.c_str());
    // LIST CMD: the command rows of apcHid2Nut() the descriptor has (no battery test usage on
    // this Smart-UPS). The zeros answered give AudibleAlarmControl 0, outside beeper_info:
    // ups.beeper.status is missing, but the field was read, so beeper.enable / beeper.disable /
    // beeper.mute are offered and beeper.toggle, which writes from the state, is not (US-109)
    TEST_ASSERT_FALSE(s.ups_data.hasKey("ups.beeper.status"));
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.mute " CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT,
                             FixtureReplayRunner::listCmd(s).c_str());
}

// An INPUT report reaching the replay of a 5G (RemainingCapacity, report 0x0C) is dropped by the
// runner's quirk check, the same as USBHostUPS::processInputReport(): the replay applies the quirk
void test_apc_5g_input_report_ignored_issue22(void) {
    ReplaySession s;
    if (!FixtureReplayRunner::replay(kIssue22, s)) return;
    const HIDUsageDef* charge = nullptr;
    for (const auto& u : s.host.getUsages()) {
        if (u.report_type == 1 && strcmp(u.path, "UPS.PowerSummary.RemainingCapacity") == 0) charge = &u;
    }
    TEST_ASSERT_NOT_NULL(charge);
    TEST_ASSERT_EQUAL_UINT8(0x0C, charge->report_id);
    std::vector<uint8_t> report(s.host._parser.getExpectedLength(0x0C, 1), 0x50);
    TEST_ASSERT_TRUE(report.size() >= 2);
    report[0] = 0x0C;
    TEST_ASSERT_FALSE(FixtureReplayRunner::replayReport(s, 0x0C, 1, report));
    TEST_ASSERT_FALSE(s.ups_data.hasKey("battery.charge"));
}

// US-109 (#36): the SPD-750U reports AudibleAlarmControl 3 after beeper.toggle, outside
// powercom_beeper_info (1 enabled, 2 disabled). ups.beeper.status is missing, as in NUT, but the
// field was read: beeper.enable / beeper.disable stay offered, beeper.toggle needs the state
static const char* const kSpd750u = "test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json";

static std::string spd750uBeeperCommands(uint8_t value, bool& status, bool& read_before, bool& read_after_setup) {
    ReplaySession s;
    if (!FixtureReplayRunner::replay(kSpd750u, s)) return "";
    read_before = s.driver->beeperFieldRead(&s.host);
    const HIDUsageDef* beeper = nullptr;
    String path = s.host.getActiveBeeperPath();
    for (const auto& u : s.host.getUsages()) {
        if (u.report_type == 3 && path == u.path) { beeper = &u; break; }
    }
    TEST_ASSERT_NOT_NULL(beeper);
    TEST_ASSERT_EQUAL(0, beeper->bit_offset % 8);
    std::vector<uint8_t> report(s.host._parser.getExpectedLength(beeper->report_id, 3), 0);
    report[0] = beeper->report_id;
    report[1 + beeper->bit_offset / 8] = value;
    TEST_ASSERT_TRUE(FixtureReplayRunner::replayReport(s, beeper->report_id, 3, report));
    status = s.ups_data.hasKey("ups.beeper.status");
    std::string cmds = FixtureReplayRunner::listCmd(s);
    s.driver->setup();
    read_after_setup = s.driver->beeperFieldRead(&s.host);
    return cmds;
}

void test_spd750u_beeper_value_outside_table_keeps_commands_issue36(void) {
    bool status = true, before = true, after = true;
    std::string cmds = spd750uBeeperCommands(3, status, before, after);
    TEST_ASSERT_FALSE(before);
    TEST_ASSERT_FALSE(status);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable test.battery.start.quick", cmds.c_str());
    TEST_ASSERT_FALSE(after); // a new device forgets the read
}

void test_spd750u_beeper_disabled_offers_toggle(void) {
    bool status = false, before = true, after = true;
    std::string cmds = spd750uBeeperCommands(2, status, before, after);
    TEST_ASSERT_TRUE(status);
    TEST_ASSERT_EQUAL_STRING("beeper.enable beeper.disable beeper.toggle test.battery.start.quick", cmds.c_str());
}

void test_replay_apc_bx1500g_issue55(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_BX1500G_PID0002_ISSUE55,
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_smartups750_issue13(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json",
        /* no beeper report replayed */ CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_SMARTUPS_750_PID0002_ISSUE13,
        /* descriptor only, no DelayBeforeStartup path */ {}, RW_DELAY_OFF, RW_DELAY_OFF);
}

void test_replay_apc_cs750rs_issue48(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_CS750RS_PID0002_ISSUE48,
        /* replayed FEATURE 0x35, APCSensitivity = 2 -> apc_sensitivity_info */ { { "input.sensitivity", "high" } },
        // apc-hid.c rows, aux 10: RemainingCapacityLimit FEATURE 0x11 [17, 10]; ManufacturerDate
        // FEATURE 0x20 [32, 104, 87] = 2023/11/08; Battery.RemainingTimeLimit FEATURE 0x24
        // [36, 120, 0]; APCSensitivity FEATURE 0x35 [53, 2]; Input.HighVoltageTransfer FEATURE
        // 0x33 [51, 0, 1] = 256; Input.LowVoltageTransfer FEATURE 0x32 [50, 160, 0] = 160
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

void test_replay_apc_bx750mi_issue60(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_BX750MI_PID0002_ISSUE60,
        /* replayed FEATURE 0x35, APCSensitivity = 1 -> apc_sensitivity_info */ { { "input.sensitivity", "medium" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; 0x20 [32, 33, 42] = 2001/01/01; 0x24
        // [36, 120, 0]; 0x35 [53, 1]; 0x33 [51, 39, 1] = 295; 0x32 [50, 145, 0] = 145
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

// Live dump of v1.12.0-rc1 attached to issue #94: every value of the dump but the string ones
// (battery.type) is the same on main
void test_replay_apc_xs1500m_issue94(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_xs1500m_vid051d_pid0002_issue94.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_XS1500M_PID0002_ISSUE94,
        /* replayed FEATURE 0x35, APCSensitivity = 1 -> apc_sensitivity_info */ { { "input.sensitivity", "medium" },
          // apc-hid.c:337 UPS.Battery.RunTimeToEmpty, FEATURE 0x23 [35, 162, 6], before the
          // PowerSummary one (338, INPUT/FEATURE 0x0C); ups.realpower estimated, 24 % of 900 W
          { "battery.runtime", "1698" }, { "ups.realpower", "216" }, { "battery.voltage", "27.3" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; 0x20 [32, 129, 91] = 2025/12/01; 0x24
        // [36, 120, 0]; 0x35 [53, 1]; 0x33 [51, 139, 0] = 139; 0x32 [50, 88, 0] = 88
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

void test_replay_apc_es700g_issue71(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json",
        CMD_BEEPER CMD_PANEL_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_ES700G_PID0002_ISSUE71,
        /* replayed FEATURE 0x35, APCSensitivity = 1 -> apc_sensitivity_info */ { { "input.sensitivity", "medium" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; 0x20 [32, 143, 76] = 2018/04/15; 0x24
        // [36, 120, 0]; 0x35 [53, 1]; 0x33 [51, 10, 1] = 266; 0x32 [50, 180, 0] = 180
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

void test_replay_apc_bgm2200_issue98(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_OFF " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_BACKUPS_BGM2200_PID0002_ISSUE98,
        // US-098: fields of reports 0x20..0x52 declared with Report Count 23, read from the
        // first one as NUT: FEATURE 0x50 [80, 28] PercentLoad, 0x25 [37, 96, 9] ConfigVoltage
        // = 24.0 V, 0x30 [48, 230], 0x52 [82, 40, 5] ConfigActivePower = 1320, 0x23 [35, 42, 3]
        { { "ups.load", "28" }, { "battery.voltage.nominal", "24.0" }, { "input.voltage.nominal", "230" },
          { "ups.realpower.nominal", "1320" }, { "ups.realpower", "369" }, { "battery.runtime", "810" } },
        RW_APC_6 " " RW_DELAY_OFF, RW_APC_6 " " RW_DELAY_OFF);
}

// US-101: second dump of #98, UPS on mains with ACPresent and Discharging both set at
// 100 %: the fixture expects ups.status OL DISCHRG, as usbhid-ups with its defaults
void test_replay_apc_bgm2200_issue98_online(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_online.json");
}

// US-101: power loss test of #98 (issue98-v2), mains removed: ACPresent clear and
// Discharging set, ups.status OB DISCHRG as usbhid-ups
void test_replay_apc_bgm2200_issue98_onbattery(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_onbattery.json");
}

void test_replay_apc_smartups750_issue78(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_PANEL_TEST CMD_LOAD_FULL " shutdown.reboot" CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-076) */ NUT_ROWS_APC_SMARTUPS_750_PID0002_ISSUE78,
        /* replayed INPUT 0x22, APCSensitivity = 2 -> apc_sensitivity_info */ { { "input.sensitivity", "high" } },
        // apc-hid.c rows, aux 10: FEATURE 0x11 [17, 10]; INPUT 0x15 [21, 63, 86] = 2023/01/31;
        // INPUT 0x1A [26, 120, 0] (Battery.RemainingTimeLimit); INPUT 0x22 [34, 2]; INPUT 0x23
        // [35, 253, 0] (Output.HighVoltageTransfer, 409-410); INPUT 0x24 [36, 208, 0]. Both
        // DelayBeforeShutdown and DelayBeforeStartup paths
        RW_APC_6 " " RW_DELAYS, RW_APC_6 " " RW_DELAYS);
}

// legrandHid2Nut() rows bound by the Keor PDU 800 descriptor (US-106), derived from its usages and
// legrand-hid.c:129-184: the UPS.Input, UPS.Output and PowerSummary rows (battery.runtime through
// RuntimeToEmpty, names ignore case), the commands on UPS.Output.Test / DelayBefore* and the
// beeper. "BOOL" stands for rows 158-162.
static const char* const NUT_ROWS_LEGRAND_KEOR =
    "BOOL battery.charge battery.charge.low battery.charge.warning battery.runtime battery.voltage "
    "battery.voltage.nominal beeper.disable beeper.enable input.transfer.high input.transfer.low input.voltage "
    "input.voltage.nominal load.off.delay load.on.delay output.frequency output.voltage test.battery.start.deep "
    "test.battery.start.quick test.battery.stop ups.load ups.realpower.nominal";

// US-087: Legrand Keor PDU 800 from the #90 dump. legrand-hid.c: beeper.enable 2 /
// beeper.disable 3 (no beeper.mute), test.battery.* on UPS.Output.Test, load.* and
// shutdown.* on UPS.Output.DelayBefore*, no RW rows besides ups.delay.*
void test_replay_legrand_keor_pdu800_issue90(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json",
        // US-106: commands from legrandHid2Nut(); no shutdown.stop, which legrand-hid has no row for
        "beeper.enable beeper.disable beeper.toggle test.battery.start.quick test.battery.start.deep test.battery.stop "
        "load.off load.on load.off.delay load.on.delay shutdown.return shutdown.stayoff shutdown.default", nullptr, NUT_ROWS_LEGRAND_KEOR,
        { { "ups.mfr", "Legrand" }, { "ups.beeper.status", "enabled" }, { "battery.voltage", "13.7" },
          { "battery.voltage.nominal", "12.0" }, { "ups.delay.shutdown", "20" }, { "ups.delay.start", "30" },
          /* FEATURE 0x17 [23, 0]: UPS.Output.Overload */ { "ups.status.overload", "0" },
          /* FEATURE 0x07 byte 4: PowerSummary.WarningCapacityLimit */ { "battery.charge.warning", "20" } },
        RW_DELAYS, RW_DELAYS);
}

// powercomHid2Nut() rows bound by the SPD-750U descriptor (US-077), derived from its usages and
// powercom-hid.c, not from the firmware. No usage of the PowercomUPS vendor page: only the
// rows with standard paths bind.
// - BOOL: PowerSummary.PresentStatus.* (544-553, report 10, INPUT and FEATURE), and
//   UPS.PresentStatus.* (557-568, report 38) with Boost/Buck (560-561, usages 0x6E/0x6F);
// - battery.voltage (587, on PowerSummary.PresentStatus.ACPresent, the value of report 0xA4);
// - battery.charge (589 report 14, 590 report 24), battery.charge.low (591, 12),
//   battery.charge.warning (592, 11), battery.runtime (593, 55), battery.mfr.date (594, 23),
//   battery.type (595, iDeviceChemistry 5), battery.temperature (677, Battery.Temperature 45:
//   the temperature report of issue #36);
// - ups.beeper.status (601 report 19: the beeper of issue #36; 602 report 37), ups.load (603,
//   31), ups.date (604, PowerSummary.ManufacturerDate 13), ups.test.result (605, 21),
//   ups.timer.start (621, 16), ups.timer.shutdown (638, 15);
// - input.voltage (642, 29), input.voltage.nominal (643, 28), input.frequency (644, 30),
//   output.voltage (646, 33), output.voltage.nominal (647, 32), output.frequency (648, 34);
// - commands: beeper.toggle/enable/disable (658-660, report 19), test.battery.start.quick
//   (661, Battery.Test 21), load.on.delay (662, 16), shutdown.return/stayoff (663-664, 15).
// Not bound: PCMDelayBefore* (640, 665-668), PowerSummary.Temperature has no row.
#define NUT_ROWS_POWERCOM_SPD750U "BOOL battery.charge battery.charge.low battery.charge.warning battery.mfr.date " \
    "battery.runtime battery.temperature battery.type battery.voltage beeper.disable beeper.enable beeper.toggle " \
    "input.frequency input.voltage input.voltage.nominal load.on.delay output.frequency output.voltage " \
    "output.voltage.nominal shutdown.return shutdown.stayoff test.battery.start.quick ups.beeper.status ups.date " \
    "ups.load ups.test.result ups.timer.shutdown ups.timer.start"

void test_replay_powercom_spd750u(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json",
        // US-077: commands from powercomHid2Nut(). Only test.battery.start.quick (661):
        // test.battery.start.deep and test.battery.stop came from the catalog without a table,
        // powercom-hid has no row for them. No beeper command: the replay never reads the beeper
        // report (US-109: no quirk hides them any more, see test_spd750u_beeper_*), no load or
        // shutdown command (shutdownCommandsSupported() false, US-058) although 662-664 bind
        "test.battery.start.quick", nullptr, NUT_ROWS_POWERCOM_SPD750U,
        /* powercom-hid.c:620-637: ups.delay.start "60", ups.delay.shutdown DEFAULT_OFFDELAY */
        { { "ups.delay.start", "60" }, { "ups.delay.shutdown", "20" } },
        /* no RW row; powercom-hid.c:620-639 delays with aux 8 */ "ups.delay.shutdown:8 ups.delay.start:8", "ups.delay.shutdown:8 ups.delay.start:8");
}

// cpsHid2Nut() rows bound by the CyberPower descriptors (US-075), derived from their usages and
// cps-hid.c, not from the firmware. Common to all five: battery.type (366, iDeviceChemistry),
// battery.mfr.date (368, iOEMInformation: no UPS.Battery collection, so no 367 row),
// battery.charge.warning / battery.charge.low / battery.status (369, 370, 376: FEATURE 0x07),
// battery.charge / battery.runtime / battery.runtime.low (371-373), battery.voltage.nominal and
// battery.voltage (374-375), ups.load (379, Output.PercentLoad), ups.beeper.status (380,
// PowerSummary.AudibleAlarmControl), ups.test.result (381, Output.Test), ups.realpower.nominal
// (385), ups.timer.start / ups.timer.shutdown (388-389), the BOOL rows of PresentStatus.ACPresent,
// Charging, Discharging, BelowRemainingCapacityLimit, FullyCharged, RemainingTimeLimitExpired
// (395-400), Output.Boost and Output.Overload (402-403), input.voltage.nominal / input.voltage
// (409-410), input.transfer.low/high (411-412 on UPS.Input, 414-415 on UPS.Output for the
// CP1600EPFCLCD), output.voltage (422) and the command rows of Output.Test (426-428),
// DelayBeforeShutdown/Startup (429-431) and AudibleAlarmControl (433-437). No descriptor has
// NeedReplacement (401), PowerSummary.Temperature (392), DelayBeforeReboot (390, 432),
// Output.Frequency (421) or Output.ConfigVoltage (423).
#define NUT_ROWS_CPS_COMMON_1 "BOOL battery.charge battery.charge.low battery.charge.warning battery.mfr.date " \
    "battery.runtime battery.runtime.low battery.status battery.type battery.voltage battery.voltage.nominal " \
    "beeper.disable beeper.enable beeper.mute beeper.off beeper.on "
#define NUT_ROWS_CPS_COMMON_2 "input.transfer.high input.transfer.low input.voltage input.voltage.nominal " \
    "load.off.delay load.on.delay output.voltage shutdown.stop test.battery.start.deep test.battery.start.quick " \
    "test.battery.stop ups.beeper.status "
// BR700ELCD: plus ups.firmware (391, CPSFirmwareVersion 0xFF0100D0) and ups.power.nominal (383,
// Output.ConfigApparentPower, FEATURE 62)
#define NUT_ROWS_CPS_BR700ELCD NUT_ROWS_CPS_COMMON_1 NUT_ROWS_CPS_COMMON_2 \
    "ups.firmware ups.load ups.power.nominal ups.realpower.nominal ups.test.result ups.timer.shutdown ups.timer.start"
// CP1350C: neither CPSFirmwareVersion nor Output.ConfigApparentPower
#define NUT_ROWS_CPS_CP1350C NUT_ROWS_CPS_COMMON_1 NUT_ROWS_CPS_COMMON_2 \
    "ups.load ups.realpower.nominal ups.test.result ups.timer.shutdown ups.timer.start"
// CP1500EPFCLCD: plus ups.firmware (391) and input.sensitivity (416, Output.CPSInputSensitivity)
#define NUT_ROWS_CPS_CP1500EPFCLCD NUT_ROWS_CPS_COMMON_1 "input.sensitivity " NUT_ROWS_CPS_COMMON_2 \
    "ups.firmware ups.load ups.realpower.nominal ups.test.result ups.timer.shutdown ups.timer.start"
// CP1600EPFCLCD: plus ups.firmware (391), input.sensitivity (416), ups.power (382,
// Output.ApparentPower 29), ups.power.nominal (383, FEATURE 24) and ups.realpower (384,
// Output.ActivePower 25: measured, no estimate)
#define NUT_ROWS_CPS_CP1600EPFCLCD NUT_ROWS_CPS_COMMON_1 "input.sensitivity " NUT_ROWS_CPS_COMMON_2 \
    "ups.firmware ups.load ups.power ups.power.nominal ups.realpower ups.realpower.nominal ups.test.result " \
    "ups.timer.shutdown ups.timer.start"
// CP2000EIPFCRM2U: the CP1600EPFCLCD rows. Its descriptor differs only in report 0x17, FEATURE
// only with Output.Overload and Output.Boost (402-403), without the three Output flags cps-hid
// does not read
#define NUT_ROWS_CPS_CP2000EIPFCRM2U NUT_ROWS_CPS_CP1600EPFCLCD
// UT850EG: the BR700ELCD rows plus input.frequency (408, Input.Frequency FEATURE 42). Its
// 0xFF010043 is under UPS.0xFF0100BA, not UPS.Output: no input.sensitivity row
#define NUT_ROWS_CPS_UT850EG NUT_ROWS_CPS_COMMON_1 "input.frequency " NUT_ROWS_CPS_COMMON_2 \
    "ups.firmware ups.load ups.power.nominal ups.realpower.nominal ups.test.result ups.timer.shutdown ups.timer.start"

// Descriptor only, no scenarios: checks the NUT table rows (US-066, US-075)
void test_replay_cyberpower_br700elcd(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json", nullptr, nullptr,
        NUT_ROWS_CPS_BR700ELCD,
        /* Output.DelayBeforeShutdown/Startup */ {}, RW_DELAYS, RW_DELAYS);
}

void test_replay_cyberpower_cp1350c(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-075) */ NUT_ROWS_CPS_CP1350C,
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
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-075) */ NUT_ROWS_CPS_CP1500EPFCLCD,
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
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json", nullptr, nullptr, /* NUT table rows (US-066, US-075) */ NUT_ROWS_CPS_CP1600EPFCLCD,
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
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-075) */ NUT_ROWS_CPS_UT850EG,
        // issue #43: FEATURE 0x2A [42, 245, 1] Input.Frequency = 501, ten times 50 Hz ->
        // cps_adjust_frequency_scale 0.1 (v1.8.0 served 501.0); battery.voltage "%.1f" as
        // cps_battvolt. No UPS.Battery collection, so no battery.mfr.date row (discussion #95)
        { { "input.frequency", "50.1" }, { "battery.voltage", "13.2" }, { "battery.charge.warning", "20" } },
        // cps-hid.c rows: FEATURE 0x07 byte 4 = 10; INPUT 0x82 [130, 44, 1] = 300.
        // Input.*VoltageTransfer (FEATURE 0x83/0x84) is bound but not in the dump: not listed
        "battery.charge.low:10 battery.runtime.low:10 " RW_DELAYS,
        "battery.charge.low:10 battery.runtime.low:10 " RW_DELAYS);
}

// Live dump of v1.11.0 attached to issue #108 (ESP32-P4): every value of the dump is the same on
// main but the strings (battery.type, ups.firmware: no string descriptors in the dump) and the
// serial (anonymized). battery.mfr.date (cps-hid.c:368, PowerSummary.iOEMInformation, FEATURE
// 0x04) is bound, and report 4 is read since US-111 (layout without vendor reports), but the dump
// of v1.11.0 has neither report 4 nor the string descriptors: the date is checked by the dump of the
// test build below
void test_replay_cyberpower_cp2000eipfcrm2u_issue108(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-075) */ NUT_ROWS_CPS_CP2000EIPFCRM2U,
        /* replayed FEATURE 0x1A, CPSInputSensitivity = 1 -> cps_sensitivity_info */ { { "input.sensitivity", "normal" },
          // Output.ConfigApparentPower (FEATURE 0x18 [24, 176, 4, 208, 7] = 2000), and ups.realpower
          // measured by Output.ActivePower (INPUT 0x19 [25, 106, 0], the last replayed: FEATURE
          // 0x19 has 107), never replaced by the estimate (10% of 1200 W = 120)
          { "ups.power.nominal", "2000" }, { "ups.realpower", "106" }, { "battery.voltage", "27.4" },
          { "battery.charge.warning", "20" }, { "ups.timer.shutdown", "-1" }, { "ups.timer.start", "-1" } },
        // cps-hid.c rows: FEATURE 0x07 byte 4 = 10; INPUT 0x08 bytes 4-5 [.., 44, 1] = 300;
        // FEATURE 0x10 [16, 195, 0, 1, 1] = 195 / 257 (Output.*VoltageTransfer, 414-415, as on the
        // CP1600EPFCLCD); FEATURE 0x1A [26, 1]
        RW_CPS_5 " " RW_DELAYS, RW_CPS_5 " " RW_DELAYS);
}

// Live dump of the test build cps-report4-v1 on the same CP2000EIPFCRM2U (issue #108): the v1.11.0
// dump plus FEATURE 0x04 [4, 3], read since US-111. iOEMInformation points at string 3, the
// manufacturer "CPS": battery.mfr.date is "CPS" as in NUT (cps-hid.c:368), also on the reporter's
// Home Assistant NUT add-on. The strings come from what the build served; the runner hands them to
// the driver again after the reports, when the StringId rows know their index
void test_replay_cyberpower_cp2000eipfcrm2u_issue108_report4(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108_report4.json",
        CMD_BEEPER CMD_BATTERY_TEST CMD_LOAD_FULL CMD_DEFAULT, nullptr, /* NUT table rows (US-066, US-075) */ NUT_ROWS_CPS_CP2000EIPFCRM2U,
        // The three StringId rows of cps-hid.c: iOEMInformation (FEATURE 0x04 = 3), iDeviceChemistry
        // (FEATURE 0x03 = 4), 0xFF0100D0 (FEATURE 0x1B = 5); ups.realpower from INPUT 0x19
        // [25, 99, 0], the last replayed (FEATURE 0x19 has 97)
        { { "battery.mfr.date", "CPS" }, { "battery.type", "PbAcid" }, { "ups.firmware", "CR02401B7114" },
          { "ups.realpower", "99" }, { "battery.voltage", "26.5" } },
        RW_CPS_5 " " RW_DELAYS, RW_CPS_5 " " RW_DELAYS);
}

// liebertHid2Nut() rows bound by the VI 2200 SHL descriptor (US-106), derived from its usages and
// liebert-hid.c:87-141: Flow.[4] and PowerConverter.Input.[1] rows included; no transfer usages.
// "BOOL" stands for rows 121-130.
static const char* const NUT_ROWS_LIEBERT_VI2200 =
    "BOOL battery.charge battery.runtime battery.type battery.voltage beeper.toggle experimental.battery.capacity "
    "experimental.battery.capacity.nominal input.voltage load.off.delay load.on.delay output.frequency "
    "output.frequency.nominal output.voltage output.voltage.nominal shutdown.stop test.battery.start "
    "ups.beeper.status ups.load ups.power.nominal ups.test.result";

// US-097: liebert-hid.c commands only (test.battery.start, beeper.toggle, load.*.delay,
// shutdown.stop and what usbhid-ups derives), shutdown.default (US-058). liebert_hid2nut
// has no RW row besides ups.delay.* (PowerSummary.DelayBefore*, aux 10)
void test_replay_phoenixtec_powerwalker_vi2200shl_issue96(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json",
        "beeper.toggle test.battery.start load.off load.on load.off.delay load.on.delay "
        "shutdown.return shutdown.stayoff shutdown.stop shutdown.default",
        nullptr, NUT_ROWS_LIEBERT_VI2200,
        // FEATURE 0x72 [114, 230] UPS.Flow.[4].ConfigVoltage, FEATURE 0x36 [54, 50] ConfigFrequency
        { { "output.voltage.nominal", "230" }, { "output.frequency.nominal", "50" } },
        RW_DELAYS, RW_DELAYS);
}

// openupsHid2Nut() rows bound by the W150 descriptor (US-078), derived from its usages and
// openups-hid.c:296-337: every row but battery.voltage.nominal and the cell voltages
// (commented out upstream). "BOOL" stands for the 12 status rows 326-337.
static const char* const NUT_ROWS_OPENUPS_W150 =
    "BOOL battery.capacity battery.charge battery.charge.low battery.charge.warning battery.current "
    "battery.mfr.date battery.runtime battery.temperature battery.type battery.voltage input.current "
    "input.voltage output.current output.voltage ups.serial";

// The W150 reports PresentStatus.BatteryPresent = 0 in its normal state, but
// OpenUPSDriver ignores the unreliable flag, so ups.status stays OL; see the
// fixture "notes" (US-055).
void test_replay_openups_wallecube_w150(void) {
    FixtureReplayRunner::runFixtureTest("test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json",
        "", nullptr, NUT_ROWS_OPENUPS_W150,
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

// Demonstration of US-107 on the Eaton 3S descriptor: NeedReplacement (mge-hid.c:2126, FEATURE
// 0x01) gives ups.alarm "Replace battery!" and ALARM OL RB; the vendor row of
// Output.ShortCircuit (mge-hid.c:2160, FEATURE 0x03) gives its text first. The 3S descriptor
// has no Input.[1] Buck/VoltageOutOfRange: those are in test_eaton_driver.
void test_eaton_3s_need_replacement_alarm(void) {
    FixtureReplayRunner::runFieldScenario("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        { { "UPS.PowerSummary.PresentStatus.NeedReplacement", 1 } }, "ALARM OL RB", "Replace battery!", "OL");
}

void test_eaton_3s_vendor_and_bit_alarms(void) {
    FixtureReplayRunner::runFieldScenario("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        { { "UPS.PowerSummary.PresentStatus.NeedReplacement", 1 },
          { "UPS.PowerConverter.Output.PresentStatus.0xFFFF004A", 1 } },
        "ALARM OL RB", "Output short circuit! Replace battery!", "OL");
}

// PresentStatus.Good 0 (off_info, mge-hid.c:2133) gives OFF, with no alarm
void test_eaton_3s_good_zero_is_off(void) {
    FixtureReplayRunner::runFieldScenario("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
        { { "UPS.PowerSummary.PresentStatus.Good", 0 } }, "OL OFF", nullptr, "OL");
}

// ups.status, ups.alarm and input.transfer.reason at the end of the replay of every fixture
// (US-107). Taken from the firmware before US-107: the regression criterion is "the same
// tokens as before, in the order of ups_status_set()", and every difference is commented
// with the fixture key that causes it. No fixture had a status key of US-107 set (trim,
// boost, bypass*, off, cal, vrange, frange, the ups_alarm_set() bits) nor LB+CHRG or RB+OVER
// together. "Unknown": fixtures without reports. A new fixture must be added here.
void test_status_baseline_all_fixtures(void) {
    static const struct {
        const char* file;
        const char* status;
        const char* alarm;   // nullptr: no ups.alarm
        const char* reason;  // nullptr: no input.transfer.reason
    } rows[] = {
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98.json", "OL DISCHRG", nullptr, nullptr },
        // US-076: APCLineFailCause (apc-hid.c:403) gives the reason of the transfer to battery
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_onbattery.json", "OB DISCHRG", nullptr, "input voltage out of range" },
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_online.json", "OL DISCHRG", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json", "OL CHRG", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json", "OL", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json", "OL CHRG", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_xs1500m_vid051d_pid0002_issue94.json", "OL", nullptr, nullptr },
        { "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json", "OL", nullptr, nullptr },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108_report4.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_ut850eg_vid0764_pid0501_discussion95.json", "OL", nullptr, nullptr },
        { "test/fixtures/eaton/eaton_3s_vid0463_pidffff.json", "OL", nullptr, nullptr },
        { "test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json", "Unknown", nullptr, nullptr },
        // Charging 1 at 80 %: eaton_abm_check_chrg_fun keeps CHRG (mge-hid.c:556-576)
        { "test/fixtures/eaton/eaton_ellipse_eco_650_vid0463_pidffff_issue112.json", "OL CHRG", nullptr, nullptr },
        // Charging 1 at 100 %, ABM unknown: "!chrg" (mge-hid.c:556-576), OL as on the 3S
        { "test/fixtures/eaton/eaton_ellipse_pro_650_vid0463_pidffff_issue110.json", "OL", nullptr, nullptr },
        { "test/fixtures/eaton/mge_protection_center_vid0463_pidffff_issue111.json", "OL", nullptr, nullptr },
        { "test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json", "OL", nullptr, nullptr },
        { "test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json", "OL", nullptr, nullptr },
        { "test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json", "OL CHRG", nullptr, nullptr },
        { "test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json", "Unknown", nullptr, nullptr },
    };
    TEST_ASSERT_EQUAL_MESSAGE(30, (int)(sizeof(rows) / sizeof(rows[0])), "one row per fixture in test/fixtures");
    for (const auto& r : rows) FixtureReplayRunner::runStatusBaseline(r.file, r.status, r.alarm, r.reason);
}

// Every fixture of test/fixtures, for the golden files of US-079
static const char* const kAllFixtures[] = {
    "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98.json",
    "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_onbattery.json",
    "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_online.json",
    "test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json",
    "test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json",
    "test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json",
    "test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json",
    "test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json",
    "test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json",
    "test/fixtures/apc/apc_backups_xs1500m_vid051d_pid0002_issue94.json",
    "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json",
    "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json",
    "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json",
    "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json",
    "test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json",
    "test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json",
    "test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json",
    "test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json",
    "test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108.json",
    "test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108_report4.json",
    "test/fixtures/cyberpower/cyberpower_ut850eg_vid0764_pid0501_discussion95.json",
    "test/fixtures/eaton/eaton_3s_vid0463_pidffff.json",
    "test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json",
    "test/fixtures/eaton/eaton_ellipse_eco_650_vid0463_pidffff_issue112.json",
    "test/fixtures/eaton/eaton_ellipse_pro_650_vid0463_pidffff_issue110.json",
    "test/fixtures/eaton/mge_protection_center_vid0463_pidffff_issue111.json",
    "test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json",
    "test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json",
    "test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json",
    "test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json",
};

// US-079: what every fixture gives with the driver the registry picks (golden/*.registry.txt,
// FixtureReplayRunner::goldenText). Taken before the removal of the hand-written mappings and
// of the command groups, which changed no known brand. A deliberate change regenerates them
// (ESP32NUT_WRITE_GOLDEN=1) and shows its diff in the review.
void test_golden_registry_all_fixtures(void) {
    TEST_ASSERT_EQUAL_MESSAGE(30, (int)(sizeof(kAllFixtures) / sizeof(kAllFixtures[0])), "one row per fixture in test/fixtures");
    for (const char* f : kAllFixtures) FixtureReplayRunner::checkGolden(f, false);
}

// US-079: the same with GenericDriver, as a brand the registry does not know
// (golden/*.generic.txt): the real PDC descriptors of the fixtures stand for an unknown UPS.
// The differences from the hand-written mappings are in the v1.12.0 release notes.
void test_golden_generic_all_fixtures(void) {
    for (const char* f : kAllFixtures) FixtureReplayRunner::checkGolden(f, true);
}

// US-072: on every fixture, with the registry's driver and with GenericDriver, the full poll
// leaves out exactly the semi-static reports (FixtureReplayRunner::checkSemiStaticReports)
// and reads them again after a SET VAR or an INSTCMD. Eaton 3S: 6 reports (11, 19, 20, 21, 32,
// 34); 12 and 25 are semi-static too but the quick poll reads them, 16 and 18 have rows deferred
// until mge_type is known, which the replay never reads again.
void test_semi_static_reports_skipped_after_first_full_poll(void) {
    size_t total = 0;
    for (const char* f : kAllFixtures) {
        total += FixtureReplayRunner::checkSemiStaticReports(f, false);
        total += FixtureReplayRunner::checkSemiStaticReports(f, true);
    }
    TEST_ASSERT_TRUE_MESSAGE(total > 0, "some fixture has semi-static reports");
    TEST_ASSERT_EQUAL(6, FixtureReplayRunner::checkSemiStaticReports("test/fixtures/eaton/eaton_3s_vid0463_pidffff.json", false));
}

// US-075 baseline: everything the five CyberPower fixtures serve after the replay, taken from
// the firmware before the cps_hid2nut migration. After the migration (every value, status
// flag and command from cpsHid2Nut()) it is the same, key by key: no difference to list. Usages of the descriptors that matter to the
// migration (from a dump of the parsed descriptors):
// - no descriptor has PresentStatus.ShutdownImminent, BatteryPresent, FullyDischarged,
//   CommunicationLost, PowerSummary.PresentStatus.Overload, BatterySystem.Battery.FullChargeCapacity,
//   Battery.ManufacturerDate, Battery.Date, PowerSummary.Temperature or any DelayBeforeReboot: the
//   generic mappings the migration supersedes have nothing to read on these fixtures;
// - Output.ActivePower and Output.ApparentPower only on the CP1600EPFCLCD (reports 25 and 29,
//   INPUT and FEATURE); the other four estimate ups.realpower;
// - BR700ELCD and UT850EG have vendor reports (>= 130: RemainingTimeLimit 130, Output.Test 133,
//   DelayBeforeShutdown/Startup 134/135), so no table string is requested (issue #40);
//   CPSFirmwareVersion 0xFF0100D0 is in every descriptor but the CP1350C, iDeviceChemistry and
//   iOEMInformation in all five, but no fixture records those strings;
// - Input.Frequency only on the UT850EG (FEATURE 42), no Output.Frequency anywhere;
// - the CP1600EPFCLCD has Output.InternalFailure/VoltageOutOfRange/FrequencyOutOfRange and the
//   BR700ELCD and UT850EG have Output.Buck: cps-hid reads none of them.
static const char* const kCpsSnapshotBr700elcd =
    "ups.delay.shutdown=60\n"
    "ups.delay.start=120\n"
    "ups.mfr=CPS\n"
    "ups.model=BR700ELCD\n"
    "ups.status=Unknown\n";

static const char* const kCpsSnapshotCp1350c =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime.low=300\n"
    "battery.runtime=5040\n"
    "battery.status=100%\n"
    "battery.voltage.nominal=12\n"
    "battery.voltage=13.9\n"
    "driver.name=CyberPowerDriver\n"
    "input.transfer.high=140\n"
    "input.transfer.low=90\n"
    "input.voltage.nominal=120\n"
    "input.voltage=123.0\n"
    "output.voltage=124.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=60\n"
    "ups.delay.start=120\n"
    "ups.load=0\n"
    "ups.mfr=CPS\n"
    "ups.model= CP 1350C\n"
    "ups.realpower.nominal=810\n"
    "ups.realpower=0\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.timer.shutdown=2048\n"
    "ups.timer.start=851\n"
    "ups.status=OL\n";

static const char* const kCpsSnapshotCp1500epfclcd =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime.low=300\n"
    "battery.runtime=15000\n"
    "battery.status=100%\n"
    "battery.voltage.nominal=24\n"
    "battery.voltage=24.0\n"
    "driver.name=CyberPowerDriver\n"
    "input.sensitivity=high\n"
    "input.transfer.high=260\n"
    "input.transfer.low=180\n"
    "input.voltage.nominal=230\n"
    "input.voltage=238.0\n"
    "output.voltage=238.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=60\n"
    "ups.delay.start=120\n"
    "ups.load=0\n"
    "ups.mfr=CPS\n"
    "ups.model=CP1500EPFCLCD\n"
    "ups.realpower.nominal=900\n"
    "ups.realpower=0\n"
    "ups.serial=redacted\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.test.result=Done and passed\n"
    "ups.timer.shutdown=-1\n"
    "ups.timer.start=-1\n"
    "ups.status=OL\n";

static const char* const kCpsSnapshotCp1600epfclcd =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime.low=300\n"
    "battery.runtime=2900\n"
    "battery.status=100%\n"
    "battery.voltage.nominal=24\n"
    "battery.voltage=27.2\n"
    "driver.name=CyberPowerDriver\n"
    "input.sensitivity=normal\n"
    "input.transfer.high=257\n"
    "input.transfer.low=195\n"
    "input.voltage.nominal=230\n"
    "input.voltage=226.0\n"
    "output.voltage=226.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=60\n"
    "ups.delay.start=120\n"
    "ups.load=16\n"
    "ups.mfr=CPS\n"
    "ups.model=CP1600EPFCLCD\n"
    "ups.power.nominal=1600\n"
    "ups.power=204\n"
    "ups.realpower.nominal=1000\n"
    "ups.realpower=153\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.shutdown=-1\n"
    "ups.timer.start=-1\n"
    "ups.status=OL\n";

// Added after the cps_hid2nut migration (issue #108, dump of v1.11.0): taken from main, the same
// as the dump but battery.type and ups.firmware (strings the dump does not have) and the serial
// (anonymized). No battery.mfr.date: report 4 is read since US-111, but the dump has neither
// report 4 nor the string descriptors
static const char* const kCpsSnapshotCp2000eipfcrm2u =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime.low=300\n"
    "battery.runtime=4250\n"
    "battery.status=100%\n"
    "battery.voltage.nominal=24\n"
    "battery.voltage=27.4\n"
    "driver.name=CyberPowerDriver\n"
    "input.sensitivity=normal\n"
    "input.transfer.high=257\n"
    "input.transfer.low=195\n"
    "input.voltage.nominal=230\n"
    "input.voltage=232.0\n"
    "output.voltage=232.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=60\n"
    "ups.delay.start=120\n"
    "ups.load=10\n"
    "ups.mfr=CPS\n"
    "ups.model=CP2000EIPFCRM2U\n"
    "ups.power.nominal=2000\n"
    "ups.power=213\n"
    "ups.realpower.nominal=1200\n"
    "ups.realpower=106\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.test.result=Done and passed\n"
    "ups.timer.shutdown=-1\n"
    "ups.timer.start=-1\n"
    "ups.status=OL\n";

static const char* const kCpsSnapshotUt850eg =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime.low=300\n"
    "battery.runtime=8830\n"
    "battery.status=100%\n"
    "battery.voltage.nominal=12\n"
    "battery.voltage=13.2\n"
    "driver.name=CyberPowerDriver\n"
    "input.frequency=50.1\n"
    "input.voltage.nominal=230\n"
    "input.voltage=232.0\n"
    "output.voltage=233.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=60\n"
    "ups.delay.start=120\n"
    "ups.load=0\n"
    "ups.mfr=CPS\n"
    "ups.model=UT850EG\n"
    "ups.power.nominal=0\n"
    "ups.realpower.nominal=425\n"
    "ups.realpower=0\n"
    "ups.serial=                \n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.test.result=Done and passed\n"
    "ups.timer.shutdown=-1\n"
    "ups.timer.start=-1\n"
    "ups.status=OL\n";

// A fixture and what it must give
struct SnapshotCase { const char* file; const char* expected; };

// FixtureReplayRunner::snapshot() of every fixture against its expected text
static void checkSnapshots(const SnapshotCase* cases, size_t n) {
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].expected, FixtureReplayRunner::snapshot(cases[i].file).c_str(), cases[i].file);
    }
}

void test_cyberpower_snapshot_baseline(void) {
    static const SnapshotCase fixtures[] = {
        { "test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json", kCpsSnapshotBr700elcd },
        { "test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json", kCpsSnapshotCp1350c },
        { "test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json", kCpsSnapshotCp1500epfclcd },
        { "test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json", kCpsSnapshotCp1600epfclcd },
        { "test/fixtures/cyberpower/cyberpower_cp2000eipfcrm2u_vid0764_pid0601_issue108.json", kCpsSnapshotCp2000eipfcrm2u },
        { "test/fixtures/cyberpower/cyberpower_ut850eg_vid0764_pid0501_discussion95.json", kCpsSnapshotUt850eg },
    };
    checkSnapshots(fixtures, sizeof(fixtures) / sizeof(fixtures[0]));
}

// US-076 baseline: everything the thirteen APC fixtures serve after the replay, taken from the
// firmware before the apc_hid2nut migration and updated after it, with every difference listed
// above its fixture. ups.status is the same on all thirteen. Usages of the descriptors that matter to the
// migration (from a dump of the parsed descriptors):
// - HIDParser names only APCBattReplaceDate among the APC vendor usages: the others have hex
//   paths (UPS.0xFF860005.0xFF86007C is APCGeneralCollection.APCDelayBeforeReboot), so the old
//   APCDriver mappings on UPS.APCGeneralCollection.* and UPS.APCEnvironment.* never matched;
// - APCGeneralCollection (0xFF860005) delays on BGM2200 (x3), BX750MI, BX1500G, CS750RS, ES700G
//   and RS900MI; Output.APCDelayBeforeReboot/Shutdown/Startup (0xFF86007C-7E) on the CS500;
// - APCStatusFlag (UPS.PowerSummary.0xFF860060) on BX1500G, CS750RS, ES700G, RS900MI, XS700U;
// - APCLineFailCause (UPS.Input.0xFF860052) on BGM2200 (x3), BX750MI, BX1500G, CS500, CS750RS,
//   ES700G, RS900MI and the Smart-UPS 750 of issue #78;
// - APCPanelTest (UPS.0xFF860072) on BX1500G, CS500, CS750RS, ES700G, RS900MI, Smart-UPS #78;
// - UPS.Output.PercentLoad only on the CS500 and the Smart-UPS #78, PowerConverter.PercentLoad
//   on the Back-UPS; Output.Current only on the Smart-UPS #78;
// - no APCEnvironment (ambient.*) on any fixture.
// US-076 differences:
//   battery.voltage: 0.00 -> 0.0, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   ups.timer.reboot: (none) -> 0, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
static const char* const kApcSnapshotBackupsBgm2200Pid0002Issue98 =
    "battery.charge.low=10\n"
    "battery.charge=100\n"
    "battery.mfr.date=not set\n"
    "battery.runtime.low=120\n"
    "battery.runtime=810\n"
    "battery.voltage.nominal=24.0\n"
    "battery.voltage=0.0\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=medium\n"
    "input.transfer.high=298\n"
    "input.transfer.low=176\n"
    "input.voltage.nominal=230\n"
    "input.voltage=0.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=20\n"
    "ups.firmware=481600G5-481700G2\n"
    "ups.load=28\n"
    "ups.mfr.date=2026/06/08\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS BGM2200-MSX\n"
    "ups.realpower.nominal=1320\n"
    "ups.realpower=369\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=1\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL DISCHRG\n";

// US-076 differences:
//   battery.voltage: 24.60 -> 24.6, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   input.transfer.reason: (none) -> input voltage out of range, apc-hid.c:403 APCLineFailCause in vrange_info (US-107)
//   ups.status.vrange: (none) -> 1, apc-hid.c:403-404 (internal key)
//   ups.timer.reboot: (none) -> 0, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
static const char* const kApcSnapshotBackupsBgm2200Pid0002Issue98Onbattery =
    "battery.charge.low=10\n"
    "battery.charge=78\n"
    "battery.mfr.date=2026/07/25\n"
    "battery.runtime.low=120\n"
    "battery.runtime=609\n"
    "battery.voltage.nominal=24.0\n"
    "battery.voltage=24.6\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=medium\n"
    "input.transfer.high=298\n"
    "input.transfer.low=176\n"
    "input.transfer.reason=input voltage out of range\n"
    "input.voltage.nominal=230\n"
    "input.voltage=0.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=20\n"
    "ups.firmware=481600G5-481700G2\n"
    "ups.load=25\n"
    "ups.mfr.date=2026/06/08\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS BGM2200-CH\n"
    "ups.realpower.nominal=1320\n"
    "ups.realpower=330\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=0\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=1\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.status.vrange=1\n"
    "ups.test.result=Done and passed\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OB DISCHRG\n";

// US-076 differences:
//   battery.voltage: 27.90 -> 27.9, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   ups.timer.reboot: (none) -> 0, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
static const char* const kApcSnapshotBackupsBgm2200Pid0002Issue98Online =
    "battery.charge.low=10\n"
    "battery.charge=100\n"
    "battery.mfr.date=2026/07/25\n"
    "battery.runtime.low=120\n"
    "battery.runtime=669\n"
    "battery.voltage.nominal=24.0\n"
    "battery.voltage=27.9\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=medium\n"
    "input.transfer.high=298\n"
    "input.transfer.low=176\n"
    "input.voltage.nominal=230\n"
    "input.voltage=224.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=20\n"
    "ups.firmware=000000G0-481700G2\n"
    "ups.load=30\n"
    "ups.mfr.date=2026/06/08\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS BGM2200-MSX\n"
    "ups.realpower.nominal=1320\n"
    "ups.realpower=396\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=1\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.test.result=Done and passed\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL DISCHRG\n";

static const char* const kApcSnapshotBackupsBx1500gPid0002Issue55 =
    "ups.delay.shutdown=20\n"
    "ups.firmware.aux=L3\n"
    "ups.firmware=866.L3 .D\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS BX1500G\n"
    "ups.status=Unknown\n";

// US-076 differences:
//   battery.voltage: 13.60 -> 13.6, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   ups.timer.reboot: (none) -> 0, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
static const char* const kApcSnapshotBackupsBx750miPid0002Issue60 =
    "battery.charge.low=10\n"
    "battery.charge=97\n"
    "battery.mfr.date=2001/01/01\n"
    "battery.runtime.low=120\n"
    "battery.runtime=2362\n"
    "battery.voltage.nominal=12.0\n"
    "battery.voltage=13.6\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=medium\n"
    "input.transfer.high=295\n"
    "input.transfer.low=145\n"
    "input.voltage.nominal=230\n"
    "input.voltage=245.0\n"
    "ups.beeper.status=disabled\n"
    "ups.delay.shutdown=20\n"
    "ups.load=14\n"
    "ups.mfr.date=2024/04/19\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS BX750MI\n"
    "ups.realpower.nominal=410\n"
    "ups.realpower=57\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.discharging=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.test.result=Done and passed\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL CHRG\n";

static const char* const kApcSnapshotBackupsCs500Pid0002Issue18 =
    "battery.date=2024/05/23\n"
    "driver.name=APCDriver\n"
    "ups.delay.shutdown=20\n"
    "ups.delay.start=30\n"
    "ups.firmware.aux=q14\n"
    "ups.firmware=808.q14 .I\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS CS 500\n"
    "ups.status=Unknown\n";

// US-076 differences:
//   battery.charge.warning: (none) -> 50, apc-hid.c:336 WarningCapacityLimit (no generic mapping before)
//   battery.voltage: 13.76 -> 13.8, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   ups.status.comm_lost: 0 -> (none), apc-hid.c has no CommunicationLost row (internal key, never served)
//   ups.timer.reboot: (none) -> 0, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
static const char* const kApcSnapshotBackupsCs750rsPid0002Issue48 =
    "battery.charge.low=10\n"
    "battery.charge.warning=50\n"
    "battery.charge=100\n"
    "battery.date=2001/09/25\n"
    "battery.mfr.date=2023/11/08\n"
    "battery.runtime.low=120\n"
    "battery.runtime=2400\n"
    "battery.voltage.nominal=12.0\n"
    "battery.voltage=13.8\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=high\n"
    "input.transfer.high=256\n"
    "input.transfer.low=160\n"
    "input.voltage.nominal=230\n"
    "input.voltage=230.0\n"
    "ups.beeper.status=disabled\n"
    "ups.delay.shutdown=20\n"
    "ups.firmware=936.b1 .I\n"
    "ups.load=11\n"
    "ups.mfr.date=2017/09/28\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS CS 750-RS\n"
    "ups.realpower.nominal=415\n"
    "ups.realpower=45\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL\n";

// US-076 differences:
//   battery.charge.warning: (none) -> 50, apc-hid.c:336 WarningCapacityLimit (no generic mapping before)
//   battery.date: (none) -> not set, apc-hid.c:349 apc_date_conversion: 0 is "not set"
//   battery.voltage: 13.26 -> 13.3, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   ups.status.off: (none) -> 0, apc-hid.c:399 APCStatusFlag 8/16 = !off (internal key, no OFF token)
//   ups.timer.reboot: (none) -> 0, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
//   ups.timer.shutdown: (none) -> -1, apc-hid.c:370 APCGeneralCollection.APCDelayBeforeShutdown
static const char* const kApcSnapshotBackupsEs700gPid0002Issue71 =
    "battery.charge.low=10\n"
    "battery.charge.warning=50\n"
    "battery.charge=97\n"
    "battery.date=not set\n"
    "battery.mfr.date=2018/04/15\n"
    "battery.runtime.low=120\n"
    "battery.runtime=1860\n"
    "battery.voltage.nominal=12.0\n"
    "battery.voltage=13.3\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=medium\n"
    "input.transfer.high=266\n"
    "input.transfer.low=180\n"
    "input.voltage.nominal=230\n"
    "input.voltage=234.0\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=20\n"
    "ups.firmware.aux=O4\n"
    "ups.firmware=871.O4 .I\n"
    "ups.load=27\n"
    "ups.mfr.date=2018/04/15\n"
    "ups.mfr=APC\n"
    "ups.model=Back-UPS ES 700G\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.discharging=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.off=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL CHRG\n";

static const char* const kApcSnapshotBackupsRs900miPid0002Issue18 =
    "ups.delay.shutdown=20\n"
    "ups.firmware.aux=g3\n"
    "ups.firmware=948.g3 .I\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS RS  900MI\n"
    "ups.status=Unknown\n";

// Added after the apc_hid2nut migration (issue #94, dump of v1.12.0-rc1): taken from main, the
// same as the dump but battery.type (a string the dump does not have) and the serial (anonymized)
static const char* const kApcSnapshotBackupsXs1500mPid0002Issue94 =
    "battery.charge.low=10\n"
    "battery.charge.warning=50\n"
    "battery.charge=100\n"
    "battery.date=2001/09/25\n"
    "battery.mfr.date=2025/12/01\n"
    "battery.runtime.low=120\n"
    "battery.runtime=1698\n"
    "battery.voltage.nominal=24.0\n"
    "battery.voltage=27.3\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=medium\n"
    "input.transfer.high=139\n"
    "input.transfer.low=88\n"
    "input.voltage.nominal=120\n"
    "input.voltage=121.0\n"
    "ups.beeper.status=disabled\n"
    "ups.delay.shutdown=20\n"
    "ups.firmware.aux=d11\n"
    "ups.firmware=947.d11 .D\n"
    "ups.load=24\n"
    "ups.mfr.date=2022/03/05\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Back-UPS XS 1500M\n"
    "ups.realpower.nominal=900\n"
    "ups.realpower=216\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.off=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.reboot=0\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL\n";

static const char* const kApcSnapshotBackupsXs700uPid0002Issue20 =
    "ups.delay.shutdown=20\n"
    "ups.mfr=Back-UPS XS 700U   FW:924.Z3 .I USB FW:Z3 \n"
    "ups.model=3B1817X67127  \n"
    "ups.status=Unknown\n";

static const char* const kApcSnapshotSmartups750Pid0002Issue13 =
    "ups.delay.shutdown=20\n"
    "ups.mfr=Smart-UPS 750 FW:651.13.I USB FW:7.3\n"
    "ups.model=AS1625841488\n"
    "ups.status=Unknown\n";

// US-076 differences:
//   battery.charge.warning: (none) -> 50, apc-hid.c:336 WarningCapacityLimit (no generic mapping before)
//   battery.voltage: 27.40 -> 27.4, apc-hid.c:341-342 "%.1f" (the generic mapping gave two decimals)
//   output.current: (none) -> 1.56, apc-hid.c:419 "%.2f" (no generic mapping before)
//   output.voltage.nominal: 230 -> 230.0, apc-hid.c:418 "%.1f"
//   ups.load: 40 -> 40.3, apc-hid.c:352 UPS.Output.PercentLoad "%.1f" (the generic mapping truncated)
//   ups.status.comm_lost: 0 -> (none), apc-hid.c has no CommunicationLost row (internal key, never served)
//   ups.status.frange: (none) -> 0, apc-hid.c:403-404 (internal key)
//   ups.status.vrange: (none) -> 0, apc-hid.c:403-404 (internal key)
//   ups.timer.reboot: (none) -> -1, apc-hid.c:371 (APCGeneralCollection, hex path: the old mapping never matched) or 365
//   ups.timer.start: (none) -> -1, apc-hid.c:363 UPS.Output.DelayBeforeStartup
static const char* const kApcSnapshotSmartups750Pid0002Issue78 =
    "battery.charge.low=10\n"
    "battery.charge.warning=50\n"
    "battery.charge=100\n"
    "battery.mfr.date=2023/01/31\n"
    "battery.runtime.low=120\n"
    "battery.runtime=240\n"
    "battery.temperature=33.2\n"
    "battery.voltage.nominal=24.0\n"
    "battery.voltage=27.4\n"
    "driver.name=APCDriver\n"
    "input.sensitivity=high\n"
    "input.transfer.high=253\n"
    "input.transfer.low=208\n"
    "input.voltage=233.2\n"
    "output.current=1.56\n"
    "output.frequency=50.0\n"
    "output.voltage.nominal=230.0\n"
    "output.voltage=233.2\n"
    "ups.beeper.status=disabled\n"
    "ups.delay.shutdown=20\n"
    "ups.delay.start=30\n"
    "ups.firmware.aux=7.3\n"
    "ups.firmware=651.13.I\n"
    "ups.load=40.3\n"
    "ups.mfr.date=2007/03/07\n"
    "ups.mfr=American Power Conversion\n"
    "ups.model=Smart-UPS 750\n"
    "ups.serial=TEST0000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.frange=0\n"
    "ups.status.no_battery=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.status.vrange=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.reboot=-1\n"
    "ups.timer.shutdown=-1\n"
    "ups.timer.start=-1\n"
    "ups.status=OL\n";

static const char* const kApcSnapshotSmartups750Pid0003Issue22 =
    "ups.delay.shutdown=20\n"
    "ups.firmware=UPS 09.3 / ID=18\n"
    "ups.mfr=American Power Conversion \n"
    "ups.model=Smart-UPS 750\n"
    "ups.status=Unknown\n";

void test_apc_snapshot_baseline(void) {
    static const SnapshotCase cases[] = {
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98.json", kApcSnapshotBackupsBgm2200Pid0002Issue98 },
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_onbattery.json", kApcSnapshotBackupsBgm2200Pid0002Issue98Onbattery },
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_online.json", kApcSnapshotBackupsBgm2200Pid0002Issue98Online },
        { "test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json", kApcSnapshotBackupsBx1500gPid0002Issue55 },
        { "test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json", kApcSnapshotBackupsBx750miPid0002Issue60 },
        { "test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json", kApcSnapshotBackupsCs500Pid0002Issue18 },
        { "test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json", kApcSnapshotBackupsCs750rsPid0002Issue48 },
        { "test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json", kApcSnapshotBackupsEs700gPid0002Issue71 },
        { "test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json", kApcSnapshotBackupsRs900miPid0002Issue18 },
        { "test/fixtures/apc/apc_backups_xs1500m_vid051d_pid0002_issue94.json", kApcSnapshotBackupsXs1500mPid0002Issue94 },
        { "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json", kApcSnapshotBackupsXs700uPid0002Issue20 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json", kApcSnapshotSmartups750Pid0002Issue13 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json", kApcSnapshotSmartups750Pid0002Issue78 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json", kApcSnapshotSmartups750Pid0003Issue22 },
    };
    checkSnapshots(cases, sizeof(cases) / sizeof(cases[0]));
}

// US-076 baseline: the quick and full poll lists APCDriver builds for each APC descriptor
// ("id/type", 3 = FEATURE), taken before the apc_hid2nut migration. The Back-UPS BX750MI and
// BX1500G lists (issue #60) are unchanged; only the ES700G gains a report.
static const char* const kApcPollBackupsBgm2200Pid0002Issue98 =
    "quick: 12/3 15/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 10/3 11/3 12/3 13/3 14/3 15/3 17/3 19/3 20/3 21/3 23/3 24/3 22/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 48/3 49/3 50/3 51/3 53/3 54/3 64/3 80/3 82/3 127/3 126/3 125/3 124/3 123/3 122/3 120/3 227/3";
static const char* const kApcPollBackupsBgm2200Pid0002Issue98Onbattery =
    "quick: 12/3 15/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 10/3 11/3 12/3 13/3 14/3 15/3 17/3 19/3 20/3 21/3 23/3 24/3 22/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 48/3 49/3 50/3 51/3 53/3 54/3 64/3 80/3 82/3 127/3 126/3 125/3 124/3 123/3 122/3 120/3 227/3";
static const char* const kApcPollBackupsBgm2200Pid0002Issue98Online =
    "quick: 12/3 15/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 10/3 11/3 12/3 13/3 14/3 15/3 17/3 19/3 20/3 21/3 23/3 24/3 22/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 48/3 49/3 50/3 51/3 53/3 54/3 64/3 80/3 82/3 127/3 126/3 125/3 124/3 123/3 122/3 120/3 227/3";
static const char* const kApcPollBackupsBx1500gPid0002Issue55 =
    "quick: 6/3 12/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 19/3 20/3 21/3 22/3 23/3 24/3 28/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 39/3 40/3 48/3 49/3 50/3 51/3 52/3 53/3 54/3 64/3 65/3 66/3 80/3 81/3 82/3 96/3 97/3 98/3 127/3 126/3 125/3 124/3 123/3 122/3 121/3 120/3 117/3 116/3 140/3 141/3 142/3 143/3 144/3 145/3 146/3 128/3";
static const char* const kApcPollBackupsBx750miPid0002Issue60 =
    "quick: 12/3 15/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 10/3 11/3 12/3 13/3 14/3 15/3 17/3 19/3 20/3 21/3 23/3 24/3 22/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 48/3 49/3 50/3 51/3 53/3 54/3 64/3 80/3 82/3 127/3 126/3 125/3 124/3 123/3 122/3 120/3 227/3";
static const char* const kApcPollBackupsCs500Pid0002Issue18 =
    "quick: 12/3 13/3 7/3 24/3 25/3 44/3 51/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 22/3 23/3 27/3 28/3 69/3 21/3 24/3 26/3 25/3 30/3 31/3 29/3 71/3 72/3 32/3 70/3 33/3 34/3 66/3 44/3 82/3 42/3 43/3 45/3 35/3 36/3 37/3 38/3 39/3 40/3 41/3 64/3 65/3 46/3 47/3 48/3 49/3 52/3 55/3 50/3 53/3 62/3 63/3 54/3 51/3";
static const char* const kApcPollBackupsCs750rsPid0002Issue48 =
    "quick: 6/3 12/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 19/3 20/3 21/3 22/3 23/3 24/3 28/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 39/3 40/3 48/3 49/3 50/3 51/3 52/3 53/3 54/3 64/3 65/3 66/3 80/3 81/3 82/3 96/3 97/3 98/3 127/3 126/3 125/3 124/3 123/3 122/3 121/3 120/3 117/3 116/3";
// US-076: report 6 (APCStatusFlag, apc-hid.c:399 HU_FLAG_QUICK_POLL) joins the quick poll
static const char* const kApcPollBackupsEs700gPid0002Issue71 =
    "quick: 6/3 12/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 22/3 23/3 24/3 28/3 32/3 34/3 35/3 36/3 37/3 38/3 39/3 40/3 48/3 49/3 50/3 51/3 52/3 53/3 54/3 64/3 65/3 80/3 81/3 96/3 97/3 98/3 127/3 126/3 125/3 124/3 123/3 122/3 121/3 120/3 140/3 141/3 142/3 143/3 144/3 145/3 146/3";
static const char* const kApcPollBackupsRs900miPid0002Issue18 =
    "quick: 6/3 12/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 19/3 20/3 21/3 22/3 23/3 24/3 28/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 39/3 40/3 48/3 49/3 50/3 51/3 52/3 53/3 54/3 64/3 65/3 66/3 80/3 81/3 82/3 96/3 97/3 98/3 127/3 126/3 125/3 124/3 123/3 122/3 121/3 120/3 117/3";
// Issue #94, added after the apc_hid2nut migration: the BX1500G lists without its reports 140-146
static const char* const kApcPollBackupsXs1500mPid0002Issue94 =
    "quick: 6/3 12/3 19/3 20/3 22/3 34/3 35/3 80/3 122/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 19/3 20/3 21/3 22/3 23/3 24/3 28/3 32/3 33/3 34/3 35/3 36/3 37/3 38/3 39/3 40/3 48/3 49/3 50/3 51/3 52/3 53/3 54/3 64/3 65/3 66/3 80/3 81/3 82/3 96/3 97/3 98/3 127/3 126/3 125/3 124/3 123/3 122/3 121/3 120/3 117/3 116/3 128/3";
static const char* const kApcPollBackupsXs700uPid0002Issue20 =
    "quick: 6/3 12/3 19/3 20/3 22/3 34/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 19/3 20/3 21/3 22/3 23/3 24/3 28/3 32/3 33/3 34/3";
static const char* const kApcPollSmartups750Pid0002Issue13 =
    "quick: 12/3 13/3 7/3 24/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 22/3 23/3 27/3 28/3 21/3 24/3 26/3";
static const char* const kApcPollSmartups750Pid0002Issue78 =
    "quick: 12/3 13/3 7/3 24/3 25/3 44/3 51/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 22/3 23/3 27/3 28/3 21/3 24/3 26/3 25/3 30/3 31/3 29/3 32/3 33/3 34/3 44/3 66/3 64/3 42/3 43/3 45/3 35/3 36/3 37/3 38/3 39/3 40/3 41/3 46/3 47/3 48/3 49/3 52/3 55/3 50/3 53/3 62/3 63/3 54/3 51/3";
// US-108: QUIRK_NO_INTERRUPT_PIPE leaves out the INPUT report 0x89 (137/1), the proprietary protocol.
// US-109: 0x90 (144) is an OUTPUT report, never read (it was polled as 144/3 and failed, #100)
static const char* const kApcPollSmartups750Pid0003Issue22 =
    "quick: 12/3 13/3 7/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 150/3 141/3 142/3 147/3 148/3 146/3";

void test_apc_poll_baseline(void) {
    static const SnapshotCase cases[] = {
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98.json", kApcPollBackupsBgm2200Pid0002Issue98 },
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_onbattery.json", kApcPollBackupsBgm2200Pid0002Issue98Onbattery },
        { "test/fixtures/apc/apc_backups_bgm2200_vid051d_pid0002_issue98_online.json", kApcPollBackupsBgm2200Pid0002Issue98Online },
        { "test/fixtures/apc/apc_backups_bx1500g_vid051d_pid0002_issue55.json", kApcPollBackupsBx1500gPid0002Issue55 },
        { "test/fixtures/apc/apc_backups_bx750mi_vid051d_pid0002_issue60.json", kApcPollBackupsBx750miPid0002Issue60 },
        { "test/fixtures/apc/apc_backups_cs500_vid051d_pid0002_issue18.json", kApcPollBackupsCs500Pid0002Issue18 },
        { "test/fixtures/apc/apc_backups_cs750rs_vid051d_pid0002_issue48.json", kApcPollBackupsCs750rsPid0002Issue48 },
        { "test/fixtures/apc/apc_backups_es700g_vid051d_pid0002_issue71.json", kApcPollBackupsEs700gPid0002Issue71 },
        { "test/fixtures/apc/apc_backups_rs900mi_vid051d_pid0002_issue18.json", kApcPollBackupsRs900miPid0002Issue18 },
        { "test/fixtures/apc/apc_backups_xs1500m_vid051d_pid0002_issue94.json", kApcPollBackupsXs1500mPid0002Issue94 },
        { "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json", kApcPollBackupsXs700uPid0002Issue20 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json", kApcPollSmartups750Pid0002Issue13 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json", kApcPollSmartups750Pid0002Issue78 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json", kApcPollSmartups750Pid0003Issue22 },
    };
    for (const auto& c : cases) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expected, FixtureReplayRunner::pollLists<APCDriver>(c.file).c_str(), c.file);
    }
}

// US-077: the SPD-750U fixture (issue #21) has no reports, so the snapshot holds only what the
// descriptor alone gives: unchanged by the migration (ups.mfr and ups.model come from the
// fixture, ups.delay.* from applyDefaultDelays). Poll lists ("id/type"), before:
// "quick: 10/3 | full: 10/3 29/3 33/3 31/3 45/3 19/3 164/3" (issue #36). After (user decisions
// of 2026-10-29): report 38 of the BOOL rows 557-568 joins the quick poll; the full poll reads
// the reports of the bound rows in table order (not 24 and 37: rows 590 and 602 are second
// candidates), the static ones (5, 13, 23, 28, 32) only once, then 0xA4 (164).
static const char* const kPowercomSnapshotSpd750u =
    "ups.delay.shutdown=20\n"
    "ups.delay.start=60\n"
    "ups.mfr=POWERCOM Co.,LTD\n"
    "ups.model=HID UPS Battery\n"
    "ups.status=Unknown\n";
static const char* const kPowercomPollSpd750u =
    "quick: 10/3 38/3 | full: 10/3 38/3 14/3 12/3 11/3 55/3 23/3 5/3 19/3 31/3 13/3 21/3 16/3 15/3 29/3 28/3 30/3 "
    "33/3 32/3 34/3 45/3 164/3";

void test_powercom_snapshot_baseline(void) {
    static const SnapshotCase cases[] = {
        { "test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json", kPowercomSnapshotSpd750u },
    };
    checkSnapshots(cases, sizeof(cases) / sizeof(cases[0]));
    TEST_ASSERT_EQUAL_STRING(kPowercomPollSpd750u,
        FixtureReplayRunner::pollLists<PowercomDriver>("test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json").c_str());
}

// US-078: the W150 fixture (issue #26) before the migration to openupsHid2Nut(), with the
// hand-written mappings of OpenUPSDriver, kept as the reference of the differences below
static const char* const kOpenupsSnapshotW150Before =
    "battery.charge.low=10\n"
    "battery.charge=100\n"
    "battery.current=0.01\n"
    "battery.runtime=12912\n"
    "battery.temperature=29.5\n"
    "battery.voltage=13.60\n"
    "driver.name=OpenUPSDriver\n"
    "input.current=1.24\n"
    "input.voltage=12.3\n"
    "output.current=1.34\n"
    "output.voltage=12.2\n"
    "ups.load=11\n"
    "ups.mfr=WalleCube\n"
    "ups.model=Smart UPS W150\n"
    "ups.realpower.nominal=150\n"
    "ups.realpower=16\n"
    "ups.serial=E4B323491C40\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.discharging=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.temperature=5.5\n"
    "ups.status=OL\n";
static const char* const kOpenupsPollW150Before = "quick: 2/3 | full: 1/3 2/3 3/3";

// After US-078, every value from openupsHid2Nut(). Differences from kOpenupsSnapshotW150Before:
// - battery.capacity, battery.charge.warning: rows 304 and 307, not read before;
// - battery.current 0.01 -> 0.008, input.current 1.24 -> 1.237, output.current 1.34 -> 1.340:
//   "%.3f" of row 303 and of openups_scale_ccharge/cdischarge_fun;
// - input.voltage 12.3 -> 12.33, output.voltage 12.2 -> 12.24: "%.2f" of
//   openups_scale_vin/vout_fun (HID values 123.3 and 122.4, x 0.1 for PID 0xD005);
// - battery.temperature 29.5 -> 29.47: "%.2f" of openups_temperature_fun;
// - ups.temperature gone: UPS.PowerSummary.Temperature is the thermistor reading of row 309;
// - ups.status.commfault, .off, .overheat: internal keys of rows 326, 327 and 329, no change
//   in ups.status (OL; BatteryPresent = 0 still ignored, issue #26).
// Unchanged: ups.load, ups.realpower(.nominal) (OpenUPSDriver hook, ADR 0011 rule 2).
// battery.type and battery.mfr.date: rows 299 and 300, string indices 8 and 9, filled since the
// replay passes the strings again after the reports (issue #108); "OPEN" is iOEMInformation, as
// in NUT (openups-hid.c:300).
static const char* const kOpenupsSnapshotW150After =
    "battery.capacity=100\n"
    "battery.charge.low=10\n"
    "battery.charge.warning=5\n"
    "battery.charge=100\n"
    "battery.current=0.008\n"
    "battery.mfr.date=OPEN\n"
    "battery.runtime=12912\n"
    "battery.temperature=29.47\n"
    "battery.type=LiFePO4\n"
    "battery.voltage=13.60\n"
    "driver.name=OpenUPSDriver\n"
    "input.current=1.237\n"
    "input.voltage=12.33\n"
    "output.current=1.340\n"
    "output.voltage=12.24\n"
    "ups.load=11\n"
    "ups.mfr=WalleCube\n"
    "ups.model=Smart UPS W150\n"
    "ups.realpower.nominal=150\n"
    "ups.realpower=16\n"
    "ups.serial=E4B323491C40\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=0\n"
    "ups.status.commfault=0\n"
    "ups.status.discharging=0\n"
    "ups.status.off=0\n"
    "ups.status.overheat=0\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status.replace_battery=0\n"
    "ups.status.shutdown_imminent=0\n"
    "ups.status=OL\n";
// The poll is the generic one, unchanged
static const char* const kOpenupsPollW150After = kOpenupsPollW150Before;

void test_openups_snapshot_baseline(void) {
    static const SnapshotCase cases[] = {
        { "test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json", kOpenupsSnapshotW150After },
    };
    checkSnapshots(cases, sizeof(cases) / sizeof(cases[0]));
    TEST_ASSERT_EQUAL_STRING(kOpenupsPollW150After,
        FixtureReplayRunner::pollLists<OpenUPSDriver>("test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json").c_str());
}

// US-106: the Keor PDU 800 (issue #90) and VI 2200 SHL (issue #96) fixtures before the migration to
// legrandHid2Nut() / liebertHid2Nut(), with the hand-written mappings of LegrandDriver (US-087) and
// LiebertDriver (US-097), kept as the reference of the differences below
static const char* const kLegrandSnapshotKeorBefore =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime=2160\n"
    "battery.voltage.nominal=12\n"
    "battery.voltage=13.70\n"
    "driver.name=LegrandDriver\n"
    "input.frequency=50.0\n"
    "input.transfer.high=270\n"
    "input.transfer.low=180\n"
    "input.voltage.nominal=230\n"
    "input.voltage=230.2\n"
    "output.frequency=50.0\n"
    "output.voltage=230.2\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=20\n"
    "ups.delay.start=30\n"
    "ups.load=25\n"
    "ups.mfr=Legrand\n"
    "ups.model=KEOR PDU 800\n"
    "ups.realpower.nominal=480\n"
    "ups.realpower=120\n"
    "ups.serial=                \n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status=OL\n";
static const char* const kLiebertSnapshotVi2200Before =
    "battery.charge=30\n"
    "battery.runtime=1007\n"
    "battery.voltage=27.60\n"
    "driver.name=LiebertDriver\n"
    "experimental.battery.capacity.nominal=100\n"
    "experimental.battery.capacity=100\n"
    "input.voltage=230.0\n"
    "output.frequency.nominal=50\n"
    "output.frequency=49.60\n"
    "output.voltage.nominal=230\n"
    "output.voltage=230.0\n"
    "ups.beeper.status=disabled\n"
    "ups.delay.shutdown=20\n"
    "ups.delay.start=30\n"
    "ups.load=0\n"
    "ups.mfr=PPC\n"
    "ups.model=Offline UPS\n"
    "ups.power.nominal=9\n"
    "ups.realpower=0\n"
    "ups.serial=000000000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=1\n"
    "ups.status.discharging=0\n"
    "ups.status.good=1\n"
    "ups.status.internal_failure=0\n"
    "ups.status.overload=0\n"
    "ups.status.trim=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL CHRG\n";
// After US-106, every value from legrandHid2Nut() but the extensions. Differences from
// kLegrandSnapshotKeorBefore:
// - input.voltage 230.2 -> 230, output.voltage 230.2 -> 230, output.frequency 50.0 -> 50: "%.0f" of
//   legrand-hid.c:129, 149 and 151 (no conversion upstream);
// - battery.voltage 13.70 -> 13.7, battery.voltage.nominal 12 -> 12.0: rows 141 and 139 without
//   divide_by_10_conversion, with its "%0.1f" (US-087, user decision of 2026-10-30).
// Unchanged: ups.status OL, input.frequency (generic UPS.Input.Frequency, ADR 0011 rule 2), the
// FullyCharged / RemainingTimeLimitExpired flags, ups.beeper.status (Legrand hook).
static const char* const kLegrandSnapshotKeorAfter =
    "battery.charge.low=10\n"
    "battery.charge.warning=20\n"
    "battery.charge=100\n"
    "battery.runtime=2160\n"
    "battery.voltage.nominal=12.0\n"
    "battery.voltage=13.7\n"
    "driver.name=LegrandDriver\n"
    "input.frequency=50.0\n"
    "input.transfer.high=270\n"
    "input.transfer.low=180\n"
    "input.voltage.nominal=230\n"
    "input.voltage=230\n"
    "output.frequency=50\n"
    "output.voltage=230\n"
    "ups.beeper.status=enabled\n"
    "ups.delay.shutdown=20\n"
    "ups.delay.start=30\n"
    "ups.load=25\n"
    "ups.mfr=Legrand\n"
    "ups.model=KEOR PDU 800\n"
    "ups.realpower.nominal=480\n"
    "ups.realpower=120\n"
    "ups.serial=                \n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.charging=1\n"
    "ups.status.discharging=0\n"
    "ups.status.fully_charged=1\n"
    "ups.status.overload=0\n"
    "ups.status.remaining_time_limit_expired=0\n"
    "ups.status=OL\n";
// After US-106, every value from liebertHid2Nut() but the extensions. Differences from
// kLiebertSnapshotVi2200Before (end of the second scenario, PresentStatus.Good = 1):
// - ups.status.good -> ups.status.off, ups.status.internal_failure -> ups.status.commfault: internal
//   keys of off_info and commfault_info (rows 126-127), no change in ups.status. On the first
//   scenario (Good = 0 on mains) ups.status is OL CHRG OFF, as usbhid-ups (fixture expectations).
// Unchanged: every value, ups.timer.shutdown (generic PowerSummary.DelayBeforeShutdown, ADR 0011
// rule 2). The replay passes the strings before the reports: no battery.type here.
static const char* const kLiebertSnapshotVi2200After =
    "battery.charge=30\n"
    "battery.runtime=1007\n"
    "battery.voltage=27.60\n"
    "driver.name=LiebertDriver\n"
    "experimental.battery.capacity.nominal=100\n"
    "experimental.battery.capacity=100\n"
    "input.voltage=230.0\n"
    "output.frequency.nominal=50\n"
    "output.frequency=49.60\n"
    "output.voltage.nominal=230\n"
    "output.voltage=230.0\n"
    "ups.beeper.status=disabled\n"
    "ups.delay.shutdown=20\n"
    "ups.delay.start=30\n"
    "ups.load=0\n"
    "ups.mfr=PPC\n"
    "ups.model=Offline UPS\n"
    "ups.power.nominal=9\n"
    "ups.realpower=0\n"
    "ups.serial=000000000\n"
    "ups.status.ac_present=1\n"
    "ups.status.battery_low=0\n"
    "ups.status.boost=0\n"
    "ups.status.charging=1\n"
    "ups.status.commfault=0\n"
    "ups.status.discharging=0\n"
    "ups.status.off=0\n"
    "ups.status.overload=0\n"
    "ups.status.trim=0\n"
    "ups.test.result=No test initiated\n"
    "ups.timer.shutdown=-1\n"
    "ups.status=OL CHRG\n";
static const char* const kLegrandPollKeorBefore =
    "quick: 8/3 11/3 19/3 23/3 27/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 28/3 18/3 "
    "19/3 20/3 21/3 22/3 23/3 24/3 29/3 27/3 25/3 26/3 50/3 51/3 52/3 53/3 54/3 55/3 56/3 57/3 58/3";
static const char* const kLiebertPollVi2200Before =
    "quick: 45/3 7/3 1/3 6/3 | full: 36/3 116/3 54/3 114/3 74/3 49/3 45/3 66/3 19/3 9/3 10/3 16/3 226/3 7/3 1/3 11/3 6/3";

void test_legrand_liebert_snapshot_baseline(void) {
    static const SnapshotCase cases[] = {
        { "test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json", kLegrandSnapshotKeorAfter },
        { "test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json", kLiebertSnapshotVi2200After },
    };
    checkSnapshots(cases, sizeof(cases) / sizeof(cases[0]));
    // The poll is the generic one: the lists are those of US-087 / US-097, unchanged
    TEST_ASSERT_TRUE(strcmp(kLegrandSnapshotKeorBefore, kLegrandSnapshotKeorAfter) != 0);
    TEST_ASSERT_TRUE(strcmp(kLiebertSnapshotVi2200Before, kLiebertSnapshotVi2200After) != 0);
    TEST_ASSERT_EQUAL_STRING(kLegrandPollKeorBefore, FixtureReplayRunner::pollLists<LegrandDriver>("test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json").c_str());
    TEST_ASSERT_EQUAL_STRING(kLiebertPollVi2200Before, FixtureReplayRunner::pollLists<LiebertDriver>("test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json").c_str());
}

#ifdef PIO_UNIT_TESTING
#ifndef ARDUINO
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_replay_eaton_3s);
    RUN_TEST(test_replay_eaton_5p);
    RUN_TEST(test_replay_eaton_ellipse_pro_650_issue110);
    RUN_TEST(test_replay_mge_protection_center_issue111);
    RUN_TEST(test_replay_eaton_ellipse_eco_650_issue112);
    RUN_TEST(test_replay_apc_cs500);
    RUN_TEST(test_replay_apc_rs900mi);
    RUN_TEST(test_replay_apc_xs700u);
    RUN_TEST(test_replay_apc_smartups750);
    RUN_TEST(test_apc_5g_reads_feature_reports_issue22);
    RUN_TEST(test_apc_5g_input_report_ignored_issue22);
    RUN_TEST(test_spd750u_beeper_value_outside_table_keeps_commands_issue36);
    RUN_TEST(test_spd750u_beeper_disabled_offers_toggle);
    RUN_TEST(test_replay_apc_smartups750_issue13);
    RUN_TEST(test_replay_apc_cs750rs_issue48);
    RUN_TEST(test_replay_apc_bx1500g_issue55);
    RUN_TEST(test_replay_apc_bx750mi_issue60);
    RUN_TEST(test_replay_apc_xs1500m_issue94);
    RUN_TEST(test_replay_apc_es700g_issue71);
    RUN_TEST(test_replay_apc_bgm2200_issue98);
    RUN_TEST(test_replay_apc_bgm2200_issue98_online);
    RUN_TEST(test_replay_apc_bgm2200_issue98_onbattery);
    RUN_TEST(test_replay_apc_smartups750_issue78);
    RUN_TEST(test_replay_powercom_spd750u);
    RUN_TEST(test_replay_legrand_keor_pdu800_issue90);
    RUN_TEST(test_replay_cyberpower_br700elcd);
    RUN_TEST(test_replay_cyberpower_cp1350c);
    RUN_TEST(test_replay_cyberpower_cp1500epfclcd);
    RUN_TEST(test_replay_cyberpower_cp1600epfclcd_issue48);
    RUN_TEST(test_replay_cyberpower_cp2000eipfcrm2u_issue108);
    RUN_TEST(test_replay_cyberpower_cp2000eipfcrm2u_issue108_report4);
    RUN_TEST(test_replay_cyberpower_ut850eg_discussion95);
    RUN_TEST(test_replay_phoenixtec_powerwalker_vi2200shl_issue96);
    RUN_TEST(test_replay_openups_wallecube_w150);
    RUN_TEST(test_set_cycle_eaton_3s_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_mfr_date);
    RUN_TEST(test_set_cycle_cp1600_transfer_low);
    RUN_TEST(test_set_cycle_apc_bx750mi_sensitivity);
    RUN_TEST(test_status_baseline_all_fixtures);
    RUN_TEST(test_golden_registry_all_fixtures);
    RUN_TEST(test_golden_generic_all_fixtures);
    RUN_TEST(test_semi_static_reports_skipped_after_first_full_poll);
    RUN_TEST(test_cyberpower_snapshot_baseline);
    RUN_TEST(test_apc_snapshot_baseline);
    RUN_TEST(test_apc_poll_baseline);
    RUN_TEST(test_powercom_snapshot_baseline);
    RUN_TEST(test_openups_snapshot_baseline);
    RUN_TEST(test_legrand_liebert_snapshot_baseline);
    RUN_TEST(test_eaton_3s_need_replacement_alarm);
    RUN_TEST(test_eaton_3s_vendor_and_bit_alarms);
    RUN_TEST(test_eaton_3s_good_zero_is_off);
    return UNITY_END();
}
#else
void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_replay_eaton_3s);
    RUN_TEST(test_replay_eaton_5p);
    RUN_TEST(test_replay_eaton_ellipse_pro_650_issue110);
    RUN_TEST(test_replay_mge_protection_center_issue111);
    RUN_TEST(test_replay_eaton_ellipse_eco_650_issue112);
    RUN_TEST(test_replay_apc_cs500);
    RUN_TEST(test_replay_apc_rs900mi);
    RUN_TEST(test_replay_apc_xs700u);
    RUN_TEST(test_replay_apc_smartups750);
    RUN_TEST(test_apc_5g_reads_feature_reports_issue22);
    RUN_TEST(test_apc_5g_input_report_ignored_issue22);
    RUN_TEST(test_spd750u_beeper_value_outside_table_keeps_commands_issue36);
    RUN_TEST(test_spd750u_beeper_disabled_offers_toggle);
    RUN_TEST(test_replay_apc_smartups750_issue13);
    RUN_TEST(test_replay_apc_cs750rs_issue48);
    RUN_TEST(test_replay_apc_bx1500g_issue55);
    RUN_TEST(test_replay_apc_bx750mi_issue60);
    RUN_TEST(test_replay_apc_xs1500m_issue94);
    RUN_TEST(test_replay_apc_es700g_issue71);
    RUN_TEST(test_replay_apc_bgm2200_issue98);
    RUN_TEST(test_replay_apc_bgm2200_issue98_online);
    RUN_TEST(test_replay_apc_bgm2200_issue98_onbattery);
    RUN_TEST(test_replay_apc_smartups750_issue78);
    RUN_TEST(test_replay_powercom_spd750u);
    RUN_TEST(test_replay_legrand_keor_pdu800_issue90);
    RUN_TEST(test_replay_cyberpower_br700elcd);
    RUN_TEST(test_replay_cyberpower_cp1350c);
    RUN_TEST(test_replay_cyberpower_cp1500epfclcd);
    RUN_TEST(test_replay_cyberpower_cp1600epfclcd_issue48);
    RUN_TEST(test_replay_cyberpower_cp2000eipfcrm2u_issue108);
    RUN_TEST(test_replay_cyberpower_cp2000eipfcrm2u_issue108_report4);
    RUN_TEST(test_replay_cyberpower_ut850eg_discussion95);
    RUN_TEST(test_replay_phoenixtec_powerwalker_vi2200shl_issue96);
    RUN_TEST(test_replay_openups_wallecube_w150);
    RUN_TEST(test_set_cycle_eaton_3s_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_mfr_date);
    RUN_TEST(test_set_cycle_cp1600_transfer_low);
    RUN_TEST(test_set_cycle_apc_bx750mi_sensitivity);
    RUN_TEST(test_status_baseline_all_fixtures);
    RUN_TEST(test_golden_registry_all_fixtures);
    RUN_TEST(test_golden_generic_all_fixtures);
    RUN_TEST(test_semi_static_reports_skipped_after_first_full_poll);
    RUN_TEST(test_cyberpower_snapshot_baseline);
    RUN_TEST(test_apc_snapshot_baseline);
    RUN_TEST(test_apc_poll_baseline);
    RUN_TEST(test_powercom_snapshot_baseline);
    RUN_TEST(test_openups_snapshot_baseline);
    RUN_TEST(test_legrand_liebert_snapshot_baseline);
    RUN_TEST(test_eaton_3s_need_replacement_alarm);
    RUN_TEST(test_eaton_3s_vendor_and_bit_alarms);
    RUN_TEST(test_eaton_3s_good_zero_is_off);
    UNITY_END();
}
void loop() {}
#endif
#endif

