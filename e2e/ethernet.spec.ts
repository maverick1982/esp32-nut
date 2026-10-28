import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

// Boards without Wi-Fi (ESP32-P4 Ethernet, US-092): /api/config and /api/system-status carry
// network.type = "ethernet" (src/network/web_config_server.cpp). The UI hides the
// Wi-Fi tab and labels the link status "Ethernet"; the Wi-Fi build is unchanged.
test.describe('Boards without Wi-Fi', () => {
  test('hides the Wi-Fi tab and labels the link status Ethernet', async ({ page }) => {
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({
      json: { wifi: { ssid: '', mode: 'STA' }, network: { type: 'ethernet' }, nut: { username: '', ups_name: 'ups' } },
    }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: '192.168.1.50 (100 Mbps)' }, network: { type: 'ethernet' }, ups: { status: 'Back-UPS 1500' } },
    }));
    await page.goto('http://esp32.local/');

    await expect(page.locator('#lbl-wifi')).toHaveText('Ethernet: 192.168.1.50 (100 Mbps)');
    await expect(page.locator('#ind-wifi')).toHaveClass('status-indicator success');
    await expect(page.locator('button[data-target="wifi"]')).toBeHidden();
    await expect(page.locator('button[data-target="nut"]')).toBeVisible();
  });

  test('link down is danger, and offline keeps the Ethernet label', async ({ page }) => {
    await serveIndex(page);
    let reachable = true;
    await page.route('**/api/system-status', route => reachable
      ? route.fulfill({ json: { wifi: { status: 'Disconnected' }, network: { type: 'ethernet' }, ups: { status: 'Disconnected' } } })
      : route.abort('connectionrefused'));
    await page.clock.install();
    await page.goto('http://esp32.local/');

    await expect(page.locator('#lbl-wifi')).toHaveText('Ethernet: Disconnected');
    await expect(page.locator('#ind-wifi')).toHaveClass('status-indicator danger');

    reachable = false;
    await page.clock.runFor(10000);
    await expect(page.locator('#lbl-wifi')).toHaveText('Ethernet: Offline');
  });

  test('boards with Wi-Fi keep the Wi-Fi tab', async ({ page }) => {
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({
      json: { wifi: { ssid: 'HomeNetwork', mode: 'STA' }, network: { type: 'wifi' }, nut: { username: '', ups_name: 'ups' } },
    }));
    await page.goto('http://esp32.local/');
    await expect(page.locator('button[data-target="wifi"]')).toBeVisible();
  });
});
