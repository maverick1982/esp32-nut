import { test, expect } from '@playwright/test';
import { serveIndex, serveUpdatePage } from './support/webui';

// Regenerates the README screenshots in docs/images/ (npm run e2e:screenshots).
// Every API the pages call is mocked with the data of an Eaton 3S 700.

// Commands of an Eaton 3S (mge-hid): beeper and power control, no battery test
const EATON_3S_COMMANDS = [
  { name: 'beeper.disable', description: 'Disable the UPS beeper', destructive: false },
  { name: 'beeper.enable', description: 'Enable the UPS beeper', destructive: false },
  { name: 'beeper.mute', description: 'Temporarily mute the UPS beeper', destructive: false },
  { name: 'load.off', description: 'Turn off the load immediately', destructive: true },
  { name: 'load.off.delay', description: 'Turn off the load with a delay (seconds)', destructive: true },
  { name: 'load.on', description: 'Turn on the load immediately', destructive: true },
  { name: 'load.on.delay', description: 'Turn on the load with a delay (seconds)', destructive: true },
  { name: 'shutdown.return', description: 'Turn off the load and return when power is back', destructive: true },
  { name: 'shutdown.stayoff', description: 'Turn off the load and remain off', destructive: true },
  { name: 'shutdown.stop', description: 'Stop a shutdown in progress', destructive: true },
];

// Writable variables of an Eaton 3S as returned by GET /api/ups/variables
const EATON_3S_VARIABLES = [
  { name: 'battery.charge.low', description: 'Remaining battery level when UPS switches to LB (percent)', value: '20', max_length: 10, enum: [], driver_side: false, confirm: false },
  { name: 'battery.runtime.low', description: 'Remaining battery runtime when UPS switches to LB (seconds)', value: '180', max_length: 10, enum: [], driver_side: false, confirm: false },
  { name: 'input.sensitivity', description: 'Input power sensitivity', value: 'normal', max_length: 0, enum: ['high', 'normal', 'low'], driver_side: false, confirm: false },
  { name: 'ups.shutdown', description: 'Enable or disable UPS shutdown ability (poweroff)', value: 'enabled', max_length: 0, enum: ['enabled', 'disabled'], driver_side: false, confirm: true },
  { name: 'ups.delay.shutdown', description: 'Interval to wait after shutdown with delay command (seconds)', value: '20', max_length: 10, enum: [], driver_side: true, confirm: false },
  { name: 'ups.delay.start', description: 'Interval to wait before (re)starting the load (seconds)', value: '30', max_length: 10, enum: [], driver_side: true, confirm: false },
];

