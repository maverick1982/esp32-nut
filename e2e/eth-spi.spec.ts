import { test, expect, Page, Request } from '@playwright/test';
import { serveIndex } from './support/webui';

// W5500 section of the Network page (US-126, docs/mockups/US-126): shown only when
// GET /api/network/eth answers 200 with available:true (WebApiJson::ethSpiJson), saved with
// POST /api/network/eth {enabled, pins} (WebApiJson::checkEthSpiBody). Empty fields are sent
// as null; the 400 messages are the ones of checkEthSpiSettings().

const WIFI_UP = {
  type: 'wifi', state: 'connected', ip: '192.168.1.126', gateway: '192.168.1.1',
  mac: '3C:84:27:56:D7:84', dhcp: true, ssid: 'HomeNetwork', rssi: -58,
};
const S3_CONFIG = {
  wifi: { ssid: 'HomeNetwork', mode: 'STA' },
  network: { type: 'wifi', links: ['wifi'], hostname: { name: 'esp32-nut-56d784', custom: '', default: 'esp32-nut-56d784' } },
};

const NO_PINS = { sck: null, mosi: null, miso: null, cs: null, irq: null, rst: null };
const EXAMPLE = { sck: 12, mosi: 11, miso: 13, cs: 10, irq: 9, rst: 14 };
const STRAPPING = [0, 3, 45, 46];

const ethGet = (enabled: boolean, pins: object, state: string, reason?: string) => ({
  available: true, enabled, pins, strapping: STRAPPING, status: reason ? { state, reason } : { state },
});
const OFF = ethGet(false, NO_PINS, 'off');

type Answer = { status?: number; json: object };

// Opens the Network page; GET /api/network/eth answers `get`, every POST is recorded and
// answered with `answer`
async function openNetwork(page: Page, get: Answer, answer?: Answer) {
  const posts: object[] = [];
  await serveIndex(page);
  await page.route('**/api/config', route => route.fulfill({ json: S3_CONFIG }));
  await page.route('**/api/system-status', route =>
    route.fulfill({ json: { wifi: { status: 'x' }, network: WIFI_UP, ups: { status: 'Eaton 3S' } } }));
  await page.route('**/api/network/eth', (route, request: Request) => {
    if (request.method() === 'GET') return route.fulfill({ status: get.status ?? 200, json: get.json });
    posts.push(request.postDataJSON());
    const a = answer ?? { status: 501, json: { error: 'unexpected POST' } };
    return route.fulfill({ status: a.status ?? 200, json: a.json });
  });
  await page.goto('http://esp32.local/');
  await page.click('button[data-target="network"]');
  await expect(page.locator('h1.page-title')).toHaveText('Network');
  return posts;
}

const section = (page: Page) => page.locator('#sec-w5500');
const pin = (page: Page, f: string) => page.locator(`#w5500-${f}`);

async function fillPins(page: Page, pins: Record<string, number | null>) {
  for (const [f, v] of Object.entries(pins)) await pin(page, f).fill(v === null ? '' : String(v));
}

