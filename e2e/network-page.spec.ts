import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// Network page (US-112): one section per link of the board. The sections come from
// /api/config network.links (src/network/web_config_server.cpp, WebApiJson::networkLinksJson),
// the status from /api/system-status network (WebApiJson::networkStatusJson), refreshed with
// the status bar (fetchSystemStatus, every 3 s).

const WIFI_UP = {
  type: 'wifi', state: 'connected', ip: '192.168.1.126', gateway: '192.168.1.1',
  mac: '3C:84:27:C1:2A:F0', dhcp: true, ssid: 'HomeNetwork', rssi: -58,
};
const ETH_UP = {
  type: 'ethernet', state: 'connected', ip: '192.168.1.50', gateway: '192.168.1.1',
  mac: '30:ED:A0:E1:4B:7C', dhcp: true, link_speed: 100, full_duplex: true,
};

const statusOf = (network: object) => ({
  json: { wifi: { status: 'x' }, network, ups: { status: 'Eaton 3S' } },
});

async function openNetwork(page: Page, config: object, network: object) {
  await serveIndex(page);
  await page.route('**/api/config', route => route.fulfill({ json: config }));
  await page.route('**/api/system-status', route => route.fulfill(statusOf(network)));
  await page.goto('http://esp32.local/');
  await page.click('button[data-target="network"]');
  await expect(page.locator('h1.page-title')).toHaveText('Network');
}

const wifi = (page: Page) => page.locator('.net-section[data-link="wifi"]');
const eth = (page: Page) => page.locator('.net-section[data-link="ethernet"]');
const field = (section: ReturnType<typeof wifi>, name: string) => section.locator(`[data-net-field="${name}"]`);

