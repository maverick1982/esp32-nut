import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-083 — the left menu is identical on every page. Before the fix the flex row
// squeezed the sidebar whenever the active page had wide content (UPS Commands,
// UPS Settings, and Wi-Fi/NUT/Logs at 1024 px): logo and title shrank and Log out
// moved. Serves the real data/www sources (support/webui.ts) and mocks the APIs
// with deliberately long descriptions and values.

const LONG_TEXT = 'Turn off the load after the configured delay and keep it off until the mains power ' +
  'comes back, then restart it once the battery has recharged above the minimum level';

const COMMANDS = {
  connected: true,
  commands: [
    { name: 'beeper.enable', description: 'Enable the UPS beeper', destructive: false },
    { name: 'test.battery.start.quick', description: LONG_TEXT, destructive: false },
    { name: 'shutdown.return', description: 'Turn off the load and return when power is back', destructive: true },
  ],
};

const VARIABLES = {
  connected: true,
  variables: [
    { name: 'battery.charge.low', description: LONG_TEXT, value: '20', max_length: 10, enum: [], driver_side: false, confirm: false },
    { name: 'input.sensitivity', description: 'Input power sensitivity', value: 'normal', max_length: 0, enum: ['high', 'normal', 'low'], driver_side: false, confirm: false },
    { name: 'outlet.1.desc', description: 'Outlet 1 description', value: 'PowerShare Outlet 1x', max_length: 20, enum: [], driver_side: true, confirm: false },
  ],
};

// UPS Telemetry data of an APC Smart-UPS: long parameter names and values
const TELEMETRY_VARS: Record<string, string> = {
  'battery.charge': '100', 'battery.charge.low': '10', 'battery.charge.warning': '50',
  'battery.mfr.date': '2024/03/15', 'battery.runtime': '2400', 'battery.runtime.low': '120',
  'battery.voltage': '27.3', 'battery.voltage.nominal': '24.0',
  'device.mfr': 'American Power Conversion', 'device.model': 'Smart-UPS 750', 'device.serial': 'AS1234567890',
  'input.transfer.high': '253', 'ups.firmware': '651.13.I', 'ups.mfr': 'American Power Conversion',
  'ups.status': 'OL CHRG', 'ups.test.result': 'Done and passed', 'ups.vendorid': '051d',
};

// Sidebar elements whose position and size must not change between pages. Every
// menu entry is measured, not just the first one.
const ELEMENTS = ['.sidebar', '.brand-icon', '.brand-name', 'nav.tabs button.tab', '#lbl-wifi', '#session-username', '#btn-logout', '#btn-logout-all'];

// Pages compared with UPS Telemetry (the reference); OTA opens /update, which has no sidebar
const PAGES = ['commands', 'settings', 'wifi', 'nut', 'weblogin', 'logs'];

type Box = { x: number; y: number; width: number; height: number };

// selector -> boxes of every match, in document order
async function geometry(page: Page): Promise<Record<string, Box[]>> {
  return page.evaluate((selectors) => {
    const out: Record<string, Box[]> = {};
    for (const sel of selectors) {
      out[sel] = Array.from(document.querySelectorAll(sel)).map(e => {
        const r = e.getBoundingClientRect();
        return { x: r.x, y: r.y, width: r.width, height: r.height };
      });
    }
    return out;
  }, ELEMENTS);
}

// The reference is taken once fonts (data URIs, font-display: swap) and the Wi-Fi /
// UPS status lines are final: each one changes the size of the sidebar text
async function openApp(page: Page) {
  await page.goto('http://esp32.local/');
  await expect(page.locator('#sidebar-session')).toBeVisible();
  await expect(page.locator('#lbl-wifi')).toHaveText('Wi-Fi: Connected');
  await expect(page.locator('#lbl-ups')).not.toHaveText('UPS: --');
  await page.evaluate(() => document.fonts.ready);
}

async function openTab(page: Page, target: string) {
  await page.locator(`button.tab[data-target="${target}"]`).click();
  await expect(page.locator(`#content-${target}`)).toHaveClass(/active/);
  // The two pages with wide content: wait until their rows are rendered
  if (target === 'commands') await expect(page.locator('#content-commands .cmd-run').first()).toBeVisible();
  if (target === 'settings') await expect(page.locator('#content-settings .set-apply').first()).toBeVisible();
}

function expectSameGeometry(actual: Record<string, Box[]>, expected: Record<string, Box[]>, where: string) {
  for (const sel of ELEMENTS) {
    expect(actual[sel].length, `${where}: number of ${sel}`).toBe(expected[sel].length);
    expected[sel].forEach((e, i) => {
      for (const k of ['x', 'y', 'width', 'height'] as const) {
        const a = actual[sel][i][k];
        expect(Math.abs(a - e[k]), `${where}: ${sel} #${i} ${k}: ${a} vs ${e[k]}`).toBeLessThanOrEqual(0.5);
      }
    });
  }
}

