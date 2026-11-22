import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-144 (#143) — custom variables of the NUT Server tab on the UPS Telemetry page.
// Serves the real data/www sources and mocks GET /api/ups-vars, whose _overrides says per
// row what NUT clients see ({key, value, description, added}), and GET /api/nut/overrides
// for the NUT Server tab the chips open. The UPS values never change.

type Override = { key: string; value: string; description: string; added: boolean };
type UpsVars = Record<string, unknown> & { _overrides?: Record<string, Override> };

const UPS: Record<string, string> = {
  'ups.status': 'OL',
  'ups.mfr': 'APC',
  'battery.charge': '100',
  'battery.charge.low': '10',
  'battery.runtime': '2340',
  'battery.runtime.low': '120',
  'ups.load': '18',
};

const RUNTIME_LOW: Override = { key: 'battery.runtime.low', value: '300', description: 'Shut the NAS down earlier', added: false };
const BATTERY_DATE: Override = { key: 'battery.date', value: '2025/03/01', description: 'New battery', added: true };

// The live payload: tests change it between two polls
async function mockUpsVars(page: Page, initial: UpsVars): Promise<{ set: (next: UpsVars) => void }> {
  let current = initial;
  await page.route('**/api/ups-vars', route => route.fulfill({ json: current }));
  return { set: next => { current = next; } };
}

// GET /api/nut/overrides answers the list the chips point to (ups_value null = added)
async function mockNutOverrides(page: Page, entries: { key: string; value: string; ups_value: string | null }[]) {
  await page.route('**/api/nut/overrides', route => route.fulfill({
    json: {
      max: 16, max_value_length: 64, max_description_length: 64,
      overrides: entries.map(e => ({ key: e.key, value: e.value, description: '', ups_value: e.ups_value })),
    },
  }));
}

async function openTelemetry(page: Page) {
  await page.goto('http://esp32.local/');
  await page.click('.tab[data-target="ups"]');
  await expect(page.locator('#row-ups-status .ups-val')).toHaveText('OL');
}

const tr = (page: Page, key: string) => page.locator(`#row-${key.replace(/\./g, '-')}`);

test.describe('UPS Telemetry custom variables (US-144)', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
    await page.route('**/api/config', route => route.fulfill({ json: {} }));
  });

  test('overridden row keeps the UPS value, added row sits among the others', async ({ page }) => {
    await mockUpsVars(page, { ...UPS, _overrides: { 'battery.runtime.low': RUNTIME_LOW, 'battery.date': BATTERY_DATE } });
    await openTelemetry(page);

    const low = tr(page, 'battery.runtime.low');
    await expect(low).toHaveClass(/tm-ovr/);
    await expect(low.locator('.ups-val')).toHaveText('120');
    await expect(low.locator('.tm-nut.ovr .k')).toHaveText('NUT clients');
    await expect(low.locator('.tm-nut.ovr .v')).toHaveText('300');
    await expect(low.locator('td').nth(2)).toContainText('Remaining battery runtime when UPS switches to LB (seconds)');
    await expect(low.locator('.tm-why')).toHaveText('Shut the NAS down earlier');

    const date = tr(page, 'battery.date');
    await expect(date).toHaveClass(/tm-add/);
    await expect(date.locator('.ups-val')).toHaveText('2025/03/01');
    await expect(date.locator('.tm-nut.add .k')).toHaveText('Added');
    await expect(date.locator('.tm-why')).toHaveText('New battery');

    // Alphabetical order with the UPS rows
    const keys = await page.locator('#ups-table-body tr td:first-child').allTextContents();
    expect(keys.indexOf('battery.date')).toBe(keys.indexOf('battery.charge.low') + 1);
    expect(keys.indexOf('battery.runtime')).toBe(keys.indexOf('battery.date') + 1);

    await expect(page.locator('#tm-summary')).toBeVisible();
    await expect(page.locator('#tm-summary-text')).toHaveText('1 overridden · 1 added for NUT clients');
  });

  test('without custom variables the page is as before', async ({ page }) => {
    await mockUpsVars(page, { ...UPS });
    await openTelemetry(page);
    await expect(tr(page, 'battery.runtime.low').locator('.ups-val')).toHaveText('120');
    await expect(page.locator('#ups-table-body .tm-nut')).toHaveCount(0);
    await expect(page.locator('#ups-table-body tr.tm-ovr, #ups-table-body tr.tm-add')).toHaveCount(0);
    await expect(page.locator('#tm-summary')).toBeHidden();
    await expect(page.locator('.tm-card-mark:visible')).toHaveCount(0);
  });

  test('cards keep the UPS values and show a marker', async ({ page }) => {
    await mockUpsVars(page, {
      ...UPS,
      _overrides: {
        'ups.realpower': { key: 'ups.realpower', value: '95', description: '', added: true },
        'battery.charge': { key: 'battery.charge', value: '80', description: '', added: false },
      },
    });
    await openTelemetry(page);

    // Real Power: not reported by the UPS, the card stays -- and the marker says 95 W
    await expect(page.locator('#ups-realpower')).toHaveText('--');
    await expect(page.locator('#card-mark-realpower')).toBeVisible();
    await expect(page.locator('#card-mark-realpower')).toHaveClass(/add/);
    await expect(page.locator('#card-mark-realpower .v')).toHaveText('95 W');
    // Battery Charge: the real 100 %, the marker 80 %
    await expect(page.locator('#ups-charge')).toHaveText('100');
    await expect(page.locator('#card-mark-charge')).toHaveClass(/ovr/);
    await expect(page.locator('#card-mark-charge .v')).toHaveText('80 %');
    await expect(page.locator('#card-mark-load')).toBeHidden();
  });

  test('chips, card marker and summary open NUT Server', async ({ page }) => {
    await mockUpsVars(page, { ...UPS, _overrides: { 'battery.runtime.low': RUNTIME_LOW, 'battery.date': BATTERY_DATE } });
    await mockNutOverrides(page, [
      { key: 'battery.runtime.low', value: '300', ups_value: '120' },
      { key: 'battery.date', value: '2025/03/01', ups_value: null },
    ]);
    await openTelemetry(page);

    // Overridden: amber pulse
    await tr(page, 'battery.runtime.low').locator('.tm-nut').click();
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await expect(page.locator('#cv-list .cv-row[data-key="battery.runtime.low"]')).toHaveClass(/(^|\s)pulse(\s|$)/);

    // Added: cyan pulse
    await page.click('.tab[data-target="ups"]');
    await tr(page, 'battery.date').locator('.tm-nut').click();
    await expect(page.locator('#cv-list .cv-row[data-key="battery.date"]')).toHaveClass(/pulse-add/);

    // Summary: the tab opens, no row pulses
    await page.click('.tab[data-target="ups"]');
    await page.click('#tm-summary');
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await expect(page.locator('#cv-list .cv-row').first()).toBeVisible();
    await expect(page.locator('#cv-list .cv-row.pulse, #cv-list .cv-row.pulse-add')).toHaveCount(0);
  });

  test('card marker opens NUT Server on its variable', async ({ page }) => {
    await mockUpsVars(page, { ...UPS, _overrides: { 'ups.realpower': { key: 'ups.realpower', value: '95', description: '', added: true } } });
    await mockNutOverrides(page, [{ key: 'ups.realpower', value: '95', ups_value: null }]);
    await openTelemetry(page);
    await page.click('#card-mark-realpower');
    await expect(page.locator('#cv-list .cv-row[data-key="ups.realpower"]')).toHaveClass(/pulse-add/);
  });

  test('device.* alias row points to the ups.* variable', async ({ page }) => {
    const model: Override = { key: 'ups.model', value: 'Studio', description: '', added: false };
    await mockUpsVars(page, {
      ...UPS, 'ups.model': 'Back-UPS BX950MI', 'device.model': 'Back-UPS BX950MI',
      _overrides: { 'ups.model': model, 'device.model': model },
    });
    await mockNutOverrides(page, [{ key: 'ups.model', value: 'Studio', ups_value: 'Back-UPS BX950MI' }]);
    await openTelemetry(page);

    await expect(tr(page, 'device.model').locator('.ups-val')).toHaveText('Back-UPS BX950MI');
    await expect(tr(page, 'device.model').locator('.tm-nut .v')).toHaveText('Studio');
    // One custom variable, even if it marks two rows
    await expect(page.locator('#tm-summary-text')).toHaveText('1 overridden for NUT clients');
    await tr(page, 'device.model').locator('.tm-nut').click();
    await expect(page.locator('#cv-list .cv-row[data-key="ups.model"]')).toHaveClass(/(^|\s)pulse(\s|$)/);
  });

  test('a change saved in NUT Server shows up at the next poll', async ({ page }) => {
    const api = await mockUpsVars(page, { ...UPS, _overrides: { 'battery.runtime.low': RUNTIME_LOW, 'battery.date': BATTERY_DATE } });
    await openTelemetry(page);
    await expect(tr(page, 'battery.runtime.low').locator('.tm-nut .v')).toHaveText('300');

    api.set({ ...UPS, _overrides: { 'battery.runtime.low': { ...RUNTIME_LOW, value: '600' }, 'battery.date': BATTERY_DATE } });
    await expect(tr(page, 'battery.runtime.low').locator('.tm-nut .v')).toHaveText('600', { timeout: 6000 });

    api.set({ ...UPS });
    await expect(tr(page, 'battery.date')).toHaveCount(0, { timeout: 6000 });
    await expect(tr(page, 'battery.runtime.low').locator('.tm-nut')).toHaveCount(0);
    await expect(tr(page, 'battery.runtime.low')).not.toHaveClass(/tm-ovr/);
    await expect(page.locator('#tm-summary')).toBeHidden();
  });

  test('UPS disconnected: no custom rows, summary or markers', async ({ page }) => {
    await mockUpsVars(page, {
      _disconnected: true, 'ups.status': 'Disconnected',
      _overrides: { 'battery.date': BATTERY_DATE, 'ups.realpower': { key: 'ups.realpower', value: '95', description: '', added: true } },
    });
    await page.goto('http://esp32.local/');
    await page.click('.tab[data-target="ups"]');
    await expect(page.locator('#ups-status')).toHaveText('Disconnected');
    await expect(page.locator('#ups-table-body tr')).toHaveCount(0);
    await expect(page.locator('#tm-summary')).toBeHidden();
    await expect(page.locator('#card-mark-realpower')).toBeHidden();
  });

  test('custom text is shown literally, never as HTML', async ({ page }) => {
    await mockUpsVars(page, {
      ...UPS,
      _overrides: { 'ups.note': { key: 'ups.note', value: '<b>x</b>', description: '<i>y</i>', added: true } },
    });
    await openTelemetry(page);
    await expect(tr(page, 'ups.note').locator('.ups-val')).toHaveText('<b>x</b>');
    await expect(tr(page, 'ups.note').locator('.tm-why')).toHaveText('<i>y</i>');
    await expect(tr(page, 'ups.note').locator('b, i')).toHaveCount(0);
  });

  test('375 px: chips and markers without horizontal scroll', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await mockUpsVars(page, {
      ...UPS,
      _overrides: {
        'battery.runtime.low': RUNTIME_LOW,
        'battery.date': BATTERY_DATE,
        'ups.realpower': { key: 'ups.realpower', value: '95', description: '', added: true },
      },
    });
    await openTelemetry(page);
    await expect(tr(page, 'battery.runtime.low').locator('.tm-nut')).toBeVisible();
    const [scrollWidth, clientWidth] = await page.evaluate(() =>
      [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
    expect(scrollWidth).toBeLessThanOrEqual(clientWidth);
  });
});
