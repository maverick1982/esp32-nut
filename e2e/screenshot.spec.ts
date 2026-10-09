import { test, expect } from '@playwright/test';
import { serveIndex, serveUpdatePage } from './support/webui';

test.describe('Generate Screenshots', () => {
  test.beforeEach(async ({ page }) => {
    // Serve the real Web UI (index + OTA page) as the firmware does
    await serveIndex(page);
    await serveUpdatePage(page);

    // Mock API responses
    await page.route('**/api/config', async route => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          wifi_ssid: 'MyHomeNetwork',
          nut_user: 'homeassistant',
          nut_pass: 'secretpassword'
        })
      });
    });

    await page.route('**/api/system-status', async route => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          wifi: { status: 'Connected', ip: '192.168.1.100' },
          ups: { status: 'Eaton 3S 700' }
        })
      });
    });

    await page.route('**/api/ups-vars', async route => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({
          'ups.status': 'OL CHRG',
          'battery.charge': '100',
          'ups.load': '25',
          'battery.runtime': '2400',
          'input.voltage': '230.5',
          'output.voltage': '230.5',
          'ups.beeper.status': 'enabled',
          'ups.model': 'Eaton 3S 700',
          'ups.mfr': 'EATON',
          'ups.realpower': '150'
        })
      });
    });

    await page.route('**/api/logs', async route => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify([
          { id: 1, level: 'INFO', msg: 'System Boot' },
          { id: 2, level: 'INFO', msg: 'Wi-Fi Connected to MyHomeNetwork' },
          { id: 3, level: 'INFO', msg: 'IP Assigned: 192.168.1.100' },
          { id: 4, level: 'INFO', msg: 'UPS Found: Eaton 3S 700' },
          { id: 5, level: 'WARN', msg: 'NUT Server listening on port 3493' }
        ])
      });
    });
  });

  test('capture tabs', async ({ page }) => {
    // Imposta una dimensione adatta
    await page.setViewportSize({ width: 1024, height: 768 });
    await page.goto('http://esp32.local/');

    // 1. Wi-Fi Tab (the default tab is UPS Telemetry since US-049: select it explicitly)
    await page.locator('.tab[data-target="wifi"]').click();
    await expect(page.locator('#content-wifi')).toHaveClass(/active/);
    await page.screenshot({ path: 'docs/images/ui-wifi.png', animations: 'disabled' });

    // 2. NUT Tab
    await page.locator('.tab[data-target="nut"]').click();
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await page.screenshot({ path: 'docs/images/ui-nut.png', animations: 'disabled' });

    // 3. UPS Tab (wait for the mocked telemetry to be rendered)
    await page.locator('.tab[data-target="ups"]').click();
    await expect(page.locator('#content-ups')).toHaveClass(/active/);
    await expect(page.locator('#ups-charge')).toHaveText('100');
    await page.screenshot({ path: 'docs/images/ui-ups.png', animations: 'disabled' });

    // 4. Logs Tab (wait for the mocked log lines)
    await page.locator('.tab[data-target="logs"]').click();
    await expect(page.locator('#content-logs')).toHaveClass(/active/);
    await expect(page.locator('#terminal-output')).toContainText('NUT Server listening on port 3493');
    await page.screenshot({ path: 'docs/images/ui-logs.png', animations: 'disabled' });

    // 5. OTA page (separate page served at /update)
    await page.goto('http://esp32.local/update');
    await expect(page.locator('.dropzone')).toBeVisible();
    await page.screenshot({ path: 'docs/images/ui-ota.png', animations: 'disabled' });
  });
});