test.describe('W5500 section of the Network page (US-126)', () => {
  test('enabling with the example pins sends them and starts the countdown', async ({ page }) => {
    const posts = await openNetwork(page, { json: OFF }, { json: { success: true, restart: true, warnings: [] } });

    await expect(section(page)).toBeVisible();
    await expect(page.locator('#w5500-badge')).toHaveText('Off');
    await expect(page.locator('#w5500-enabled')).not.toBeChecked();
    for (const [f, v] of Object.entries(EXAMPLE)) {
      await expect(pin(page, f)).toHaveValue('');
      await expect(pin(page, f)).toHaveAttribute('placeholder', String(v));
    }

    await page.locator('#sec-w5500 .switch').click();
    await expect(page.locator('#w5500-state')).toHaveText('On');
    await fillPins(page, EXAMPLE);
    await expect(page.locator('#w5500-strapping')).toBeHidden();
    await page.click('#btn-w5500');

    await expect(page.locator('#w5500-success')).toBeVisible();
    await expect(page.locator('#btn-w5500')).toHaveText(/Saved/);
    await expect(page.locator('#btn-w5500')).toBeDisabled();
    await expect(pin(page, 'cs')).toBeDisabled();
    await expect(page.locator('#w5500-countdown')).toHaveText(/^(15|14|13)$/);
    await expect(page.locator('#lbl-wifi')).toHaveText('Wi-Fi: Restarting...');
    expect(posts).toEqual([{ enabled: true, pins: EXAMPLE }]);
  });

  test('a forbidden GPIO shows the error of the board, and typing hides it', async ({ page }) => {
    const error = 'GPIO 19 cannot be used for SCK: USB port of the UPS';
    const posts = await openNetwork(page, { json: OFF }, { status: 400, json: { error } });

    await page.locator('#sec-w5500 .switch').click();
    await fillPins(page, { sck: 19, mosi: 11, miso: 13, cs: 10 });
    await page.click('#btn-w5500');

    await expect(page.locator('#w5500-error')).toBeVisible();
    await expect(page.locator('#w5500-error-text')).toHaveText(error);
    await expect(page.locator('.pin-cell[data-wire="sck"]')).toHaveClass(/invalid/);
    await expect(pin(page, 'sck')).toHaveAttribute('aria-invalid', 'true');
    await expect(page.locator('#btn-w5500')).toBeEnabled();
    await expect(page.locator('#w5500-success')).toBeHidden();
    expect(posts).toEqual([{ enabled: true, pins: { sck: 19, mosi: 11, miso: 13, cs: 10, irq: null, rst: null } }]);

    await pin(page, 'sck').fill('12');
    await expect(page.locator('#w5500-error')).toBeHidden();
    await expect(page.locator('.pin-cell[data-wire="sck"]')).not.toHaveClass(/invalid/);
  });

  test('RST on a strapping pin shows a warning and saving stays possible', async ({ page }) => {
    const warning = 'GPIO 3 is a strapping pin: the board may not boot if the module pulls it at power-on';
    const posts = await openNetwork(page, { json: OFF }, { json: { success: true, restart: true, warnings: [warning] } });

    await page.locator('#sec-w5500 .switch').click();
    await fillPins(page, { ...EXAMPLE, rst: 3 });

    await expect(page.locator('#w5500-strapping')).toBeVisible();
    await expect(page.locator('#w5500-strapping-text')).toContainText('GPIO 3 (RST) is a strapping pin');
    await expect(page.locator('.pin-cell[data-wire="rst"]')).toHaveClass(/strap/);
    await expect(page.locator('#btn-w5500')).toBeEnabled();

    await page.click('#btn-w5500');
    await expect(page.locator('#w5500-success')).toBeVisible();
    expect(posts).toEqual([{ enabled: true, pins: { ...EXAMPLE, rst: 3 } }]);
  });

  test('empty INT and RST are sent as null', async ({ page }) => {
    const posts = await openNetwork(page, { json: OFF }, { json: { success: true, restart: true, warnings: [] } });

    await page.locator('#sec-w5500 .switch').click();
    await fillPins(page, { sck: 12, mosi: 11, miso: 13, cs: 10 });
    await page.click('#btn-w5500');

    await expect(page.locator('#w5500-success')).toBeVisible();
    expect(posts).toEqual([{ enabled: true, pins: { sck: 12, mosi: 11, miso: 13, cs: 10, irq: null, rst: null } }]);
  });

  test('fallback to Wi-Fi: the section explains why', async ({ page }) => {
    await openNetwork(page, { json: ethGet(true, { ...EXAMPLE, cs: 8 }, 'fallback', 'no_chip') });

    await expect(section(page)).toBeVisible();
    await expect(page.locator('#w5500-badge')).toHaveText('Fallback');
    await expect(page.locator('#w5500-status')).toBeVisible();
    await expect(page.locator('#w5500-status-text')).toContainText('The W5500 did not answer at startup.');
    await expect(page.locator('#w5500-status-text')).toContainText('The board is using Wi-Fi.');
    await expect(page.locator('#w5500-enabled')).toBeChecked();
    await expect(pin(page, 'cs')).toHaveValue('8');
    await expect(pin(page, 'rst')).toHaveValue('14');
  });

  test('no W5500 on the board (404, P4): the section is not shown', async ({ page }) => {
    await openNetwork(page, { status: 404, json: { error: 'Not found' } });

    await expect(page.locator('.net-section[data-section="hostname"]')).toBeVisible();
    await expect(section(page)).toBeHidden();
  });

  test('saving the same values answers restart:false without a countdown', async ({ page }) => {
    const posts = await openNetwork(page, { json: ethGet(true, EXAMPLE, 'active') },
      { json: { success: true, restart: false, warnings: [] } });

    await expect(page.locator('#w5500-badge')).toHaveText('Active');
    await expect(page.locator('#w5500-status')).toBeHidden();
    await page.click('#btn-w5500');

    await expect(page.locator('#btn-w5500')).toHaveText('No changes');
    await expect(page.locator('#btn-w5500')).toBeEnabled();
    await expect(page.locator('#w5500-success')).toBeHidden();
    await expect(page.locator('#lbl-wifi')).not.toHaveText('Wi-Fi: Restarting...');
    expect(posts).toEqual([{ enabled: true, pins: EXAMPLE }]);
  });
});
