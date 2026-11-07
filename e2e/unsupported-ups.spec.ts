import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-132: a Q* UPS (issues #91, #28) is recognised but not supported yet. /api/ups-vars
// answers as for a missing UPS plus "_unsupported" (WebApiJson::generateUpsVars), and the
// UPS page shows #unsupported-ups-banner instead of leaving the user with "Disconnected".

const QX_TEXT = 'Q* UPS (USB transport: hunnox)';
const QX_VARS = { _disconnected: true, 'ups.status': 'Disconnected', _unsupported: QX_TEXT };
const NO_UPS_VARS = { _disconnected: true, 'ups.status': 'Disconnected' };

test.describe('Q* UPS not supported yet', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({ json: {} }));
  });

  test('banner shown for a recognised Q* UPS, generic banner hidden', async ({ page }) => {
    await page.route('**/api/ups-vars', route => route.fulfill({ json: QX_VARS }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'HomeNetwork' }, ups: { status: 'Not supported yet', unsupported: QX_TEXT } },
    }));
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="ups"]');

    const banner = page.locator('#unsupported-ups-banner');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText('Q* UPS recognized (USB transport: hunnox)');
    await expect(banner).toContainText('not supported yet');
    await expect(page.locator('#generic-ups-banner')).toBeHidden();
    await expect(page.locator('#lbl-ups')).toHaveText('UPS: Not supported yet');
    await expect(page.locator('#ind-ups')).toHaveClass('status-indicator warning');
  });

  test('no banner and red indicator when no UPS is attached', async ({ page }) => {
    await page.route('**/api/ups-vars', route => route.fulfill({ json: NO_UPS_VARS }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'HomeNetwork' }, ups: { status: 'Disconnected' } },
    }));
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="ups"]');

    await expect(page.locator('#lbl-ups')).toHaveText('UPS: Disconnected');
    await expect(page.locator('#ind-ups')).toHaveClass('status-indicator danger');
    await expect(page.locator('#unsupported-ups-banner')).toBeHidden();
  });

  test('banner goes away when the Q* UPS is unplugged between two polls', async ({ page }) => {
    let vars: object = QX_VARS;
    await page.route('**/api/ups-vars', route => route.fulfill({ json: vars }));
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="ups"]');

    const banner = page.locator('#unsupported-ups-banner');
    await expect(banner).toBeVisible();

    vars = NO_UPS_VARS;
    await expect(banner).toBeHidden({ timeout: 10000 });
  });
});
