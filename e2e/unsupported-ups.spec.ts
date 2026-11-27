import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-132: a Q* UPS (issues #91, #28) whose USB transport is not ported yet (sgs here;
// krauler, fabula, hunnox and cypress, US-141, are ported) is recognised but not supported. /api/ups-vars
// answers as for a missing UPS plus "_unsupported" (WebApiJson::generateUpsVars), and the
// UPS page shows #unsupported-ups-banner instead of leaving the user with "Disconnected".

const QX_TEXT = 'Q* UPS (USB transport: sgs)';
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
    await expect(banner).toContainText('Q* UPS recognized (USB transport: sgs)');
    await expect(banner).toContainText('not supported yet');
    await expect(banner).toContainText('only the krauler, fabula, hunnox and cypress USB transports work');
    await expect(banner).toContainText('USB Diagnostics export');
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

  // ADR 0014: a USB serial bridge (XR21V1410 of the APC SRV2KI-E) is recognized and opened, but
  // no protocol runs on it yet: its own text, not the Q* transport list
  test('banner for a USB serial device without a protocol', async ({ page }) => {
    const SERIAL_TEXT = 'USB serial: XR21V141x (04E2:1410)';
    await page.route('**/api/ups-vars', route => route.fulfill({
      json: { _disconnected: true, 'ups.status': 'Disconnected', _unsupported: SERIAL_TEXT },
    }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'HomeNetwork' }, ups: { status: 'Not supported yet', unsupported: SERIAL_TEXT } },
    }));
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="ups"]');

    const banner = page.locator('#unsupported-ups-banner');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText('USB serial: XR21V141x (04E2:1410) recognized');
    await expect(banner).toContainText('no UPS protocol runs on USB serial devices yet');
    await expect(banner).toContainText('USB Diagnostics export');
    await expect(banner).not.toContainText('krauler');
    await expect(page.locator('#generic-ups-banner')).toBeHidden();
    await expect(page.locator('#lbl-ups')).toHaveText('UPS: Not supported yet');
    await expect(page.locator('#ind-ups')).toHaveClass('status-indicator warning');
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
