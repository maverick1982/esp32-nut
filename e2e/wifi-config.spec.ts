import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

test.describe('Wi-Fi Configuration UI', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('renders the main interface correctly', async ({ page }) => {
    await page.goto('http://esp32.local/');
    await page.click('button[data-target=\"wifi\"]');
    
    // Verify title and headers
    await expect(page.locator('h1')).toHaveText('Wi-Fi Config');
    await expect(page.locator('#wifi-form')).toBeVisible();
    await expect(page.locator('#ssid')).toBeVisible();
  });

  test('submits the form successfully', async ({ page }) => {
    // Intercept the connect calls
    await page.route('**/api/wifi/connect', async route => {
      expect(route.request().method()).toBe('POST');
      await route.fulfill({ json: { success: true } });
    });

    // Mock window.alert so the test doesn't hang
    await page.addInitScript(() => {
      window.alert = () => {};
    });

    await page.goto('http://esp32.local/');
    await page.click('button[data-target=\"wifi\"]');
    
    // Fill SSID and password manually
    await page.fill('#ssid', 'MyNetwork');
    await page.fill('#password', 'SecretPassword');
    
    // Submit form
    await page.click('#btn-connect');
    
    // Verify the UI changes state
    await expect(page.locator('#btn-connect')).toHaveText(/Saved & Connecting/i);
    await expect(page.locator('#lbl-wifi')).toHaveText('Restarting...');
  });
});
