import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('Wi-Fi Configuration UI', () => {
  test.beforeEach(async ({ page }) => {
    // Serve static files over HTTP to allow fetch to work without CORS/file issues
    await page.route('http://esp32.local/', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/index.html') });
    });
    await page.route('http://esp32.local/shared.css', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/shared.css') });
    });
    await page.route('http://esp32.local/app.js', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/app.js') });
    });
  });

  test('renders the main interface correctly', async ({ page }) => {
    await page.goto('http://esp32.local/');
    
    // Verify title and headers
    await expect(page.locator('h1')).toHaveText('Network Configuration');
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
    
    // Fill SSID and password manually
    await page.fill('#ssid', 'MyNetwork');
    await page.fill('#password', 'SecretPassword');
    
    // Submit form
    await page.click('#btn-connect');
    
    // Verify the UI changes state
    await expect(page.locator('#btn-connect')).toHaveText(/Saved & Connecting/i);
    await expect(page.locator('.system-status span')).toHaveText('Restarting...');
  });
});