async function expectNoHorizontalOverflow(page: Page, where: string) {
  const fits = await page.locator('.content-area').evaluate(el => el.scrollWidth <= el.clientWidth);
  expect(fits, `${where}: horizontal overflow in .content-area`).toBe(true);
}

// Every matching control lies inside its .cmd-group: the group has overflow: hidden,
// so anything past its edges is clipped without any scrollbar
async function expectInsideGroup(page: Page, selector: string, where: string) {
  const controls = page.locator(selector);
  const n = await controls.count();
  expect(n, `${where}: ${selector} count`).toBeGreaterThan(0);
  for (let i = 0; i < n; i++) {
    const c = controls.nth(i);
    await expect(c).toBeVisible();
    const box = (await c.boundingBox())!;
    const group = (await c.locator('xpath=ancestor::*[contains(concat(" ", @class, " "), " cmd-group ")][1]').boundingBox())!;
    expect(box.x, `${where}: ${selector} #${i} left`).toBeGreaterThanOrEqual(group.x - 0.5);
    expect(box.x + box.width, `${where}: ${selector} #${i} right`).toBeLessThanOrEqual(group.x + group.width + 0.5);
  }
}

// Each row of the page: the text cells (name, description) are at least minWidth
// wide and none of them, nor anything inside them, overlaps the other cells.
// rowSel / text / others are CSS selectors relative to the page / row.
async function expectReadableColumns(page: Page, rowSel: string, text: string[], others: string[], where: string) {
  const problems = await page.evaluate(({ rowSel, text, others }) => {
    type R = { l: number; t: number; r: number; b: number };
    // The cell and everything inside it: an overflowing child counts as the cell
    const extent = (el: Element): R => {
      const out = { l: Infinity, t: Infinity, r: -Infinity, b: -Infinity };
      for (const e of [el, ...Array.from(el.querySelectorAll('*'))]) {
        const b = e.getBoundingClientRect();
        if (b.width === 0 || b.height === 0) continue;
        out.l = Math.min(out.l, b.left); out.t = Math.min(out.t, b.top);
        out.r = Math.max(out.r, b.right); out.b = Math.max(out.b, b.bottom);
      }
      return out;
    };
    const overlap = (a: R, b: R) => Math.min(a.r, b.r) - Math.max(a.l, b.l) > 0.5 && Math.min(a.b, b.b) - Math.max(a.t, b.t) > 0.5;
    const found: string[] = [];
    const rows = Array.from(document.querySelectorAll(rowSel));
    if (rows.length === 0) found.push('no rows');
    for (const row of rows) {
      const id = (row as HTMLElement).dataset.cmd || (row as HTMLElement).dataset.var;
      for (const t of text) {
        const cell = row.querySelector(t);
        if (!cell) continue;
        const w = cell.getBoundingClientRect().width;
        if (w < 180) found.push(`${id} ${t} width ${w.toFixed(1)}`);
        for (const o of others) {
          const other = row.querySelector(o);
          if (other && overlap(extent(cell), extent(other))) found.push(`${id} ${t} overlaps ${o}`);
        }
      }
    }
    return found;
  }, { rowSel, text, others });
  expect(problems, where).toEqual([]);
}