test.describe('Generate Screenshots', () => {
  test.beforeEach(async ({ page }) => {
    // Serve the real Web UI (index + OTA page) as the firmware does
    await serveIndex(page);
    await serveUpdatePage(page);

    // Mock API responses
    await page.route('**/api/config', route => route.fulfill({
      json: {
        wifi: { mode: 'STA', ssid: 'MyHomeNetwork' },
        nut: { username: 'homeassistant', ups_name: 'ups' },
      },
    }));

    await page.route('**/api/system-status', route => route.fulfill({
      json: {
        wifi: { status: 'Connected', ssid: 'MyHomeNetwork', ip: '192.168.1.100' },
        ups: { status: 'Connected' },
        version: 'v1.8.0',
        device_id: 'ESP32-S3-A1B2C3',
      },
    }));

    await page.route('**/api/auth/status', route => route.fulfill({
      json: { configured: true, authenticated: true, ap_mode: false, username: 'admin' },
    }));

    await page.route('**/api/ups-vars', route => route.fulfill({
      json: {
        'ups.status': 'OL',
        'battery.charge': '100',
        'ups.load': '25',
        'battery.runtime': '2400',
        'input.voltage': '230.5',
        'output.voltage': '230.5',
        'ups.beeper.status': 'enabled',
        'ups.model': 'Eaton 3S 700',
        'ups.mfr': 'EATON',
        'ups.realpower': '150',
      },
    }));

    await page.route('**/api/ups/commands', route => route.fulfill({
      json: { connected: true, commands: EATON_3S_COMMANDS },
    }));

    await page.route('**/api/ups/variables', route => route.fulfill({
      json: { connected: true, variables: EATON_3S_VARIABLES },
    }));

    await page.route('**/api/logs', route => route.fulfill({
      json: [
        { id: 1, level: 'INFO', msg: '--- ESP32 NUT Server Initialized ---' },
        { id: 2, level: 'INFO', msg: '[NETWORK] Connection established! IP Address: 192.168.1.100' },
        { id: 3, level: 'INFO', msg: '[MAIN] NUTServer started correctly on port 3493.' },
        { id: 4, level: 'INFO', msg: 'UPS interface claimed and ready (IN endpoint MPS 8).' },
        { id: 5, level: 'INFO', msg: '[USB] First full poll done after 1671 ms: data served' },
        { id: 6, level: 'INFO', msg: '[NUT] Clients: 2/8 in use - 192.168.1.10, 192.168.1.20' },
      ],
    }));
  });

  test('capture tabs', async ({ page }) => {
    await page.setViewportSize({ width: 1280, height: 800 });
    await page.goto('http://esp32.local/');
    await page.evaluate(() => document.fonts.ready);

    // 1. UPS Telemetry (default tab): wait for the mocked telemetry
    await page.locator('.tab[data-target="ups"]').click();
    await expect(page.locator('#content-ups')).toHaveClass(/active/);
    await expect(page.locator('#ups-charge')).toHaveText('100');
    await expect(page.locator('#row-ups-model')).toBeVisible();
    await page.screenshot({ path: 'docs/images/ui-ups.png', animations: 'disabled' });

    // 2. UPS Commands
    await page.locator('.tab[data-target="commands"]').click();
    await expect(page.locator('#content-commands')).toHaveClass(/active/);
    await expect(page.locator('.cmd-row')).toHaveCount(EATON_3S_COMMANDS.length);
    await page.screenshot({ path: 'docs/images/ui-commands.png', animations: 'disabled' });

    // 3. UPS Settings
    await page.locator('.tab[data-target="settings"]').click();
    await expect(page.locator('#content-settings')).toHaveClass(/active/);
    await expect(page.locator('.cmd-row.set-row')).toHaveCount(EATON_3S_VARIABLES.length);
    await page.screenshot({ path: 'docs/images/ui-settings.png', animations: 'disabled' });

    // 4. Wi-Fi
    await page.locator('.tab[data-target="wifi"]').click();
    await expect(page.locator('#content-wifi')).toHaveClass(/active/);
    await expect(page.locator('#content-wifi input').first()).toHaveValue('MyHomeNetwork');
    await page.screenshot({ path: 'docs/images/ui-wifi.png', animations: 'disabled' });

    // 5. NUT Server
    await page.locator('.tab[data-target="nut"]').click();
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await page.screenshot({ path: 'docs/images/ui-nut.png', animations: 'disabled' });

    // 6. Web Login
    await page.locator('.tab[data-target="weblogin"]').click();
    await expect(page.locator('#content-weblogin')).toHaveClass(/active/);
    await page.screenshot({ path: 'docs/images/ui-weblogin.png', animations: 'disabled' });

    // 7. System Logs (wait for the mocked log lines)
    await page.locator('.tab[data-target="logs"]').click();
    await expect(page.locator('#content-logs')).toHaveClass(/active/);
    await expect(page.locator('#terminal-output')).toContainText('Clients: 2/8 in use');
    await page.screenshot({ path: 'docs/images/ui-logs.png', animations: 'disabled' });

    // 8. OTA page (separate page served at /update)
    await page.goto('http://esp32.local/update');
    await expect(page.locator('.dropzone')).toBeVisible();
    await page.screenshot({ path: 'docs/images/ui-ota.png', animations: 'disabled' });
  });
});
