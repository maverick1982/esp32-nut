import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-146 (#128) — ups.realpower estimated from a nominal power set as a custom variable of the
// NUT Server tab. Serves the real data/www sources and mocks GET/POST /api/nut/overrides and
// GET /api/ups-vars with one shared state, like the firmware: after the save, the next poll
// serves ups.realpower = load x ups.realpower.nominal (10.4 % of 980 W = 102 W) as a UPS value,
// and ups.realpower.nominal only as an added custom variable (_overrides).

interface Entry { key: string; value: string; description: string }

// The Smart-UPS 1500 of #120: ups.load 10.4, no nominal power
const UPS: Record<string, string> = {
  'ups.status': 'OL',
  'ups.mfr': 'American Power Conversion',
  'ups.model': 'Smart-UPS 1500',
  'battery.charge': '100',
  'battery.runtime': '3480',
  'ups.load': '10.4',
  'output.voltage': '240.4',
};

// base: other UPS values (states A and C); estimate: the _realpower_estimate the firmware
// sends from the nominal powers of the UPS (US-075), when no custom variable gives one
interface State { list: Entry[]; base?: Record<string, string>; estimate?: Record<string, unknown> }

function find(list: Entry[], key: string): Entry | undefined {
  return list.find(e => e.key.toLowerCase() === key);
}

// What the firmware serves: the estimate from the custom W (or VA x 0.6) and _overrides
function upsVars(state: State): Record<string, unknown> {
  const data: Record<string, unknown> = { ...UPS, ...(state.base ?? {}) };
  const load = parseFloat(String(data['ups.load']));
  const w = parseFloat(find(state.list, 'ups.realpower.nominal')?.value ?? '');
  const va = parseFloat(find(state.list, 'ups.power.nominal')?.value ?? '');
  if (w > 0) {
    data['ups.realpower'] = Math.round(load * w / 100);
    data._realpower_estimate = { source: 'custom', key: 'ups.realpower.nominal', nominal: w, unit: 'W', factor: 1 };
  } else if (va > 0) {
    data['ups.realpower'] = Math.round(load * va * 0.6 / 100);
    data._realpower_estimate = { source: 'custom', key: 'ups.power.nominal', nominal: va, unit: 'VA', factor: 0.6 };
  } else if (state.estimate) {
    data._realpower_estimate = state.estimate;
  }
  if (state.list.length > 0) {
    const ovr: Record<string, unknown> = {};
    for (const e of state.list) {
      const k = e.key.toLowerCase();
      const added = !(k in data);
      ovr[added ? e.key : k] = { key: e.key, value: e.value, description: e.description, added };
    }
    data._overrides = ovr;
  }
  return data;
}

async function mockFirmware(page: Page, state: State): Promise<void> {
  await page.route('**/api/config', route => route.fulfill({ json: {} }));
  await page.route('**/api/ups-vars', route => route.fulfill({ json: upsVars(state) }));
  await page.route('**/api/nut/overrides', async route => {
    if (route.request().method() === 'POST') {
      const body = route.request().postDataJSON() as { overrides: Entry[] };
      state.list = body.overrides.map(o => ({ key: o.key, value: o.value, description: o.description ?? '' }));
    }
    await route.fulfill({
      json: {
        max: 16, max_value_length: 64, max_description_length: 64,
        overrides: state.list.map(e => ({
          key: e.key, value: e.value, description: e.description,
          ups_value: UPS[e.key.toLowerCase()] ?? null,
        })),
      },
    });
  });
}

async function openTab(page: Page, target: 'ups' | 'nut') {
  await page.click(`.tab[data-target="${target}"]`);
  await expect(page.locator(`#content-${target}`)).toHaveClass(/active/);
}

const tr = (page: Page, key: string) => page.locator(`#row-${key.replace(/\./g, '-')}`);
const cvRow = (page: Page, key: string) => page.locator(`#cv-list .cv-row[data-key="${key}"]`);

test.describe('Real power estimate from a custom nominal power (US-146)', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('adding ups.realpower.nominal gives the Real Power card, removing it clears the card', async ({ page }) => {
    const state: State = { list: [] };
    await mockFirmware(page, state);
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(tr(page, 'ups.load').locator('.ups-val')).toHaveText('10.4');
    await expect(page.locator('#ups-realpower')).toHaveText('--');

    // NUT Server: the rating on the label, 980 W
    await openTab(page, 'nut');
    await page.fill('#cv-key', 'ups.realpower.nominal');
    await page.fill('#cv-value', '980');
    await page.fill('#cv-desc', 'SUA1500I label');
    await page.click('#cv-submit');
    await expect(cvRow(page, 'ups.realpower.nominal')).toBeVisible();

    // Telemetry at the next poll: the estimate is a UPS value, no NUT marker on the card
    await openTab(page, 'ups');
    await expect(page.locator('#ups-realpower')).toHaveText('102');
    await expect(page.locator('#card-mark-realpower')).toBeHidden();
    await expect(tr(page, 'ups.realpower').locator('.ups-val')).toHaveText('102');
    await expect(tr(page, 'ups.realpower').locator('.tm-nut')).toHaveCount(0);
    const nominal = tr(page, 'ups.realpower.nominal');
    await expect(nominal.locator('.ups-val')).toHaveText('980');
    await expect(nominal.locator('.tm-nut.add .k')).toHaveText('Added');

    // Removed in NUT Server: the card is back to -- and the row is gone
    await openTab(page, 'nut');
    page.once('dialog', dialog => dialog.accept());
    await cvRow(page, 'ups.realpower.nominal').locator('.cv-act.del').click();
    await expect(cvRow(page, 'ups.realpower.nominal')).toHaveCount(0);
    await openTab(page, 'ups');
    await expect(page.locator('#ups-realpower')).toHaveText('--');
    await expect(tr(page, 'ups.realpower')).toHaveCount(0);
  });

  test('an override of ups.realpower itself shows the NUT marker next to the estimate', async ({ page }) => {
    const state: State = {
      list: [
        { key: 'ups.realpower.nominal', value: '980', description: '' },
        { key: 'ups.realpower', value: '150', description: '' },
      ],
    };
    await mockFirmware(page, state);
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(page.locator('#ups-realpower')).toHaveText('102');
    await expect(page.locator('#card-mark-realpower')).toBeVisible();
    await expect(page.locator('#card-mark-realpower')).toHaveClass(/ovr/);
    await expect(page.locator('#card-mark-realpower .v')).toHaveText('150 W');
  });

  test('the Custom variables text names the two nominal powers', async ({ page }) => {
    await mockFirmware(page, { list: [] });
    await page.goto('http://esp32.local/');
    await openTab(page, 'nut');
    const lead = page.locator('#nut-overrides .cv-lead');
    await expect(lead).toContainText('ups.realpower.nominal');
    await expect(lead).toContainText('ups.power.nominal');
    await expect(lead).toContainText('estimate of ups.realpower');
  });
});