test.describe('Sidebar layout (US-083)', () => {
  test.beforeEach(async ({ page }) => {
    // Page, assets and API catch-all first: the mocks below take precedence
    await serveIndex(page);

    await page.route('**/api/config', route => route.fulfill({ json: {} }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'Connected', ssid: 'lab', ip: '192.168.1.50' }, ups: { status: 'Connected' } },
    }));
    await page.route('**/api/ups-vars', route => route.fulfill({
      json: { 'ups.status': 'OL', 'battery.charge': '100', 'ups.mfr': 'EATON', 'ups.beeper.status': 'enabled' },
    }));
    await page.route('**/api/auth/status', route => route.fulfill({
      json: { configured: true, authenticated: true, ap_mode: false, username: 'admin' },
    }));
    await page.route('**/api/ups/commands', route => route.fulfill({ json: COMMANDS }));
    await page.route('**/api/ups/variables', route => route.fulfill({ json: VARIABLES }));
    // Every command and every setting is rejected: the error messages stay on the page
    await page.route('**/api/ups/command', route => route.fulfill({
      status: 500, json: { error: 'Command rejected by the UPS' },
    }));
    await page.route('**/api/ups/variable', route => route.fulfill({
      status: 500, json: { error: 'Rejected by the UPS' },
    }));
  });

  for (const width of [1024, 1280]) {
    test(`desktop ${width} px: same sidebar on every page`, async ({ page }) => {
      await page.setViewportSize({ width, height: 900 });
      await openApp(page);

      const reference = await geometry(page);
      expect(Math.abs(reference['.sidebar'][0].width - 260), 'sidebar width on UPS Telemetry').toBeLessThanOrEqual(0.5);

      for (const target of PAGES) {
        await openTab(page, target);
        expectSameGeometry(await geometry(page), reference, target);
        await expectNoHorizontalOverflow(page, target);
      }
    });
  }

  test('desktop 1024 px: same sidebar with error messages on UPS Commands and UPS Settings', async ({ page }) => {
    await page.setViewportSize({ width: 1024, height: 900 });
    await openApp(page);
    const reference = await geometry(page);

    await openTab(page, 'commands');
    const cmdRow = page.locator('.cmd-row[data-cmd="beeper.enable"]');
    await cmdRow.locator('button.cmd-run').click();
    await expect(cmdRow.locator('.cmd-result')).toHaveText('Command rejected by the UPS');
    expectSameGeometry(await geometry(page), reference, 'commands with error');
    await expectNoHorizontalOverflow(page, 'commands with error');

    await openTab(page, 'settings');
    const setRow = page.locator('.cmd-row.set-row[data-var="battery.charge.low"]');
    await setRow.locator('input[type="text"]').fill('30');
    await setRow.locator('.set-apply').click();
    await expect(setRow.locator('.cmd-result.error')).toBeVisible();
    expectSameGeometry(await geometry(page), reference, 'settings with error');
    await expectNoHorizontalOverflow(page, 'settings with error');
    await expectInsideGroup(page, '#content-settings .set-apply', 'settings with error');
  });

  // 800 and 900 px: the sidebar keeps 260 px, so the UPS Settings editor moves to
  // its own line (max-width: 1000px); nothing may be clipped by .cmd-group
  for (const width of [800, 900, 1024, 1280]) {
    test(`desktop ${width} px: Run, Apply and the value fields are not clipped`, async ({ page }) => {
      await page.setViewportSize({ width, height: 900 });
      await openApp(page);

      await openTab(page, 'commands');
      await expectInsideGroup(page, '#content-commands .cmd-run', `commands ${width}`);
      await expectNoHorizontalOverflow(page, `commands ${width}`);

      await openTab(page, 'settings');
      await expectInsideGroup(page, '#content-settings .set-apply', `settings ${width}`);
      await expectInsideGroup(page, '#content-settings .set-field', `settings ${width}`);
      await expectNoHorizontalOverflow(page, `settings ${width}`);
    });
  }

  // With the sidebar at 260 px the row grids change layout at 1000/1240 px (UPS
  // Commands) and 1180/1400 px (UPS Settings): the name and description columns
  // never get narrow enough for their text to run over the next cells
  for (const width of [900, 1020, 1100, 1200, 1300, 1420]) {
    test(`desktop ${width} px: name and description columns stay readable`, async ({ page }) => {
      await page.setViewportSize({ width, height: 900 });
      await openApp(page);

      await openTab(page, 'commands');
      await expectReadableColumns(page, '#content-commands .cmd-row', ['.cmd-name', '.cmd-desc'],
        ['.cmd-action'], `commands ${width}`);

      await openTab(page, 'settings');
      await expectReadableColumns(page, '#content-settings .set-row', ['.set-id', '.cmd-desc'],
        ['.set-current', '.set-edit'], `settings ${width}`);
    });
  }

  // UPS Telemetry: parameter names and values do not wrap, so the table needs ~450 px.
  // .details-table-container has overflow: hidden: a wider table lost its Description
  // column without any scrollbar. Up to 1000 px the rows are stacked instead.
  for (const width of [800, 900, 1020, 1300]) {
    test(`desktop ${width} px: the telemetry table keeps its Description column`, async ({ page }) => {
      await page.route('**/api/ups-vars', route => route.fulfill({ json: TELEMETRY_VARS }));
      await page.setViewportSize({ width, height: 900 });
      await openApp(page);
      await expect(page.locator('#row-battery-voltage-nominal')).toBeVisible();

      const clipped = await page.evaluate(() => {
        const box = document.querySelector('.details-table-container')!.getBoundingClientRect();
        return Array.from(document.querySelectorAll('#ups-table-body td'))
          .filter(td => td.getBoundingClientRect().right > box.right + 0.5)
          .map(td => `${td.parentElement!.id} #${Array.from(td.parentElement!.children).indexOf(td)}`);
      });
      expect(clipped, `${width} px: cells past the table container`).toEqual([]);
      await expect(page.locator('#row-battery-voltage-nominal td').nth(2)).toHaveText('Nominal battery voltage (V)');
    });
  }

  test('mobile 375 px: the menu stays on top at full width', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await openApp(page);

    const reference = (await geometry(page))['.sidebar'][0];
    expect(Math.abs(reference.width - 375), 'sidebar width').toBeLessThanOrEqual(0.5);

    await openTab(page, 'commands');
    const sidebar = (await geometry(page))['.sidebar'][0];
    for (const k of ['x', 'y', 'width', 'height'] as const) {
      expect(Math.abs(sidebar[k] - reference[k]), `commands: .sidebar ${k}`).toBeLessThanOrEqual(0.5);
    }
  });
});
