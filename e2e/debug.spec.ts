import { test, expect } from '@playwright/test';
import * as path from 'path';

test.describe('Wi-Fi Configuration UI Debug', () => {
  test.beforeEach(async ({ page }) => {
    page.on('console', msg => console.log('BROWSER LOG:', msg.text()));
    page.on('pageerror', err => console.log('BROWSER ERROR:', err.message));
    
    await page.route('http://esp32.local/', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/index.html') });
    });
    await page.route('**/*shared.css*', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/shared.css') });
    });
    await page.route('**/*app.js*', async route => {
      await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/app.js') });
    });
  });

  test('renders the main interface correctly', async ({ page }) => {
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="wifi"]');
    await expect(page.locator('h1')).toHaveText('Wi-Fi Config');
  });
});
