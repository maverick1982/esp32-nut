import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

test.describe('Config Auto-populate', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

    test('should pre-populate SSID and NUT username from /api/config', async ({ page }) => {
        // Mock the /api/config response
        await page.route('**/api/config', async route => {
            const json = {
                wifi: {
                    ssid: "MockNetwork",
                    mode: "STA"
                },
                nut: {
                    username: "mockadmin"
                }
            };
            await route.fulfill({ json });
        });

        // Navigate to the app
        await page.goto('http://esp32.local/');
    await page.click('button[data-target=\"wifi\"]');

        // Verify SSID field is populated
        const ssidInput = page.locator('#ssid');
        await expect(ssidInput).toHaveValue('MockNetwork');

        // Verify NUT username is populated in its tab
        const nutTab = page.locator('button.tab[data-target="nut"]');
        await nutTab.click();

        const nutUsernameInput = page.locator('#nut-username');
        await expect(nutUsernameInput).toHaveValue('mockadmin');
    });

    test('should NOT pre-populate SSID if mode is AP', async ({ page }) => {
        // Mock the /api/config response for AP mode
        await page.route('**/api/config', async route => {
            const json = {
                wifi: {
                    ssid: "MockNetwork",
                    mode: "AP"
                },
                nut: {
                    username: ""
                }
            };
            await route.fulfill({ json });
        });

        // Navigate to the app
        await page.goto('http://esp32.local/');
    await page.click('button[data-target=\"wifi\"]');

        // Verify SSID field remains empty
        const ssidInput = page.locator('#ssid');
        await expect(ssidInput).toHaveValue('');
    });
});
