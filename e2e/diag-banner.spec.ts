import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-121: degraded mode banner (renderDiagnostics in bundle.js). When the restarts ran out
// because the UPS never enumerated again, /api/system-status sends
// diagnostics.ups_not_enumerated with diagnostics.degraded, and the banner asks to replug
// the USB cable instead of the generic degraded mode text.

const NOT_ENUMERATED = 'did not come back after the automatic restart';
const REPLUG = 'Unplug and replug the UPS USB cable.';
const GENERIC = 'Degraded mode:';

async function openWithDiagnostics(page: Page, diagnostics: Record<string, unknown>): Promise<void> {
  await serveIndex(page);
  await page.route('**/api/system-status', route => route.fulfill({
    json: {
      wifi: { status: 'HomeNetwork' },
      ups: { status: 'Disconnected' },
      diagnostics: { reset_reason: 'SW', coredump_partition: true, ...diagnostics },
    },
  }));
  await page.goto('http://esp32.local/');
}

test.describe('Degraded mode banner', () => {
  test('UPS never enumerated after the restarts: asks to replug the USB cable', async ({ page }) => {
    await openWithDiagnostics(page, {
      degraded: true,
      ups_not_enumerated: true,
      consecutive_restarts: 4,
      last_restart_cause: 'UPS not enumerated after the automatic restart',
    });

    const banner = page.locator('#diag-banner');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText(NOT_ENUMERATED);
    await expect(banner).toContainText('restarted 4 times in a row and power-cycled its USB port');
    await expect(banner).toContainText(REPLUG);
    await expect(banner).not.toContainText(GENERIC);
    await expect(banner).toContainText('Last automatic restart: UPS not enumerated after the automatic restart (4 in a row)');
  });

  test('degraded for another reason keeps the generic text', async ({ page }) => {
    await openWithDiagnostics(page, {
      degraded: true,
      consecutive_restarts: 4,
      last_restart_cause: 'control transfer stuck in the USB stack',
    });

    const banner = page.locator('#diag-banner');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText(GENERIC);
    await expect(banner).toContainText('Replug the UPS USB cable or reboot the board.');
    await expect(banner).not.toContainText(NOT_ENUMERATED);
  });

  test('not degraded: neither degraded line', async ({ page }) => {
    // ups_not_enumerated alone never comes from the board, and must not show anything
    await openWithDiagnostics(page, {
      ups_not_enumerated: true,
      consecutive_restarts: 1,
      last_restart_cause: 'UPS not enumerated after the automatic restart',
    });

    const banner = page.locator('#diag-banner');
    await expect(banner).toBeVisible();
    await expect(banner).toContainText('Last automatic restart: UPS not enumerated after the automatic restart');
    await expect(banner).not.toContainText(NOT_ENUMERATED);
    await expect(banner).not.toContainText(GENERIC);
  });
});
