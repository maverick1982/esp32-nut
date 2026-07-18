import { test, expect } from '@playwright/test';

test('Status indicators update correctly from API', async ({ page }) => {
  // Intercept the API call to mock the response
  await page.route('/api/system-status', async route => {
    const json = {
      wifi: { status: 'Connected (HomeNetwork)' },
      ups: { status: 'Connected (Eaton 3S)' }
    };
    await route.fulfill({ json });
  });

  // Navigate to the dashboard
  await page.goto('/');

  // Check that the indicators are updated correctly after polling
  const wifiLabel = page.locator('#lbl-wifi');
  const upsLabel = page.locator('#lbl-ups');

  await expect(wifiLabel).toHaveText('Wi-Fi: Connected (HomeNetwork)');
  await expect(upsLabel).toHaveText('UPS: Connected (Eaton 3S)');
  
  const wifiIndicator = page.locator('#ind-wifi');
  await expect(wifiIndicator).toHaveClass(/success/);
  
  const upsIndicator = page.locator('#ind-ups');
  await expect(upsIndicator).toHaveClass(/success/);
});
