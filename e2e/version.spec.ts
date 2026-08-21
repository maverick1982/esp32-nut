import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('Dynamic Firmware Version', () => {
  test('should display firmware version fetched from API', async ({ page }) => {
    await page.route('http://esp32.local/', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/index.html') });
    });
    await page.route('**/*app.js*', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/app.js') });
    });

    // Intercept the API call to return a mock version
    await page.route('**/api/system-status', async route => {
      await route.fulfill({
        json: {
          wifi: { status: 'Connected' },
          ups: { status: 'Online' },
          version: 'v1.2.3-test'
        }
      });
    });

    // Also mock other endpoints to prevent errors
    await page.route('**/api/config', route => route.fulfill({ json: {} }));
    await page.route('**/api/ups-vars', route => route.fulfill({ json: {} }));
    await page.route('**/api/logs', route => route.fulfill({ json: [] }));

    // Load the mock HTTP URL
    await page.goto('http://esp32.local/');

    // Verify the version string is updated
    const versionSpan = page.locator('#fw-version');
    await expect(versionSpan).toBeVisible();
    await expect(versionSpan).toHaveText('v1.2.3-test');
    
    // Ensure "v1.0" is not present
    const brandName = page.locator('.brand-name');
    await expect(brandName).not.toContainText('v1.0');
  });
});
