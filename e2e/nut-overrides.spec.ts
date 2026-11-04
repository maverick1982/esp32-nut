import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-115 — custom variables of the NUT Server tab (override.<var> of ups.conf).
// Serves the real data/www sources and mocks GET/POST /api/nut/overrides: the
// page reads the list when the tab opens and posts the whole list on every
// add, edit and delete. The firmware answers with the saved list, where
// ups_value is the real UPS value (null when the UPS does not report the key).

interface Entry {
  key: string;
  value: string;
  description: string;
}

interface Mock {
  list: Entry[];
  upsValues: Record<string, string>;
  posts: Entry[][];
  postStatus: number;
  postError: string;
}

function answer(mock: Mock) {
  return {
    max: 16,
    max_value_length: 64,
    max_description_length: 64,
    overrides: mock.list.map(e => ({
      key: e.key,
      value: e.value,
      description: e.description,
      ups_value: mock.upsValues[e.key.toLowerCase()] ?? null,
    })),
  };
}

async function mockOverrides(page: Page, mock: Mock): Promise<void> {
  await page.route('**/api/nut/overrides', async route => {
    if (route.request().method() === 'POST') {
      expect(route.request().headers()['content-type']).toContain('application/json');
      const body = route.request().postDataJSON() as { overrides: Entry[] };
      mock.posts.push(body.overrides);
      if (mock.postStatus !== 200) {
        await route.fulfill({ status: mock.postStatus, json: { error: mock.postError, index: 0 } });
        return;
      }
      mock.list = body.overrides.map(o => ({ key: o.key, value: o.value, description: o.description ?? '' }));
    }
    await route.fulfill({ json: answer(mock) });
  });
}

function newMock(list: Entry[] = []): Mock {
  return {
    list,
    upsValues: { 'ups.model': 'Eaton 3S 700', 'battery.charge.low': '20' },
    posts: [],
    postStatus: 200,
    postError: '',
  };
}

async function openNutTab(page: Page) {
  await page.goto('http://esp32.local/');
  await page.click('.tab[data-target="nut"]');
  await expect(page.locator('#content-nut')).toHaveClass(/active/);
}

const row = (page: Page, key: string) => page.locator(`#cv-list .cv-row[data-key="${key}"]`);

