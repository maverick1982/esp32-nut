import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('Default Tab', () => {
  test.beforeEach(async ({ page }) => {
    await page.route('http://esp32.local/', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/index.html') });
    });
    await page.route('**/*shared.css*', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/shared.css') });
    });
    await page.route('**/*app.js*', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/app.js') });
    });
    await page.route('**/*ups.css*', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/ups.css') });
    });
    await page.route('**/api/config', async route => {
      await route.fulfill({ json: {} });
    });
  });

  test('UPS Telemetry is the default tab and triggers polling', async ({ page }) => {
    let upsApiCalled = false;

    await page.route('**/api/ups-vars', async route => {
      upsApiCalled = true;
      await route.fulfill({ json: {
        'ups.status': 'OL',
        'battery.charge': '100',
        'ups.load': '20'
      }});
    });

    await page.route('**/api/system-status', async route => {
      await route.fulfill({ json: { wifi: { status: 'Connected' }, ups: { status: 'OL' } } });
    });

    await page.goto('http://esp32.local/');

    const activeTab = page.locator('.tab.active');
    await expect(activeTab).toHaveAttribute('data-target', 'ups');

    const activeContent = page.locator('.tab-content.active');
    await expect(activeContent).toHaveAttribute('id', 'content-ups');

    const upsStatus = page.locator('#ups-status');
    await expect(upsStatus).toHaveText('OL', { timeout: 3000 });
    
    expect(upsApiCalled).toBeTruthy();
  });
});
