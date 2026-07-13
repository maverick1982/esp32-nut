import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('System Logs View', () => {
  test.beforeEach(async ({ page }) => {
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

  test('should display mocked system logs in the terminal', async ({ page }) => {
    // Intercept the API call to /api/logs
    await page.route('**/api/logs', async (route) => {
      if (route.request().method() === 'GET') {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify([
            { time: 1000, level: 'WARN', msg: 'Mocked warning message' }
          ])
        });
      } else {
        await route.continue();
      }
    });

    // 1. Navigate to /
    await page.goto('http://esp32.local/');

    // 2. Click on the "System Logs" tab
    await page.click('[data-target="logs"]');

    // 3. Wait for the terminal to update
    const terminalOutput = page.locator('#terminal-output');
    
    // Wait for the specific message to appear in the terminal
    await expect(terminalOutput).toContainText('Mocked warning message');

    // Verify the correct class is applied to the log line (e.g., span containing the log)
    const logLine = page.locator('.terminal-line', { hasText: 'Mocked warning message' });
    await expect(logLine.locator('.terminal-level-warn')).toBeVisible();
  });
});
