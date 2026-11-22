import { test, expect, Page } from '@playwright/test';
import { serveIndex, serveUpdatePage } from './support/webui';

// US-147 — browser back/forward between the tabs of the Web UI.
// Each tab change adds a '#<data-target>' history entry; back/forward open the
// tab of the entry through the same click; on load the address picks the tab.
// Serves the real data/www sources; the APIs the tabs load are counted.

const BASE = 'http://esp32.local/';

const UPS = {
  'ups.status': 'OL',
  'battery.charge': '100',
  'battery.runtime.low': '120',
  'ups.load': '18',
};

type Counters = { upsVars: number; board: number };

async function mockApis(page: Page, upsVars: Record<string, unknown> = UPS): Promise<Counters> {
  const calls: Counters = { upsVars: 0, board: 0 };
  await page.route('**/api/config', route => route.fulfill({ json: {} }));
  await page.route('**/api/system-status', route => route.fulfill({
    json: { wifi: { status: 'Connected' }, ups: { status: 'OL' } },
  }));
  await page.route('**/api/ups-vars', route => {
    calls.upsVars++;
    return route.fulfill({ json: upsVars });
  });
  await page.route('**/api/board', route => {
    calls.board++;
    return route.fulfill({ json: {} });
  });
  return calls;
}

// The tab is open: button and section active, title as the button
async function expectTab(page: Page, target: string) {
  await expect(page.locator('.tab.active')).toHaveAttribute('data-target', target);
  await expect(page.locator('.tab-content.active')).toHaveAttribute('id', `content-${target}`);
  const label = (await page.locator(`.tab[data-target="${target}"]`).textContent())!.trim();
  await expect(page.locator('.page-title')).toHaveText(label);
}

const historyLength = (page: Page) => page.evaluate(() => history.length);

test.describe('Tab history (US-147)', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('clicks add entries, back and forward reopen the tabs', async ({ page }) => {
    await mockApis(page);
    await page.goto(BASE);
    await expectTab(page, 'ups');

    await page.click('.tab[data-target="nut"]');
    await expect(page).toHaveURL(`${BASE}#nut`);
    await page.click('.tab[data-target="board"]');
    await expect(page).toHaveURL(`${BASE}#board`);
    await expectTab(page, 'board');

    await page.goBack();
    await expect(page).toHaveURL(`${BASE}#nut`);
    await expectTab(page, 'nut');

    await page.goBack();
    await expect(page).toHaveURL(BASE);
    await expectTab(page, 'ups');

    await page.goForward();
    await expect(page).toHaveURL(`${BASE}#nut`);
    await expectTab(page, 'nut');
  });

  test('back and forward load the tab as a click does', async ({ page }) => {
    const calls = await mockApis(page);
    await page.goto(BASE);
    await expect.poll(() => calls.upsVars).toBeGreaterThan(0);

    await page.click('.tab[data-target="board"]');
    await expect.poll(() => calls.board).toBeGreaterThan(0);

    // Back on telemetry: the poll of /api/ups-vars starts again
    await page.goBack();
    await expectTab(page, 'ups');
    const upsBefore = calls.upsVars;
    await expect.poll(() => calls.upsVars).toBeGreaterThan(upsBefore);

    // Forward on Board Settings: /api/board is read again
    const boardBefore = calls.board;
    await page.goForward();
    await expectTab(page, 'board');
    await expect.poll(() => calls.board).toBeGreaterThan(boardBefore);
  });

  test('a click on the tab already open adds no entry', async ({ page }) => {
    await mockApis(page);
    await page.goto(BASE);
    await expectTab(page, 'ups');

    // UPS Telemetry on '/' with no '#'
    const start = await historyLength(page);
    await page.click('.tab[data-target="ups"]');
    expect(await historyLength(page)).toBe(start);
    await expect(page).toHaveURL(BASE);

    await page.click('.tab[data-target="nut"]');
    const afterNut = await historyLength(page);
    expect(afterNut).toBe(start + 1);
    await page.click('.tab[data-target="nut"]');
    expect(await historyLength(page)).toBe(afterNut);
  });

  test('the address opens its tab on load and after a reload', async ({ page }) => {
    const calls = await mockApis(page);
    await page.goto(`${BASE}#board`);
    await expectTab(page, 'board');
    await expect.poll(() => calls.board).toBeGreaterThan(0);

    await page.reload();
    await expectTab(page, 'board');
    await expect(page).toHaveURL(`${BASE}#board`);

    // Telemetry is not shown, so its poll does not run
    expect(calls.upsVars).toBe(0);

    const other = await page.context().newPage();
    await serveIndex(other);
    await mockApis(other);
    await other.goto(`${BASE}#logs`);
    await expectTab(other, 'logs');
    await other.close();
  });

  for (const hash of ['#pippo', '#ota']) {
    test(`${hash} opens UPS Telemetry with no errors`, async ({ page }) => {
      const errors: string[] = [];
      page.on('pageerror', e => errors.push(e.message));
      page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
      const calls = await mockApis(page);

      await page.goto(`${BASE}${hash}`);
      await expectTab(page, 'ups');
      await expect.poll(() => calls.upsVars).toBeGreaterThan(0);
      expect(errors).toEqual([]);
    });
  }

  test('back after a telemetry chip returns to the telemetry', async ({ page }) => {
    await mockApis(page, {
      ...UPS,
      _overrides: {
        'battery.runtime.low': { key: 'battery.runtime.low', value: '300', description: '', added: false },
      },
    });
    await page.route('**/api/nut/overrides', route => route.fulfill({
      json: {
        max: 16, max_value_length: 64, max_description_length: 64,
        overrides: [{ key: 'battery.runtime.low', value: '300', description: '', ups_value: '120' }],
      },
    }));
    await page.goto(BASE);

    await page.locator('#row-battery-runtime-low .tm-nut').click();
    await expect(page).toHaveURL(`${BASE}#nut`);
    await expectTab(page, 'nut');

    await page.goBack();
    await expect(page).toHaveURL(BASE);
    await expectTab(page, 'ups');
  });

  test('back from the OTA page returns to the tab it was opened from', async ({ page }) => {
    await serveUpdatePage(page);
    await mockApis(page);
    await page.goto(BASE);

    await page.click('.tab[data-target="nut"]');
    await expect(page).toHaveURL(`${BASE}#nut`);

    await page.click('.tab[data-target="ota"]');
    await expect(page).toHaveURL(`${BASE}update`);

    await page.goBack();
    await expect(page).toHaveURL(`${BASE}#nut`);
    await expectTab(page, 'nut');
  });
});