test.describe('Network page (US-112)', () => {
  test('Wi-Fi board: status of the connected network above the settings', async ({ page }) => {
    await openNetwork(page, { wifi: { ssid: 'HomeNetwork', mode: 'STA' }, network: { type: 'wifi', links: ['wifi'] } }, WIFI_UP);

    await expect(wifi(page)).toBeVisible();
    await expect(eth(page)).toBeHidden();
    await expect(wifi(page).locator('[data-net-badge]')).toHaveText('Connected');
    await expect(field(wifi(page), 'ssid')).toHaveText('HomeNetwork');
    await expect(field(wifi(page), 'signal')).toContainText('-58 dBm');
    await expect(field(wifi(page), 'signal')).toContainText('Good');
    await expect(field(wifi(page), 'ip')).toHaveText('192.168.1.126');
    await expect(field(wifi(page), 'gateway')).toHaveText('192.168.1.1');
    await expect(field(wifi(page), 'mac')).toHaveText('3C:84:27:C1:2A:F0');
    await expect(field(wifi(page), 'dhcp')).toHaveText('On');
    await expect(wifi(page).locator('[data-net-notice]')).toBeHidden();
    await expect(page.locator('#wifi-form')).toBeVisible();
    await expect(page.locator('#ssid')).toHaveValue('HomeNetwork');
  });

  test('access point mode: the section says so and the form still saves', async ({ page }) => {
    let posted = false;
    await page.addInitScript(() => { window.alert = () => {}; });
    await openNetwork(page, { wifi: { ssid: '', mode: 'AP' }, network: { type: 'wifi', links: ['wifi'] } },
      { type: 'wifi', state: 'ap', ip: '192.168.4.1', mac: '3C:84:27:C1:2A:F0', ssid: 'ESP32-NUT-2AF0' });
    await page.route('**/api/wifi/connect', route => { posted = true; return route.fulfill({ json: { success: true } }); });

    await expect(wifi(page).locator('[data-net-badge]')).toHaveText('Access point mode');
    const notice = wifi(page).locator('[data-net-notice]');
    await expect(notice).toBeVisible();
    await expect(notice).toContainText('not on your home network');
    await expect(notice).toContainText('192.168.4.1');
    await expect(field(wifi(page), 'gateway')).toHaveText('—');

    await page.fill('#ssid', 'HomeNetwork');
    await page.fill('#password', 'SecretPassword');
    await page.click('#btn-connect');
    await expect(page.locator('#btn-connect')).toHaveText(/Saved & Connecting/i);
    expect(posted).toBeTruthy();
  });

  test('Ethernet board: Ethernet section only, with the link speed and no settings', async ({ page }) => {
    await openNetwork(page, { network: { type: 'ethernet', links: ['ethernet'] } }, ETH_UP);

    await expect(eth(page)).toBeVisible();
    await expect(wifi(page)).toBeHidden();
    await expect(page.locator('#wifi-form')).toBeHidden();
    await expect(eth(page).locator('[data-net-badge]')).toHaveText('Connected');
    await expect(field(eth(page), 'speed')).toHaveText('100 Mbps full duplex');
    await expect(field(eth(page), 'cable')).toHaveText('Plugged in');
    await expect(field(eth(page), 'ip')).toHaveText('192.168.1.50');
    await expect(field(eth(page), 'gateway')).toHaveText('192.168.1.1');
    await expect(field(eth(page), 'mac')).toHaveText('30:ED:A0:E1:4B:7C');
    await expect(field(eth(page), 'dhcp')).toHaveText('On');
  });

  test('Ethernet link down reported by the API', async ({ page }) => {
    await openNetwork(page, { network: { type: 'ethernet', links: ['ethernet'] } },
      { type: 'ethernet', state: 'disconnected', mac: '30:ED:A0:E1:4B:7C', dhcp: false });

    await expect(eth(page).locator('[data-net-badge]')).toHaveText('Disconnected');
    await expect(field(eth(page), 'cable')).toHaveText('Unplugged');
    await expect(eth(page).locator('[data-net-notice]')).toContainText('No Ethernet link');
  });

  test('board not answering: last values greyed, then back to connected', async ({ page }) => {
    let reachable = true;
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({ json: { network: { type: 'ethernet', links: ['ethernet'] } } }));
    await page.route('**/api/system-status', route => reachable
      ? route.fulfill(statusOf(ETH_UP))
      : route.abort('connectionrefused'));
    await page.clock.install();
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="network"]');
    await expect(field(eth(page), 'ip')).toHaveText('192.168.1.50');
    // while the board answers the badge is enough: no "updated N s ago" counter
    await expect(eth(page).locator('[data-net-live]')).toBeHidden();

    reachable = false;
    await page.clock.runFor(4000);
    await expect(eth(page).locator('[data-net-badge]')).toHaveText('Disconnected');
    await expect(field(eth(page), 'ip')).toHaveText('192.168.1.50');
    await expect(field(eth(page), 'ip')).toHaveClass('na');
    await expect(eth(page).locator('[data-net-live]')).toBeVisible();
    await expect(eth(page).locator('[data-net-live]')).toHaveText(/^No answer for \d+ s$/);
    await expect(page.locator('#lbl-wifi')).toHaveText('Ethernet: Offline');

    reachable = true;
    await page.clock.runFor(4000);
    await expect(eth(page).locator('[data-net-badge]')).toHaveText('Connected');
    await expect(field(eth(page), 'ip')).toHaveClass('');
    await expect(eth(page).locator('[data-net-live]')).toBeHidden();
  });

  test('the status refreshes with the status bar', async ({ page }) => {
    let ip = '192.168.1.50';
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({ json: { network: { type: 'ethernet', links: ['ethernet'] } } }));
    await page.route('**/api/system-status', route => route.fulfill(statusOf({ ...ETH_UP, ip })));
    await page.clock.install();
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="network"]');
    await expect(field(eth(page), 'ip')).toHaveText('192.168.1.50');

    ip = '192.168.1.51';
    await page.clock.runFor(4000);
    await expect(field(eth(page), 'ip')).toHaveText('192.168.1.51');
  });

  test('older /api/config without links: the section follows network.type', async ({ page }) => {
    await openNetwork(page, { network: { type: 'ethernet' } }, ETH_UP);
    await expect(eth(page)).toBeVisible();
    await expect(wifi(page)).toBeHidden();
  });

  test('repeated spaces in an SSID are shown as they are', async ({ page }) => {
    const ssid = 'My  Home   Net';
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({ json: { network: { type: 'wifi', links: ['wifi'] } } }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: ssid }, network: { type: 'wifi', state: 'connecting', ssid }, ups: { status: 'Eaton 3S' } },
    }));
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="network"]');

    // innerText follows the CSS white-space rule: without pre-wrap the spaces collapse to one
    const shown = (sel: string) => page.locator(sel).first().evaluate(el => (el as HTMLElement).innerText);
    await expect.poll(() => shown('.net-section[data-link="wifi"] [data-net-field="ssid"]')).toBe(ssid);
    await expect.poll(() => shown('.net-section[data-link="wifi"] [data-net-notice-text]')).toContain(`Connecting to ${ssid}…`);
    await expect.poll(() => shown('#lbl-wifi')).toBe(`Wi-Fi: ${ssid}`);
  });

  test('an SSID is shown as text, not markup', async ({ page }) => {
    await openNetwork(page, { network: { type: 'wifi', links: ['wifi'] } }, { ...WIFI_UP, ssid: '<b>x</b>' });
    await expect(field(wifi(page), 'ssid')).toHaveText('<b>x</b>');
    await expect(field(wifi(page), 'ssid').locator('b')).toHaveCount(0);
  });
});
