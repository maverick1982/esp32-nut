import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('UPS Parameters UI', () => {
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
    // Mock the global config so app.js doesn't fail fetching it
    await page.route('**/api/config', async route => {
        await route.fulfill({ json: {} });
    });
  });

  test('polls and updates UPS parameters automatically', async ({ page }) => {
    // Initial mock data
    let mockData = {
      "ups.status": "OL",
      "ups.mfr": "Eaton",
      "battery.charge": "100",
      "battery.runtime": "3240",
      "ups.load": "15",
      "ups.realpower": "120",
      "input.voltage.nominal": "230",
      "output.voltage": "230.1"
    };

    await page.route('**/api/ups-vars', async route => {
      await route.fulfill({ json: mockData });
    });

    await page.goto('http://esp32.local/');

    // Click UPS tab
    await page.click('button[data-target="ups"]');
    
    // Check initial values
    await expect(page.locator('#ups-status')).toHaveText('OL');
    await expect(page.locator('#ups-charge')).toHaveText('100');
    await expect(page.locator('#ups-load')).toHaveText('15');
    await expect(page.locator('#ups-realpower')).toHaveText('120');

    // Check table rows and descriptions
    await expect(page.locator('#row-ups-status .ups-val')).toHaveText('OL');
    await expect(page.locator('#row-ups-status').locator('td').nth(2)).toHaveText('UPS status');
    
    await expect(page.locator('#row-ups-mfr .ups-val')).toHaveText('Eaton');
    await expect(page.locator('#row-ups-mfr').locator('td').nth(2)).toHaveText('UPS manufacturer');

    await expect(page.locator('#row-battery-runtime .ups-val')).toHaveText('3240');
    await expect(page.locator('#row-battery-runtime').locator('td').nth(2)).toHaveText('Battery runtime (seconds)');

    // Update mock data to simulate new reading
    mockData = {
      "ups.status": "OB",
      "ups.mfr": "Eaton",
      "battery.charge": "95",
      "battery.runtime": "1800",
      "ups.load": "45",
      "ups.realpower": "350",
      "input.voltage.nominal": "230",
      "output.voltage": "229.5"
    };

    // Wait for the polling interval (2 seconds) plus some buffer
    // and verify the UI updates
    await expect(page.locator('#ups-status')).toHaveText('OB', { timeout: 3500 });
    await expect(page.locator('#ups-charge')).toHaveText('95');
    await expect(page.locator('#ups-load')).toHaveText('45');
    await expect(page.locator('#ups-realpower')).toHaveText('350');
    await expect(page.locator('#row-battery-runtime .ups-val')).toHaveText('1800');
    
    // Check progress bars update visually
    const chargeWidth = await page.locator('#bar-charge').evaluate((el) => el.style.width);
    expect(chargeWidth).toBe('95%');
  });

  test('shows generic banner for Generic UPS', async ({ page }) => {
    let mockData = {
      "ups.status": "OL",
      "ups.type": "Generic"
    };

    await page.route('**/api/ups-vars', async route => {
      await route.fulfill({ json: mockData });
    });

    await page.goto('http://esp32.local/');
    await page.click('button[data-target="ups"]');
    
    await expect(page.locator('#generic-ups-banner')).toBeVisible();

    // Change to supported UPS
    mockData = {
      "ups.status": "OL",
      "ups.type": "Eaton"
    };
    
    // Wait for the next poll
    await expect(page.locator('#generic-ups-banner')).toBeHidden({ timeout: 3500 });
  });

  test('sorts table rows alphabetically by parameter name', async ({ page }) => {
    const mockData = {
      "ups.status": "OL",
      "battery.charge": "100",
      "ups.mfr": "Eaton",
      "input.voltage.nominal": "230",
      "battery.runtime": "3240",
      "output.voltage": "230.1"
    };

    await page.route('**/api/ups-vars', async route => {
      await route.fulfill({ json: mockData });
    });

    await page.goto('http://esp32.local/');
    await page.click('button[data-target="ups"]');

    // Wait for the table to render
    await page.waitForSelector('#ups-table-body tr');

    // Extract all parameter names from the first column
    const paramCells = page.locator('#ups-table-body tr td:first-child');
    await expect(paramCells.first()).toBeVisible();
    
    const params = await paramCells.allInnerTexts();
    
    // Check they are not empty and are sorted
    expect(params.length).toBeGreaterThan(0);
    const expectedSorted = [...params].sort();
    expect(params).toEqual(expectedSorted);
  });
});
