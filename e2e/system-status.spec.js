import { test, expect } from '@playwright/test';

test.skip('Status indicators update correctly from API', async ({ page }) => {
  // Intercept the API call to mock the response
  await page.route('**/api/system-status', async route => {
    const json = {
      wifi: { status: 'HomeNetwork' },
      ups: { status: 'Eaton 3S' }
    };
    await route.fulfill({ json });
  });

  // Navigate to the dashboard
  const path = require('path');
  await page.goto(`file:///${path.resolve(__dirname, '../data/www/index.html').replace(/\\/g, '/')}`);

  // Check that the indicators are updated correctly after polling
  const wifiLabel = page.locator('#lbl-wifi');
  const upsLabel = page.locator('#lbl-ups');

  await expect(wifiLabel).toHaveText('Wi-Fi: HomeNetwork');
  await expect(upsLabel).toHaveText('UPS: Eaton 3S');
  
  const wifiIndicator = page.locator('#ind-wifi');
  await expect(wifiIndicator).toHaveClass(/success/);
  
  const upsIndicator = page.locator('#ind-ups');
  await expect(upsIndicator).toHaveClass(/success/);
});
