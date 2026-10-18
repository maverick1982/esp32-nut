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

  // US-065: the UPS tab is active on load, so /api/ups-vars starts first. The status
  // poll must not wait for it. The clock is installed and never advanced, so no
  // setInterval runs: the indicators can only come from the poll made on load, while
  // /api/ups-vars is still pending (with a shared in-flight flag they never appear).
  test('indicators come from the first poll while /api/ups-vars is still pending', async ({ page }) => {
    await page.clock.install();
    await serveIndex(page);
    let releaseUpsVars;
    const upsVarsHeld = new Promise(resolve => { releaseUpsVars = resolve; });
    let upsVarsRequested = false;
    await page.route('**/api/ups-vars', async route => {
      upsVarsRequested = true;
      await upsVarsHeld;
      await route.fulfill({ json: {} }).catch(() => {});
    });
    await page.route('**/api/system-status', route =>
      route.fulfill({ json: CASES[0].status }));
    await page.goto('http://esp32.local/');

    await expectIndicator(page, 'wifi', CASES[0].wifi);
    await expectIndicator(page, 'ups', CASES[0].ups);
    expect(upsVarsRequested).toBe(true);
    releaseUpsVars();
  });

  test('the status poll sends one request at a time', async ({ page }) => {
    await page.clock.install();
    await serveIndex(page);
    let requests = 0;
    let releaseStatus;
    const statusHeld = new Promise(resolve => { releaseStatus = resolve; });
    await page.route('**/api/system-status', async route => {
      requests++;
      if (requests === 1) await statusHeld;
      await route.fulfill({ json: CASES[0].status }).catch(() => {});
    });
    await page.goto('http://esp32.local/');
    await expect.poll(() => requests).toBe(1);

    // Two intervals pass while the first answer is pending: no second request
    await page.clock.runFor(7000);
    expect(requests).toBe(1);

    releaseStatus();
    await expect(page.locator('#lbl-wifi')).toHaveText('Wi-Fi: HomeNetwork');
    await page.clock.runFor(3000);
    await expect.poll(() => requests).toBe(2);
  });
});
