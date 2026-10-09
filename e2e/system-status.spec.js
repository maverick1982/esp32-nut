import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

// Status indicators in the header (fetchSystemStatus in bundle.js). The states are
// the ones /api/system-status really sends (src/network/web_config_server.cpp):
// Wi-Fi = SSID, "AP Mode Active" or "Connecting"; UPS = model or "Disconnected",
// with ups.stale when the attached UPS stops answering.
const CASES = [
  {
    name: 'connected: SSID and UPS model are green',
    status: { wifi: { status: 'HomeNetwork' }, ups: { status: 'Eaton 3S' } },
    wifi: { label: 'Wi-Fi: HomeNetwork', level: 'success' },
    ups: { label: 'UPS: Eaton 3S', level: 'success' },
  },
  {
    name: 'access point mode is info, missing UPS is danger',
    status: { wifi: { status: 'AP Mode Active' }, ups: { status: 'Disconnected' } },
    wifi: { label: 'Wi-Fi: AP Mode Active', level: 'info' },
    ups: { label: 'UPS: Disconnected', level: 'danger' },
  },
  {
    name: 'Wi-Fi connecting and stale UPS data are warnings',
    status: { wifi: { status: 'Connecting' }, ups: { status: 'Eaton 3S', stale: true } },
    wifi: { label: 'Wi-Fi: Connecting', level: 'warning' },
    ups: { label: 'UPS: Eaton 3S (stale)', level: 'warning' },
  },
];

async function expectIndicator(page, id, expected) {
  await expect(page.locator(`#lbl-${id}`)).toHaveText(expected.label);
  await expect(page.locator(`#ind-${id}`)).toHaveClass(`status-indicator ${expected.level}`);
}

test.describe('Status indicators', () => {
  for (const c of CASES) {
    test(c.name, async ({ page }) => {
      await serveIndex(page);
      await page.route('**/api/system-status', route => route.fulfill({ json: c.status }));
      await page.goto('http://esp32.local/');

      await expectIndicator(page, 'wifi', c.wifi);
      await expectIndicator(page, 'ups', c.ups);
    });
  }

  test('device not answering shows both indicators offline', async ({ page }) => {
    await serveIndex(page);
    await page.route('**/api/system-status', route => route.abort('connectionrefused'));
    await page.goto('http://esp32.local/');

    await expectIndicator(page, 'wifi', { label: 'Wi-Fi: Offline', level: 'danger' });
    await expectIndicator(page, 'ups', { label: 'UPS: Offline', level: 'danger' });
  });
});
