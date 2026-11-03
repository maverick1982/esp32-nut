import { test, expect, Page, Request } from '@playwright/test';
import { serveIndex } from './support/webui';

// Hostname section of the Network page (US-113): always visible (no data-link), filled from
// /api/config network.hostname {name, custom, default} (WebApiJson::hostnameJson) and saved
// with POST /api/network/hostname (WebApiJson::checkHostnameBody). Same rule as
// NetHostname::isValid(): letters, digits and hyphens, no hyphen at the start or end, at most
// 32 characters; "" goes back to the default name.

const ERROR_TEXT =
  'Invalid hostname: use only letters, digits and hyphens, no hyphen at the start or end, at most 32 characters';
const DEFAULT_S3 = 'esp32-nut-56d784';
const DEFAULT_P4 = 'esp32-nut-e14b7c';

const WIFI_UP = {
  type: 'wifi', state: 'connected', ip: '192.168.1.126', gateway: '192.168.1.1',
  mac: '3C:84:27:56:D7:84', dhcp: true, ssid: 'HomeNetwork', rssi: -58,
};
const ETH_UP = {
  type: 'ethernet', state: 'connected', ip: '192.168.1.50', gateway: '192.168.1.1',
  mac: '30:ED:A0:E1:4B:7C', dhcp: true, link_speed: 100, full_duplex: true,
};

const s3Config = (custom = '') => ({
  wifi: { ssid: 'HomeNetwork', mode: 'STA' },
  network: { type: 'wifi', links: ['wifi'], hostname: { name: custom || DEFAULT_S3, custom, default: DEFAULT_S3 } },
});
const p4Config = {
  wifi: { ssid: '', mode: 'STA' },
  network: { type: 'ethernet', links: ['ethernet'], hostname: { name: DEFAULT_P4, custom: '', default: DEFAULT_P4 } },
};

type Answer = { status?: number; json: object };

// Opens the Network page; every POST /api/network/hostname is recorded and answered with `answer`
async function openNetwork(page: Page, config: object, network: object, answer: Answer) {
  const posts: object[] = [];
  await serveIndex(page);
  await page.route('**/api/config', route => route.fulfill({ json: config }));
  await page.route('**/api/system-status', route =>
    route.fulfill({ json: { wifi: { status: 'x' }, network, ups: { status: 'Eaton 3S' } } }));
  await page.route('**/api/network/hostname', (route, request: Request) => {
    posts.push(request.postDataJSON());
    return route.fulfill({ status: answer.status ?? 200, json: answer.json });
  });
  await page.goto('http://esp32.local/');
  await page.click('button[data-target="network"]');
  await expect(page.locator('h1.page-title')).toHaveText('Network');
  return posts;
}

const section = (page: Page) => page.locator('.net-section[data-section="hostname"]');
const RESTART = { json: { success: true, restart: true } };