test.describe('NUT Server custom variables', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('adds a variable the UPS does not report', async ({ page }) => {
    const mock = newMock();
    await mockOverrides(page, mock);
    await openNutTab(page);

    await expect(page.locator('#cv-empty')).toBeVisible();
    await expect(page.locator('#cv-count-n')).toHaveText('0');

    await page.fill('#cv-key', 'battery.date');
    await page.fill('#cv-value', '2025-03-01');
    await page.fill('#cv-desc', 'Battery replaced');
    await page.click('#cv-submit');

    await expect(row(page, 'battery.date')).toBeVisible();
    expect(mock.posts).toEqual([[{ key: 'battery.date', value: '2025-03-01', description: 'Battery replaced' }]]);
    await expect(row(page, 'battery.date').locator('.cv-tag')).toHaveText('Added');
    await expect(row(page, 'battery.date').locator('.cmd-desc')).toHaveText('Battery replaced');
    await expect(page.locator('#cv-count-n')).toHaveText('1');
    await expect(page.locator('#cv-empty')).toBeHidden();
    await expect(page.locator('#cv-ok')).toContainText('battery.date added');
    // The form is ready for the next one
    await expect(page.locator('#cv-key')).toHaveValue('');
  });

  test('overrides a UPS value, then edits and cancels', async ({ page }) => {
    const mock = newMock([{ key: 'battery.date', value: '2025-03-01', description: '' }]);
    await mockOverrides(page, mock);
    await openNutTab(page);
    await expect(row(page, 'battery.date')).toBeVisible();

    await page.fill('#cv-key', 'ups.model');
    await page.fill('#cv-value', 'Eaton 3S 700 studio');
    await page.click('#cv-submit');

    const model = row(page, 'ups.model');
    await expect(model.locator('.cv-tag')).toHaveText('Overrides UPS');
    await expect(model.locator('.cv-ups')).toHaveText('UPS: Eaton 3S 700');
    await expect(model.locator('.set-current-value')).toHaveText('Eaton 3S 700 studio');
    expect(mock.posts[0].map(e => e.key)).toEqual(['battery.date', 'ups.model']);

    // Edit: the form fills in, Save changes replaces the record in place
    await model.locator('.cv-act.edit').click();
    await expect(page.locator('#cv-form')).toHaveClass(/is-edit/);
    await expect(page.locator('#cv-key')).toHaveValue('ups.model');
    await expect(page.locator('#cv-submit-text')).toHaveText('Save changes');
    // The other rows are locked while one is in the form
    await expect(row(page, 'battery.date').locator('.cv-act.del')).toBeDisabled();
    await expect(model.locator('.cv-act.del')).toBeEnabled();
    await page.fill('#cv-value', 'Studio UPS');
    await page.click('#cv-submit');
    await expect(model.locator('.set-current-value')).toHaveText('Studio UPS');
    expect(mock.posts[1]).toEqual([
      { key: 'battery.date', value: '2025-03-01', description: '' },
      { key: 'ups.model', value: 'Studio UPS', description: '' },
    ]);

    // Cancel: nothing sent
    await model.locator('.cv-act.edit').click();
    await page.fill('#cv-value', 'Not saved');
    await page.click('#cv-cancel');
    await expect(page.locator('#cv-form')).not.toHaveClass(/is-edit/);
    await expect(page.locator('#cv-key')).toHaveValue('');
    await expect(model.locator('.set-current-value')).toHaveText('Studio UPS');
    expect(mock.posts.length).toBe(2);
  });

  test('deletes a variable after the confirmation', async ({ page }) => {
    const mock = newMock([
      { key: 'battery.date', value: '2025-03-01', description: '' },
      { key: 'ups.model', value: 'Studio', description: '' },
    ]);
    await mockOverrides(page, mock);
    await openNutTab(page);
    await expect(row(page, 'ups.model')).toBeVisible();

    // Dismissed: nothing sent
    page.once('dialog', dialog => dialog.dismiss());
    await row(page, 'ups.model').locator('.cv-act.del').click();
    await expect(row(page, 'ups.model')).toBeVisible();
    expect(mock.posts.length).toBe(0);

    page.once('dialog', dialog => {
      expect(dialog.message()).toContain('ups.model');
      expect(dialog.message()).toContain('Eaton 3S 700');
      dialog.accept();
    });
    await row(page, 'ups.model').locator('.cv-act.del').click();
    await expect(row(page, 'ups.model')).toHaveCount(0);
    expect(mock.posts).toEqual([[{ key: 'battery.date', value: '2025-03-01', description: '' }]]);
    await expect(page.locator('#cv-count-n')).toHaveText('1');
  });

  test('blocks invalid keys in the page and shows the API errors', async ({ page }) => {
    const mock = newMock();
    await mockOverrides(page, mock);
    await openNutTab(page);
    await expect(page.locator('#cv-empty')).toBeVisible();

    await page.fill('#cv-key', 'ups.status');
    await page.fill('#cv-value', 'OL');
    await page.click('#cv-submit');
    await expect(page.locator('#cv-error-text')).toHaveText('ups.status cannot be overridden');
    await expect(page.locator('#cv-key')).toHaveClass(/invalid/);

    await page.fill('#cv-key', 'battery');
    await expect(page.locator('#cv-error')).toBeHidden();
    await page.click('#cv-submit');
    await expect(page.locator('#cv-error-text')).toContainText('Invalid key "battery"');
    expect(mock.posts.length).toBe(0);

    // Upper case is a valid NUT name
    mock.postStatus = 400;
    mock.postError = 'Could not save';
    await page.fill('#cv-key', 'input.L1-N.voltage');
    await page.fill('#cv-value', '231');
    await page.click('#cv-submit');
    await expect(page.locator('#cv-error-text')).toHaveText('Could not save');
    expect(mock.posts.length).toBe(1);
    // The form keeps what was typed
    await expect(page.locator('#cv-key')).toHaveValue('input.L1-N.voltage');
    await expect(page.locator('#cv-value')).toHaveValue('231');
  });

  test('disables Add at the limit but keeps Edit', async ({ page }) => {
    const list: Entry[] = [];
    for (let i = 0; i < 16; i++) list.push({ key: `custom.v${i}`, value: String(i), description: '' });
    const mock = newMock(list);
    await mockOverrides(page, mock);
    await openNutTab(page);

    await expect(page.locator('#cv-count-n')).toHaveText('16');
    await expect(page.locator('#cv-count')).toHaveClass(/full/);
    await expect(page.locator('#cv-limit')).toBeVisible();
    await expect(page.locator('#cv-submit')).toBeDisabled();
    await expect(page.locator('#cv-key')).toBeDisabled();

    await row(page, 'custom.v3').locator('.cv-act.edit').click();
    await expect(page.locator('#cv-submit')).toBeEnabled();
    await expect(page.locator('#cv-limit')).toBeHidden();
  });

  test('UPS Settings shows an overridden variable as read-only', async ({ page }) => {
    const mock = newMock([{ key: 'battery.charge.low', value: '30', description: '' }]);
    await mockOverrides(page, mock);
    await page.route('**/api/ups/variables', route => route.fulfill({
      json: {
        connected: true,
        variables: [
          { name: 'battery.charge.low', description: 'Remaining battery level when UPS switches to LB (percent)', value: '20', max_length: 10, enum: [], driver_side: false, confirm: false, override: '30' },
          { name: 'battery.runtime.low', description: 'Remaining battery runtime when UPS switches to LB (seconds)', value: '180', max_length: 10, enum: [], driver_side: false, confirm: false },
        ],
      },
    }));
    await page.goto('http://esp32.local/');
    await page.click('.tab[data-target="settings"]');

    const locked = page.locator('.set-row[data-var="battery.charge.low"]');
    await expect(locked).toHaveClass(/is-overridden/);
    await expect(locked.locator('.set-tag.ovr')).toHaveText('override');
    await expect(locked.locator('input')).toBeDisabled();
    await expect(locked.locator('.set-apply')).toBeDisabled();
    await expect(locked.locator('.set-ovr-note b')).toHaveText('30');

    const free = page.locator('.set-row[data-var="battery.runtime.low"]');
    await expect(free).not.toHaveClass(/is-overridden/);
    await expect(free.locator('input')).toBeEnabled();

    // The link opens the NUT Server tab with the record
    await locked.locator('.cv-link').click();
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await expect(row(page, 'battery.charge.low')).toBeVisible();
    await expect(row(page, 'battery.charge.low').locator('.cv-tag')).toHaveText('Overrides UPS');
  });
});
