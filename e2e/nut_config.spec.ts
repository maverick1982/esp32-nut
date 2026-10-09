import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

test.describe('NUT Configuration UI', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
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