test.describe('Estimated mark on the Real Power card and row (US-146)', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  const card = (page: Page) => page.locator('#card-est-realpower');
  const formula = (page: Page) => page.locator('#card-est-src');

  test('A: a power the UPS measures has no Estimated mark', async ({ page }) => {
    await mockFirmware(page, { list: [], base: { 'ups.realpower': '321' } });
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(page.locator('#ups-realpower')).toHaveText('321');
    await expect(card(page)).toBeHidden();
    await expect(formula(page)).toBeHidden();
    await expect(tr(page, 'ups.realpower').locator('.tm-est')).toHaveCount(0);
    await expect(tr(page, 'ups.realpower')).not.toHaveClass(/tm-estd/);
  });

  test('B and E: custom W gives the mark, the formula and a link to NUT Server', async ({ page }) => {
    await mockFirmware(page, { list: [{ key: 'ups.realpower.nominal', value: '980', description: '' }] });
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(page.locator('#ups-realpower')).toHaveText('102');
    await expect(card(page)).toBeVisible();
    await expect(card(page)).toContainText('Estimated');
    await expect(formula(page)).toHaveText('10.4 %\u00d7980 W \u00b7 ups.realpower.nominal');

    // Tooltip on tap: what was computed, from where, and the limit of the watts
    await card(page).click();
    await expect(card(page)).toHaveClass(/open/);
    const tip = card(page).locator('.tip');
    await expect(tip).toBeVisible();
    await expect(tip).toContainText('10.4 % load \u00d7 980 W');
    await expect(tip.locator('.src.custom em')).toHaveText('ups.realpower.nominal');
    await expect(tip.locator('.warn')).toContainText('Right only if');

    // E: the ups.realpower row carries the same mark and the formula under the description
    const row = tr(page, 'ups.realpower');
    await expect(row).toHaveClass(/tm-estd/);
    await expect(row.locator('.tm-est.in-table')).toContainText('Estimated');
    await expect(row.locator('.tm-est-why')).toContainText('10.4 %');
    await expect(row.locator('.tm-est-why a')).toHaveText('ups.realpower.nominal');

    // The link opens NUT Server on the variable
    await formula(page).locator('a').click();
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await expect(cvRow(page, 'ups.realpower.nominal')).toHaveClass(/pulse/);
  });

  test('C: VA of the UPS gives the formula with x 0.6 and no link', async ({ page }) => {
    await mockFirmware(page, {
      list: [],
      base: { 'ups.load': '10', 'ups.power.nominal': '1500', 'ups.realpower': '90' },
      estimate: { source: 'ups', key: 'ups.power.nominal', nominal: 1500, unit: 'VA', factor: 0.6 },
    });
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(page.locator('#ups-realpower')).toHaveText('90');
    await expect(card(page)).toBeVisible();
    await expect(formula(page)).toHaveText('10 %\u00d71500 VA\u00d70.6 \u00b7 UPS ups.power.nominal');
    await expect(formula(page).locator('a')).toHaveCount(0);
    await expect(formula(page).locator('.ups')).toHaveText('UPS ups.power.nominal');
    const tip = card(page).locator('.tip');
    await expect(tip).toContainText('From ups.power.nominal of the UPS');
    await expect(tip).toContainText('Set ups.realpower.nominal in NUT Server');
    await expect(tip.locator('.warn')).toHaveCount(0);
  });

  test('D: the mark lives next to the NUT marker of an override of ups.realpower', async ({ page }) => {
    await mockFirmware(page, {
      list: [
        { key: 'ups.realpower.nominal', value: '980', description: '' },
        { key: 'ups.realpower', value: '150', description: '' },
      ],
    });
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(card(page)).toBeVisible();
    await expect(page.locator('#card-mark-realpower .v')).toHaveText('150 W');
    await expect(page.locator('#realpower-label')).not.toHaveClass(/no-mark/);
    const a = await card(page).boundingBox();
    const b = await page.locator('#card-mark-realpower').boundingBox();
    expect(a && b).toBeTruthy();
    const overlap = a!.x < b!.x + b!.width && b!.x < a!.x + a!.width && a!.y < b!.y + b!.height && b!.y < a!.y + a!.height;
    expect(overlap).toBe(false);
    // In the row: the mark of the estimate and the chip of the override
    await expect(tr(page, 'ups.realpower').locator('.tm-est')).toHaveCount(1);
    await expect(tr(page, 'ups.realpower').locator('.tm-nut.ovr')).toHaveCount(1);
  });

  test('the mark goes at the next poll when the variable is removed', async ({ page }) => {
    const state: State = { list: [{ key: 'ups.realpower.nominal', value: '980', description: '' }] };
    await mockFirmware(page, state);
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(card(page)).toBeVisible();
    await expect(tr(page, 'ups.realpower').locator('.tm-est')).toHaveCount(1);
    state.list = [];
    await expect(card(page)).toBeHidden({ timeout: 10000 });
    await expect(formula(page)).toBeHidden();
    await expect(page.locator('#ups-realpower')).toHaveText('--');
    await expect(tr(page, 'ups.realpower')).toHaveCount(0);
  });

  test('375 px: mark, formula and NUT marker without horizontal scroll', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await mockFirmware(page, {
      list: [
        { key: 'ups.realpower.nominal', value: '980', description: '' },
        { key: 'ups.realpower', value: '150', description: '' },
      ],
    });
    await page.goto('http://esp32.local/');
    await openTab(page, 'ups');
    await expect(card(page)).toBeVisible();
    await card(page).click();
    await expect(card(page).locator('.tip')).toBeVisible();
    const [scrollWidth, clientWidth] = await page.evaluate(() =>
      [document.documentElement.scrollWidth, document.documentElement.clientWidth]);
    expect(scrollWidth).toBeLessThanOrEqual(clientWidth);
  });
});
