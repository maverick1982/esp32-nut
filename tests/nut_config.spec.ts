import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('NUT Configuration UI', () => {
  test.beforeEach(async ({ page }) => {
    // Serve static files over HTTP
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

  test('submits NUT credentials successfully', async ({ page }) => {
    // Intercept the connect calls
    await page.route('**/api/nut/config', async route => {
      expect(route.request().method()).toBe('POST');
      const payload = route.request().postDataJSON();
      expect(payload.username).toBe('admin');
      expect(payload.password).toBe('secret');
      await route.fulfill({ json: { success: true } });
    });

    await page.goto('http://esp32.local/');
    
    // Switch to NUT tab
    await page.click('button[data-target="nut"]');
    
    // Wait for panel
    const nutTab = page.locator('#content-nut');
    await expect(nutTab).toHaveClass(/active/);

    // Fill form
    await page.fill('#nut-upsname', 'eaton');
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');
    
    // Submit form
    await page.click('#btn-save-nut');
    
    // Verify the UI changes state
    await expect(page.locator('#btn-save-nut')).toHaveText(/Saved!/i);
  });
});
