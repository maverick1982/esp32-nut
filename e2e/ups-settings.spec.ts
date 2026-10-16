import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-068 — "UPS Settings" page (writable UPS variables). Serves the real
// data/www sources (support/webui.ts) and mocks every API the page calls with
// page.route. The page reads GET /api/ups/variables when the tab opens and
// after each Apply or Reset (no polling); Apply posts /api/ups/variable,
// "Reset to default" posts /api/ups/variable/reset.

interface Variable {
  name: string;
  description: string;
  value: string;
  max_length: number;
  enum: string[];
  driver_side: boolean;
  confirm: boolean;
}

interface Post {
  method: string;
  contentType: string | undefined;
  body: unknown;
}

interface Mock {
  apMode: boolean;
  connected: boolean;
  variables: Variable[];
  variablesStatus: number;
  setStatus: number;
  setBody: Record<string, unknown> | null;
  resetStatus: number;
  sets: Post[];
  resets: Post[];
  getHits: number;
}

// Server messages of WebApiJson.cpp (runUpsVariable)
const SET_ERROR_BODIES: Record<number, string> = {
  400: 'Value too long (max 10)',
  500: 'Rejected by the UPS',
  503: 'UPS not connected',
};

// Driver-side defaults restored by "Reset to default"
const DRIVER_DEFAULTS: Record<string, string> = {
  'ups.delay.shutdown': '20',
  'ups.delay.start': '30',
  'outlet.1.desc': 'PowerShare Outlet 1',
};

const SET_CONFIRM_WARNING = 'This setting can switch the UPS outlets, bypass or ECO mode: the devices connected to the UPS may lose power.';

// Writable variables of an Eaton 3S (mge-hid) as returned by the firmware
function eaton3sCatalog(): Variable[] {
  return [
    { name: 'battery.charge.low', description: 'Remaining battery level when UPS switches to LB (percent)', value: '20', max_length: 10, enum: [], driver_side: false, confirm: false },
    { name: 'battery.runtime.low', description: 'Remaining battery runtime when UPS switches to LB (seconds)', value: '180', max_length: 10, enum: [], driver_side: false, confirm: false },
    { name: 'input.sensitivity', description: 'Input power sensitivity', value: 'normal', max_length: 0, enum: ['high', 'normal', 'low'], driver_side: false, confirm: false },
    { name: 'ups.shutdown', description: 'Enable or disable UPS shutdown ability (poweroff)', value: 'enabled', max_length: 0, enum: ['enabled', 'disabled'], driver_side: false, confirm: true },
    { name: 'ups.delay.shutdown', description: 'Interval to wait after shutdown with delay command (seconds)', value: '60', max_length: 10, enum: [], driver_side: true, confirm: false },
    { name: 'ups.delay.start', description: 'Interval to wait before (re)starting the load (seconds)', value: '30', max_length: 10, enum: [], driver_side: true, confirm: false },
    { name: 'outlet.1.desc', description: 'Outlet 1 description', value: 'PowerShare Outlet 1', max_length: 20, enum: [], driver_side: true, confirm: false },
  ];
}

function defaultMock(): Mock {
  return {
    apMode: false,
    connected: true,
    variables: eaton3sCatalog(),
    variablesStatus: 200,
    setStatus: 200,
    setBody: null,
    resetStatus: 200,
    sets: [],
    resets: [],
    getHits: 0,
  };
}

function record(route: { request(): { method(): string; headers(): Record<string, string>; postDataJSON(): unknown } }): Post {
  const req = route.request();
  return { method: req.method(), contentType: req.headers()['content-type'], body: req.postDataJSON() };
}

async function openSettingsTab(page: Page) {
  await page.goto('http://esp32.local/');
  await page.locator('button.tab[data-target="settings"]').click();
  await expect(page.locator('#content-settings')).toHaveClass(/active/);
}

function settingRow(page: Page, name: string) {
  return page.locator(`.cmd-row.set-row[data-var="${name}"]`);
}

let mock: Mock;