test.describe('Hostname section of the Network page (US-113)', () => {
  test('Wi-Fi board: shows the default name and saves a new one, then restarts', async ({ page }) => {
    const posts = await openNetwork(page, s3Config(), WIFI_UP, RESTART);

    await expect(section(page)).toBeVisible();
    await expect(page.locator('#hostname-current')).toHaveText(DEFAULT_S3);
    await expect(page.locator('#hostname-origin')).toHaveText('Default');
    await expect(page.locator('#hostname')).toHaveValue('');
    await expect(page.locator('#hostname')).toHaveAttribute('placeholder', DEFAULT_S3);
    await expect(page.locator('#hostname-default')).toHaveText(DEFAULT_S3);

    await page.fill('#hostname', 'espnut-network');
    await page.click('#btn-hostname');

    await expect(page.locator('#btn-hostname')).toHaveText(/Saved/);
    await expect(page.locator('#btn-hostname')).toBeDisabled();
    await expect(page.locator('#hostname')).toBeDisabled();
    await expect(page.locator('#hostname-success')).toBeVisible();
    await expect(page.locator('#hostname-new')).toHaveText('espnut-network');
    await expect(page.locator('#lbl-wifi')).toHaveText('Wi-Fi: Restarting...');
    expect(posts).toEqual([{ hostname: 'espnut-network' }]);
  });

  test('Ethernet board: the section is there without the Wi-Fi section', async ({ page }) => {
    await openNetwork(page, p4Config, ETH_UP, RESTART);

    await expect(section(page)).toBeVisible();
    await expect(page.locator('.net-section[data-link="wifi"]')).toBeHidden();
    await expect(page.locator('.net-section[data-link="ethernet"]')).toBeVisible();
    await expect(page.locator('#hostname-current')).toHaveText(DEFAULT_P4);
    await expect(page.locator('#hostname')).toHaveAttribute('placeholder', DEFAULT_P4);
  });

  test('a saved name fills the field; saving it again changes nothing', async ({ page }) => {
    const posts = await openNetwork(page, s3Config('espnut-network'), WIFI_UP, RESTART);

    await expect(page.locator('#hostname')).toHaveValue('espnut-network');
    await expect(page.locator('#hostname-current')).toHaveText('espnut-network');
    await expect(page.locator('#hostname-origin')).toHaveText('Custom');
    await expect(page.locator('#hostname-origin')).toHaveClass(/custom/);

    await page.click('#btn-hostname');
    await expect(page.locator('#btn-hostname')).toHaveText('No changes');
    await expect(page.locator('#hostname-success')).toBeHidden();
    expect(posts).toEqual([]);
  });

  test('invalid names are refused with a clear message and nothing is sent', async ({ page }) => {
    const posts = await openNetwork(page, s3Config(), WIFI_UP, RESTART);

    for (const name of ['-bad', 'bad-', 'has space', 'a_b', 'a'.repeat(33)]) {
      // maxlength="32" stops typing at 32 characters: set the value as a paste-like script would
      await page.locator('#hostname').evaluate((el, v) => { (el as HTMLInputElement).value = v; }, name);
      await page.click('#btn-hostname');
      await expect(page.locator('#hostname-error'), name).toBeVisible();
      await expect(page.locator('#hostname-error-text'), name).toHaveText(ERROR_TEXT);
      await expect(page.locator('#hostname'), name).toHaveAttribute('aria-invalid', 'true');
    }
    expect(posts).toEqual([]);

    // Typing hides the message
    await page.fill('#hostname', 'espnut');
    await expect(page.locator('#hostname-error')).toBeHidden();
    await expect(page.locator('#hostname')).not.toHaveAttribute('aria-invalid', 'true');
  });

  test('an error from the API is shown and the form can be used again', async ({ page }) => {
    const posts = await openNetwork(page, s3Config(), WIFI_UP, { status: 400, json: { error: 'Refused by the board' } });

    await page.fill('#hostname', 'espnut-network');
    await page.click('#btn-hostname');

    await expect(page.locator('#hostname-error-text')).toHaveText('Refused by the board');
    await expect(page.locator('#btn-hostname')).toBeEnabled();
    await expect(page.locator('#hostname')).toBeEnabled();
    await expect(page.locator('#hostname-success')).toBeHidden();
    expect(posts).toEqual([{ hostname: 'espnut-network' }]);
  });

  test('an empty field goes back to the default name', async ({ page }) => {
    const posts = await openNetwork(page, s3Config('espnut-network'), WIFI_UP, RESTART);

    await page.fill('#hostname', '');
    await page.click('#btn-hostname');

    await expect(page.locator('#hostname-success')).toBeVisible();
    await expect(page.locator('#hostname-new')).toHaveText(DEFAULT_S3);
    expect(posts).toEqual([{ hostname: '' }]);
  });

  test('spaces around the name are removed before sending', async ({ page }) => {
    const posts = await openNetwork(page, s3Config(), WIFI_UP, RESTART);

    await page.fill('#hostname', '  espnut-network  ');
    await page.click('#btn-hostname');

    await expect(page.locator('#hostname-success')).toBeVisible();
    await expect(page.locator('#hostname')).toHaveValue('espnut-network');
    expect(posts).toEqual([{ hostname: 'espnut-network' }]);
  });
});
