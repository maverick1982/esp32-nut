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
        { "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json", "OL", nullptr, nullptr },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_br700elcd_vid0764_pid0501.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp1350c_vid0764_pid0501.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp1500epfclcd_vid0764_pid0501.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_cp1600epfclcd_vid0764_pid0601_issue48.json", "OL", nullptr, nullptr },
        { "test/fixtures/cyberpower/cyberpower_ut850eg_vid0764_pid0501_discussion95.json", "OL", nullptr, nullptr },
        { "test/fixtures/eaton/eaton_3s_vid0463_pidffff.json", "OL", nullptr, nullptr },
        { "test/fixtures/eaton/eaton_5p_vid0463_pidffff_issue18.json", "Unknown", nullptr, nullptr },
        { "test/fixtures/legrand/legrand_keor_pdu800_vid1cb0_pid0038_issue90.json", "OL", nullptr, nullptr },
        { "test/fixtures/openups/wallecube_w150_vid04d8_pidd005.json", "OL", nullptr, nullptr },
        { "test/fixtures/phoenixtec/powerwalker_vi2200shl_vid06da_pidffff_issue96.json", "OL CHRG", nullptr, nullptr },
        { "test/fixtures/powercom/powercom_spd750u_vid0d9f_pid0004_issue21.json", "Unknown", nullptr, nullptr },
    };
    TEST_ASSERT_EQUAL_MESSAGE(24, (int)(sizeof(rows) / sizeof(rows[0])), "one row per fixture in test/fixtures");
    for (const auto& r : rows) FixtureReplayRunner::runStatusBaseline(r.file, r.status, r.alarm, r.reason);
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
static const char* const kApcPollBackupsXs700uPid0002Issue20 =
    "quick: 6/3 12/3 19/3 20/3 22/3 34/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 7/3 8/3 9/3 10/3 11/3 12/3 13/3 14/3 15/3 16/3 17/3 18/3 19/3 20/3 21/3 22/3 23/3 24/3 28/3 32/3 33/3 34/3";
static const char* const kApcPollSmartups750Pid0002Issue13 =
    "quick: 12/3 13/3 7/3 24/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 22/3 23/3 27/3 28/3 21/3 24/3 26/3";
static const char* const kApcPollSmartups750Pid0002Issue78 =
    "quick: 12/3 13/3 7/3 24/3 25/3 44/3 51/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 22/3 23/3 27/3 28/3 21/3 24/3 26/3 25/3 30/3 31/3 29/3 32/3 33/3 34/3 44/3 66/3 64/3 42/3 43/3 45/3 35/3 36/3 37/3 38/3 39/3 40/3 41/3 46/3 47/3 48/3 49/3 52/3 55/3 50/3 53/3 62/3 63/3 54/3 51/3";
static const char* const kApcPollSmartups750Pid0003Issue22 =
    "quick: 12/3 13/3 7/3 | full: 1/3 2/3 3/3 4/3 5/3 6/3 14/3 12/3 16/3 15/3 17/3 9/3 13/3 18/3 19/3 8/3 10/3 11/3 20/3 7/3 137/1 144/3 150/3 141/3 142/3 147/3 148/3 146/3";

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
        { "test/fixtures/apc/apc_backups_xs700u_vid051d_pid0002_issue20.json", kApcPollBackupsXs700uPid0002Issue20 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue13.json", kApcPollSmartups750Pid0002Issue13 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0002_issue78.json", kApcPollSmartups750Pid0002Issue78 },
        { "test/fixtures/apc/apc_smartups_750_vid051d_pid0003_issue22.json", kApcPollSmartups750Pid0003Issue22 },
    };
    for (const auto& c : cases) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expected, FixtureReplayRunner::pollLists<APCDriver>(c.file).c_str(), c.file);
    }
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
    RUN_TEST(test_replay_cyberpower_ut850eg_discussion95);
    RUN_TEST(test_replay_phoenixtec_powerwalker_vi2200shl_issue96);
    RUN_TEST(test_replay_openups_wallecube_w150);
    RUN_TEST(test_set_cycle_eaton_3s_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_mfr_date);
    RUN_TEST(test_set_cycle_cp1600_transfer_low);
    RUN_TEST(test_set_cycle_apc_bx750mi_sensitivity);
    RUN_TEST(test_status_baseline_all_fixtures);
    RUN_TEST(test_cyberpower_snapshot_baseline);
    RUN_TEST(test_apc_snapshot_baseline);
    RUN_TEST(test_apc_poll_baseline);
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
    RUN_TEST(test_replay_apc_cs500);
    RUN_TEST(test_replay_apc_rs900mi);
    RUN_TEST(test_replay_apc_xs700u);
    RUN_TEST(test_replay_apc_smartups750);
    RUN_TEST(test_replay_apc_smartups750_issue13);
    RUN_TEST(test_replay_apc_cs750rs_issue48);
    RUN_TEST(test_replay_apc_bx1500g_issue55);
    RUN_TEST(test_replay_apc_bx750mi_issue60);
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
    RUN_TEST(test_replay_cyberpower_ut850eg_discussion95);
    RUN_TEST(test_replay_phoenixtec_powerwalker_vi2200shl_issue96);
    RUN_TEST(test_replay_openups_wallecube_w150);
    RUN_TEST(test_set_cycle_eaton_3s_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_charge_low);
    RUN_TEST(test_set_cycle_apc_cs750rs_mfr_date);
    RUN_TEST(test_set_cycle_cp1600_transfer_low);
    RUN_TEST(test_set_cycle_apc_bx750mi_sensitivity);
    RUN_TEST(test_status_baseline_all_fixtures);
    RUN_TEST(test_cyberpower_snapshot_baseline);
    RUN_TEST(test_apc_snapshot_baseline);
    RUN_TEST(test_apc_poll_baseline);
    RUN_TEST(test_eaton_3s_need_replacement_alarm);
    RUN_TEST(test_eaton_3s_vendor_and_bit_alarms);
    RUN_TEST(test_eaton_3s_good_zero_is_off);
    UNITY_END();
}
void loop() {}
#endif
#endif

