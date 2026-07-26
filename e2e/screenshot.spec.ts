import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('Generate Screenshots', () => {
  test.beforeEach(async ({ page }) => {
    // Intercetta i file statici
    await page.route('http://esp32.local/', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/index.html') });
    });
    await page.route('http://esp32.local/shared.css', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/shared.css') });
    });
    await page.route('http://esp32.local/ups.css', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/ups.css') });
    });
    await page.route('http://esp32.local/app.js', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/app.js') });
    });
    await page.route('http://esp32.local/update.html', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/update.html') });
    });

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
    await page.waitForTimeout(500); // aspetta l'animazione e i dati

    // 1. Wi-Fi Tab
    await page.screenshot({ path: 'docs/images/ui-wifi.png' });

    // 2. NUT Tab
    await page.locator('.tab[data-target="nut"]').click();
    await page.waitForTimeout(500);
    await page.screenshot({ path: 'docs/images/ui-nut.png' });

    // 3. UPS Tab
    await page.locator('.tab[data-target="ups"]').click();
    await page.waitForTimeout(1000);
    await page.screenshot({ path: 'docs/images/ui-ups.png' });

    // 4. Logs Tab
    await page.locator('.tab[data-target="logs"]').click();
    await page.waitForTimeout(2500);
    await page.screenshot({ path: 'docs/images/ui-logs.png' });

    // 5. OTA Tab (separate page usually, but in app.js it might navigate or iframe)
    await page.goto('http://esp32.local/update.html');
    await page.waitForTimeout(300);
    await page.screenshot({ path: 'docs/images/ui-ota.png' });
  });
});
