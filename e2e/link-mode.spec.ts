import { test, expect, Page, Request } from '@playwright/test';
import { serveIndex } from './support/webui';

// ADR 0015: "Link to use" of a board with its own Ethernet port and Wi-Fi (a P4 with a Wi-Fi
// coprocessor). Shown only when GET /api/network/link answers 200 (WebApiJson::linkModeJson),
// saved with POST /api/network/link {mode} (WebApiJson::checkLinkModeBody); a change restarts.

const ETH_UP = {
  type: 'ethernet', state: 'connected', ip: '192.168.1.60', gateway: '192.168.1.1',
  mac: '30:ED:A0:11:22:33', dhcp: true, link_speed: 100, full_duplex: true,
};
const NANO_CONFIG = {
  wifi: { ssid: '', mode: 'STA' },
  network: { type: 'wifi', links: ['wifi', 'ethernet'], hostname: { name: 'esp32-nut-112233', custom: '', default: 'esp32-nut-112233' } },
};

type Answer = { status?: number; json: object };

async function openNetwork(page: Page, get: Answer, answer?: Answer) {
  const posts: object[] = [];
  await serveIndex(page);
  await page.route('**/api/config', route => route.fulfill({ json: NANO_CONFIG }));
  await page.route('**/api/system-status', route =>
    route.fulfill({ json: { wifi: { status: 'x' }, network: ETH_UP, ups: { status: 'Disconnected' } } }));
  await page.route('**/api/network/link', (route, request: Request) => {
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

const section = (page: Page) => page.locator('#sec-linkmode');
const radio = (page: Page, mode: string) => page.locator(`input[name="linkmode"][value="${mode}"]`);

test.describe('Link to use, board with its own Ethernet port and Wi-Fi (ADR 0015)', () => {
  test('hidden on boards without the route (404)', async ({ page }) => {
    await openNetwork(page, { status: 404, json: {} });
    await expect(page.locator('section[data-link="ethernet"]')).toBeVisible();
    await expect(section(page)).toBeHidden();
  });

  test('shows the saved mode, Save only after a change, saving restarts', async ({ page }) => {
    const posts = await openNetwork(page, { json: { mode: 'ethernet', active: 'ethernet' } },
      { json: { success: true, restart: true } });
    await expect(section(page)).toBeVisible();
    await expect(radio(page, 'ethernet')).toBeChecked();
    const save = page.locator('#btn-linkmode');
    await expect(save).toBeDisabled();

    await radio(page, 'auto').check({ force: true });
    await expect(save).toBeEnabled();
    await radio(page, 'ethernet').check({ force: true });
    await expect(save).toBeDisabled();

    await radio(page, 'wifi').check({ force: true });
    await save.click();
    await expect(page.locator('#linkmode-success')).toBeVisible();
    await expect(page.locator('#linkmode-countdown')).toHaveText(/^1[0-5]$/);
    expect(posts).toEqual([{ mode: 'wifi' }]);
    await expect(radio(page, 'auto')).toBeDisabled();
  });

  test('an error of the POST is shown and the choice can be saved again', async ({ page }) => {
    await openNetwork(page, { json: { mode: 'auto', active: 'wifi' } },
      { status: 400, json: { error: 'Invalid network mode' } });
    await radio(page, 'ethernet').check({ force: true });
    await page.locator('#btn-linkmode').click();
    await expect(page.locator('#linkmode-error')).toBeVisible();
    await expect(page.locator('#linkmode-error-text')).toHaveText('Invalid network mode');
    await expect(page.locator('#linkmode-success')).toBeHidden();
    await expect(page.locator('#btn-linkmode')).toBeEnabled();
  });

  test('an unknown mode in the GET keeps the section hidden', async ({ page }) => {
    await openNetwork(page, { json: { mode: 'lte', active: 'ethernet' } });
    await expect(section(page)).toBeHidden();
  });
});

// The Wi-Fi coprocessor line of the board page
async function openBoard(page: Page, board: object) {
  await serveIndex(page);
  await page.route('**/api/config', route => route.fulfill({ json: NANO_CONFIG }));
  await page.route('**/api/system-status', route =>
    route.fulfill({ json: { wifi: { status: 'x' }, network: ETH_UP, ups: { status: 'Disconnected' } } }));
  await page.route('**/api/board', route => route.fulfill({ json: board }));
  await page.goto('http://esp32.local/');
  await page.locator('button.tab[data-target="board"]').click();
  await expect(page.locator('#board-name')).toHaveText('Waveshare ESP32-P4-NANO');
}

const NANO_BOARD = {
  id: 'waveshare_p4_nano',
  label: 'Waveshare ESP32-P4-NANO',
  chip: { model: 'ESP32-P4', revision: 'v3.2', image: 'esp32-p4-nano' },
  usb: { ports: [{ id: 'hs', label: 'USB High-Speed (Type-A)' }], port: 'hs' },
  auto_shutdown: { on: false, available: true },
};

test.describe('Board page, Wi-Fi coprocessor (ADR 0015)', () => {
  test('answered: target, firmware, host and the update notice', async ({ page }) => {
    await openBoard(page, { ...NANO_BOARD, wifi_coprocessor: { answered: true, target: 'esp32c6', firmware: 'unknown', host: '2.12.13', update_available: true } });
    await expect(page.locator('#board-cp')).toBeVisible();
    await expect(page.locator('#board-cp-text')).toHaveText('ESP32-C6, firmware unknown (host 2.12.13), newer firmware available');
  });

  test('not answering: the reason and that the board runs without Wi-Fi', async ({ page }) => {
    await openBoard(page, { ...NANO_BOARD, wifi_coprocessor: { answered: false, error: 'no answer within 10 s' } });
    await expect(page.locator('#board-cp-text')).toHaveText('Not answering: no answer within 10 s. The board runs without Wi-Fi.');
  });

  test('boards without a coprocessor: no line', async ({ page }) => {
    await openBoard(page, NANO_BOARD);
    await expect(page.locator('#board-chip')).toBeVisible();
    await expect(page.locator('#board-cp')).toBeHidden();
  });
});
