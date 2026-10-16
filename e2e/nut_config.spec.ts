import { test, expect, Page } from '@playwright/test';
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
      expect(payload.ups_name).toBe('eaton');
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

  // US-082: the UPS name is never empty and only printable ASCII without spaces
  async function openNutTab(page: Page) {
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="nut"]');
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
  }

  test('UPS Name placeholder is ups', async ({ page }) => {
    await openNutTab(page);
    await expect(page.locator('#nut-upsname')).toHaveAttribute('placeholder', 'ups');
  });

  test('invalid UPS names are blocked by the form', async ({ page }) => {
    let posts = 0;
    page.on('request', req => {
      if (req.method() === 'POST' && req.url().includes('/api/nut/config')) posts++;
    });
    await page.route('**/api/nut/config', route => route.fulfill({ json: { success: true } }));
    await openNutTab(page);
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');

    const input = page.locator('#nut-upsname');
    const button = page.locator('#btn-save-nut');
    for (const name of ['my ups', 'città', 'a"b', 'a\\b', 'a#b', 'a@b']) {
      await input.fill(name);
      await button.click();
      // The browser stops the submit: pattern mismatch, a message, and no "Saving..."
      expect(await input.evaluate((el: HTMLInputElement) => el.validity.patternMismatch), name).toBe(true);
      expect(await input.evaluate((el: HTMLInputElement) => el.validationMessage), name).not.toBe('');
      await expect(button).not.toHaveText(/Saving/);
    }
    expect(posts).toBe(0);
  });

  test('the pattern accepts the printable ASCII names', async ({ page }) => {
    await openNutTab(page);
    const input = page.locator('#nut-upsname');
    for (const name of ['ups', 'Back-UPS_1', '[ups]', '!~', 'a$b?c']) {
      await input.fill(name);
      expect(await input.evaluate((el: HTMLInputElement) => el.validity.valid), name).toBe(true);
    }
  });

  test('the default name ups is saved', async ({ page }) => {
    let body: { ups_name?: string } | null = null;
    await page.route('**/api/nut/config', async route => {
      if (route.request().method() === 'POST') body = route.request().postDataJSON();
      await route.fulfill({ json: { success: true } });
    });
    await openNutTab(page);
    await page.fill('#nut-upsname', 'ups');
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');
    await page.click('#btn-save-nut');

    await expect(page.locator('#btn-save-nut')).toHaveText(/Saved!/i);
    expect(body).not.toBeNull();
    expect(body!.ups_name).toBe('ups');
  });
});