test.describe('UPS Settings page (US-068)', () => {
  test.beforeEach(async ({ page }) => {
    mock = defaultMock();

    // Page, assets and API catch-all first: the mocks below take precedence
    await serveIndex(page);

    await page.route('**/api/config', route => route.fulfill({ json: {} }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'Connected', ssid: 'lab', ip: '192.168.1.50' }, ups: { status: 'Connected' } },
    }));
    await page.route('**/api/ups-vars', route => route.fulfill({
      json: { 'ups.status': 'OL', 'battery.charge': '100', 'ups.mfr': 'EATON' },
    }));
    await page.route('**/api/auth/status', route => route.fulfill({
      json: mock.apMode
        ? { configured: true, authenticated: false, ap_mode: true }
        : { configured: true, authenticated: true, ap_mode: false, username: 'admin' },
    }));
    await page.route('**/api/ups/variables', route => {
      mock.getHits++;
      if (mock.variablesStatus !== 200) {
        return route.fulfill({ status: mock.variablesStatus, json: { error: 'Error' } });
      }
      return route.fulfill({
        json: { connected: mock.connected, variables: mock.connected ? mock.variables : [] },
      });
    });
    await page.route('**/api/ups/variable', route => {
      const post = record(route);
      mock.sets.push(post);
      const status = mock.setStatus;
      if (status === 200) {
        const { name, value } = post.body as { name: string; value: string };
        const v = mock.variables.find(x => x.name === name);
        if (v) v.value = value;
        return route.fulfill({ status, json: { success: true, value } });
      }
      if (mock.setBody === null) return route.fulfill({ status, body: '' });
      return route.fulfill({ status, json: mock.setBody });
    });
    await page.route('**/api/ups/variable/reset', route => {
      const post = record(route);
      mock.resets.push(post);
      const status = mock.resetStatus;
      if (status === 200) {
        const { name } = post.body as { name: string };
        const v = mock.variables.find(x => x.name === name);
        if (v) v.value = DRIVER_DEFAULTS[name];
        return route.fulfill({ status, json: { success: true, value: DRIVER_DEFAULTS[name] } });
      }
      return route.fulfill({ status, json: { error: 'Could not save the setting' } });
    });
  });

  test('navigation: third menu entry, page title, one GET when the tab opens', async ({ page }) => {
    await page.goto('http://esp32.local/');

    const tabs = page.locator('nav.tabs button.tab');
    await expect(tabs.nth(1)).toHaveAttribute('data-target', 'commands');
    const thirdTab = tabs.nth(2);
    await expect(thirdTab).toHaveAttribute('data-target', 'settings');
    await expect(thirdTab).toHaveText('UPS Settings');
    expect(mock.getHits).toBe(0);

    await thirdTab.click();
    await expect(page.locator('.page-title')).toHaveText('UPS Settings');
    await expect(page.locator('#content-settings')).toBeVisible();
    await expect(page.locator('.set-row')).toHaveCount(7);
    expect(mock.getHits).toBe(1);
    await expect(page.locator('#set-empty')).toBeHidden();
  });

  test('rows show name, description and current value, in the UPS and driver groups', async ({ page }) => {
    await openSettingsTab(page);

    for (const v of eaton3sCatalog()) {
      const row = settingRow(page, v.name);
      await expect(row.locator('.cmd-name')).toHaveText(v.name);
      await expect(row.locator('.cmd-desc')).toHaveText(v.description);
      await expect(row.locator('.set-current-value')).toHaveText(v.value);
    }

    const upsGroup = page.locator('section#set-group-ups');
    const driverGroup = page.locator('section#set-group-driver');
    await expect(upsGroup.locator('.set-row')).toHaveCount(4);
    await expect(driverGroup.locator('.set-row')).toHaveCount(3);
    await expect(upsGroup.locator('.set-row[data-var="battery.charge.low"]')).toHaveCount(1);
    await expect(driverGroup.locator('.set-row[data-var="ups.delay.shutdown"]')).toHaveCount(1);
    await expect(driverGroup.locator('.set-row[data-var="outlet.1.desc"]')).toHaveCount(1);
  });

  test('ENUM variables have a select with the current value, the others an input with maxlength', async ({ page }) => {
    await openSettingsTab(page);

    const sens = settingRow(page, 'input.sensitivity');
    const select = sens.locator('select');
    await expect(select).toHaveCount(1);
    await expect(sens.locator('input[type="text"]')).toHaveCount(0);
    await expect(select.locator('option')).toHaveText(['high', 'normal', 'low']);
    await expect(select).toHaveValue('normal');

    const shutdown = settingRow(page, 'ups.shutdown');
    await expect(shutdown.locator('select option')).toHaveText(['enabled', 'disabled']);
    await expect(shutdown.locator('select')).toHaveValue('enabled');

    const low = settingRow(page, 'battery.charge.low');
    const input = low.locator('input[type="text"]');
    await expect(low.locator('select')).toHaveCount(0);
    await expect(input).toHaveAttribute('maxlength', '10');
    await expect(input).toHaveValue('20');
    await expect(input).toHaveAttribute('aria-label', 'New value for battery.charge.low');
    await expect(settingRow(page, 'outlet.1.desc').locator('input[type="text"]')).toHaveAttribute('maxlength', '20');

    // Apply stays off until the value differs from the current one
    const apply = low.locator('button.set-apply');
    await expect(apply).toHaveText('Apply');
    await expect(apply).toBeDisabled();
    await input.fill('30');
    await expect(apply).toBeEnabled();
    await input.fill('20');
    await expect(apply).toBeDisabled();
  });

  test('Apply battery.charge.low=30: JSON body, "Applied" and the value re-read with a new GET', async ({ page }) => {
    await openSettingsTab(page);
    const row = settingRow(page, 'battery.charge.low');
    await expect(row.locator('.set-current-value')).toHaveText('20');
    const hits = mock.getHits;

    await row.locator('input[type="text"]').fill('30');
    await row.locator('button.set-apply').click();

    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Applied 20 → 30');
    expect(mock.sets).toEqual([{
      method: 'POST',
      contentType: expect.stringContaining('application/json'),
      body: { name: 'battery.charge.low', value: '30' },
    }]);
    expect(mock.resets).toHaveLength(0);
    expect(mock.getHits).toBe(hits + 1);
    await expect(row.locator('.set-current-value')).toHaveText('30');
    await expect(row.locator('input[type="text"]')).toHaveValue('30');
    await expect(row.locator('button.set-apply')).toBeDisabled();
  });

  test('Enter in the input applies the value', async ({ page }) => {
    await openSettingsTab(page);
    const row = settingRow(page, 'battery.runtime.low');

    await row.locator('input[type="text"]').fill('300');
    await row.locator('input[type="text"]').press('Enter');

    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Applied 180 → 300');
    expect(mock.sets.map(p => p.body)).toEqual([{ name: 'battery.runtime.low', value: '300' }]);
    await expect(row.locator('.set-current-value')).toHaveText('300');
  });

  test('only printable ASCII: an accented value is flagged in the row and nothing is sent', async ({ page }) => {
    await openSettingsTab(page);
    const row = settingRow(page, 'outlet.1.desc');
    const input = row.locator('input[type="text"]');
    const apply = row.locator('button.set-apply');

    await input.fill('Città A');
    await expect(row.locator('.cmd-result.error')).toHaveText('✕ Only printable ASCII characters (no accents or symbols such as €)');
    await expect(row.locator('.set-field')).toHaveClass(/invalid/);
    await expect(apply).toBeDisabled();
    await input.press('Enter');
    await expect(apply).toBeDisabled();
    expect(mock.sets).toHaveLength(0);

    // Back to ASCII: message gone, Apply on again
    await input.fill('Citta A');
    await expect(row.locator('.cmd-result')).toHaveText('');
    await expect(row.locator('.set-field')).not.toHaveClass(/invalid/);
    await expect(apply).toBeEnabled();
  });

  test('re-read after Apply fails: the list stays, with the outcome and the new value', async ({ page }) => {
    await openSettingsTab(page);
    const row = settingRow(page, 'battery.charge.low');
    const hits = mock.getHits;
    mock.variablesStatus = 500;

    await row.locator('input[type="text"]').fill('30');
    await row.locator('button.set-apply').click();

    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Applied 20 → 30');
    expect(mock.getHits).toBe(hits + 1);
    await expect(page.locator('#set-empty')).toBeHidden();
    await expect(page.locator('.set-row')).toHaveCount(7);
    await expect(row.locator('.set-current-value')).toHaveText('30');
    await expect(row.locator('button.set-apply')).toBeDisabled();
  });

  for (const status of [400, 500, 503]) {
    test(`Apply rejected with ${status}: the server message is shown and the value stays`, async ({ page }) => {
      mock.setStatus = status;
      mock.setBody = { error: SET_ERROR_BODIES[status] };
      await openSettingsTab(page);
      const row = settingRow(page, 'battery.charge.low');
      const hits = mock.getHits;

      await row.locator('input[type="text"]').fill('30');
      await row.locator('button.set-apply').click();

      await expect(row.locator('.cmd-result.error')).toHaveText(`✕ ${SET_ERROR_BODIES[status]}`);
      expect(mock.sets.map(p => p.body)).toEqual([{ name: 'battery.charge.low', value: '30' }]);
      expect(mock.getHits).toBe(hits + 1);
      await expect(row.locator('.set-current-value')).toHaveText('20');
      // The typed value stays in the field, to be corrected
      await expect(row.locator('input[type="text"]')).toHaveValue('30');
    });
  }

  test('Apply rejected without a server message: fallback texts by status', async ({ page }) => {
    await openSettingsTab(page);
    const row = settingRow(page, 'battery.charge.low');
    const input = row.locator('input[type="text"]');
    const result = row.locator('.cmd-result');

    const cases: [number, string][] = [
      [400, '✕ Invalid value'],
      [500, '✕ Rejected by the UPS'],
      [503, '✕ UPS not connected'],
      [502, '✕ Failed (HTTP 502)'],
    ];
    for (const [i, [status, text]] of cases.entries()) {
      mock.setStatus = status;
      mock.setBody = null;
      await input.fill(String(31 + i));
      await expect(result).toHaveText('');
      await row.locator('button.set-apply').click();
      await expect(row.locator('.cmd-result.error')).toHaveText(text);
      await expect(row.locator('.set-current-value')).toHaveText('20');
    }
    expect(mock.sets).toHaveLength(cases.length);
  });

  test('confirmation on ups.shutdown: Cancel sends nothing, OK sends the POST', async ({ page }) => {
    await openSettingsTab(page);
    const row = settingRow(page, 'ups.shutdown');
    const select = row.locator('select');
    const apply = row.locator('button.set-apply');
    const hits = mock.getHits;

    await select.selectOption('disabled');
    await expect(apply).toBeEnabled();

    // Cancel: the dialog names the variable and the new value, nothing is sent
    const dismissed = new Promise<{ type: string; message: string }>(resolve => {
      page.once('dialog', async dialog => {
        const seen = { type: dialog.type(), message: dialog.message() };
        await dialog.dismiss();
        resolve(seen);
      });
    });
    await apply.click();
    const { type, message } = await dismissed;
    expect(type).toBe('confirm');
    expect(message).toContain('ups.shutdown');
    expect(message).toBe(`Set "ups.shutdown" to "disabled"?

${SET_CONFIRM_WARNING}`);

    await page.waitForTimeout(500);
    expect(mock.sets).toHaveLength(0);
    expect(mock.getHits).toBe(hits);
    await expect(row.locator('.cmd-result')).toHaveText('');
    await expect(row.locator('.set-current-value')).toHaveText('enabled');
    await expect(select).toHaveValue('disabled');
    await expect(apply).toBeEnabled();

    // OK: the POST goes out and the value is re-read
    page.once('dialog', dialog => dialog.accept());
    await apply.click();
    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Applied enabled → disabled');
    expect(mock.sets).toEqual([{
      method: 'POST',
      contentType: expect.stringContaining('application/json'),
      body: { name: 'ups.shutdown', value: 'disabled' },
    }]);
    await expect(row.locator('.set-current-value')).toHaveText('disabled');
  });

  test('variables without confirm are applied without a dialog', async ({ page }) => {
    let dialogs = 0;
    page.on('dialog', dialog => { dialogs++; return dialog.dismiss(); });
    await openSettingsTab(page);
    const row = settingRow(page, 'input.sensitivity');

    await row.locator('select').selectOption('low');
    await row.locator('button.set-apply').click();
    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Applied normal → low');
    expect(dialogs).toBe(0);
    expect(mock.sets.map(p => p.body)).toEqual([{ name: 'input.sensitivity', value: 'low' }]);
  });

  test('"Reset to default" only on the driver-side rows; it posts the reset and shows the new value', async ({ page }) => {
    await openSettingsTab(page);

    await expect(page.locator('section#set-group-ups button.set-reset')).toHaveCount(0);
    await expect(page.locator('section#set-group-driver button.set-reset')).toHaveCount(3);
    for (const v of eaton3sCatalog()) {
      await expect(settingRow(page, v.name).locator('button.set-reset')).toHaveCount(v.driver_side ? 1 : 0);
    }

    const row = settingRow(page, 'ups.delay.shutdown');
    const reset = row.locator('button.set-reset');
    await expect(reset).toHaveText('Reset to default');
    await expect(row.locator('.set-current-value')).toHaveText('60');
    const hits = mock.getHits;

    await reset.click();

    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Reset to default 60 → 20');
    expect(mock.resets).toEqual([{
      method: 'POST',
      contentType: expect.stringContaining('application/json'),
      body: { name: 'ups.delay.shutdown' },
    }]);
    expect(mock.sets).toHaveLength(0);
    expect(mock.getHits).toBe(hits + 1);
    await expect(row.locator('.set-current-value')).toHaveText('20');
    await expect(row.locator('input[type="text"]')).toHaveValue('20');
  });

  test('AP mode: the page works as in STA and Apply sends the POST', async ({ page }) => {
    mock.apMode = true;
    await openSettingsTab(page);

    await expect(page.locator('.set-row')).toHaveCount(7);
    const row = settingRow(page, 'battery.charge.low');
    const input = row.locator('input[type="text"]');
    await expect(input).toBeEnabled();

    await input.fill('30');
    const apply = row.locator('button.set-apply');
    await expect(apply).toBeEnabled();
    await apply.click();

    await expect(row.locator('.cmd-result.success')).toHaveText('✓ Applied 20 → 30');
    expect(mock.sets).toEqual([{
      method: 'POST',
      contentType: expect.stringContaining('application/json'),
      body: { name: 'battery.charge.low', value: '30' },
    }]);
    await expect(row.locator('.set-current-value')).toHaveText('30');
    // Reset to default is available in AP too
    await expect(page.locator('section#set-group-driver button.set-reset')).toHaveCount(3);
  });

  test('empty state: UPS disconnected shows "No settings available"', async ({ page }) => {
    mock.connected = false;
    await openSettingsTab(page);

    await expect(page.locator('#set-empty')).toBeVisible();
    await expect(page.locator('#set-empty')).toContainText('No settings available');
    await expect(page.locator('#set-list')).toBeHidden();
    await expect(page.locator('.set-row')).toHaveCount(0);
  });

  test('empty state: empty catalog or failed GET shows "No settings available"', async ({ page }) => {
    mock.variables = [];
    await openSettingsTab(page);
    await expect(page.locator('#set-empty')).toBeVisible();
    await expect(page.locator('#set-empty')).toContainText('No settings available');
    await expect(page.locator('#set-list')).toBeHidden();

    // Back to the page with a catalog, then the GET fails
    mock.variables = eaton3sCatalog();
    await page.locator('button.tab[data-target="ups"]').click();
    await page.locator('button.tab[data-target="settings"]').click();
    await expect(page.locator('.set-row')).toHaveCount(7);
    await expect(page.locator('#set-empty')).toBeHidden();

    mock.variablesStatus = 500;
    await page.locator('button.tab[data-target="ups"]').click();
    await page.locator('button.tab[data-target="settings"]').click();
    await expect(page.locator('#set-empty')).toBeVisible();
    await expect(page.locator('#set-list')).toBeHidden();
    await expect(page.locator('.set-row')).toHaveCount(0);
  });

  test('no polling: no other GET of /api/ups/variables in 6 s', async ({ page }) => {
    await openSettingsTab(page);
    await expect(page.locator('.set-row')).toHaveCount(7);
    expect(mock.getHits).toBe(1);

    await page.waitForTimeout(6000);
    expect(mock.getHits).toBe(1);
  });

  test('mobile 375x812: no horizontal scroll', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await openSettingsTab(page);
    await expect(page.locator('.set-row')).toHaveCount(7);

    const overflow = await page.evaluate(() => ({
      scrollWidth: document.documentElement.scrollWidth,
      innerWidth: window.innerWidth,
    }));
    expect(overflow.scrollWidth).toBeLessThanOrEqual(overflow.innerWidth);
  });
});
