import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-124: reply of POST /api/nut/config, the settings apply at once without a restart
const APPLIED = (changed: boolean, sessionsClosed: number, applied = true) => ({
  success: true, applied, restart: false, changed, sessions_closed: sessionsClosed,
});

test.describe('NUT Configuration UI', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('submits NUT credentials successfully', async ({ page }) => {
    // Intercept the connect calls
    await page.route('**/api/nut/config', async route => {
      expect(route.request().method()).toBe('POST');
      const payload = route.request().postDataJSON();
      expect(payload.username).toBe('admin');
      expect(payload.password).toBe('secret');
      expect(payload.ups_name).toBe('eaton');
      await route.fulfill({ json: APPLIED(true, 2) });
    });

    await page.goto('http://esp32.local/');
    
    // Switch to NUT tab
    await page.click('button[data-target="nut"]');
    
    // Wait for panel
    const nutTab = page.locator('#content-nut');
    await expect(nutTab).toHaveClass(/active/);

    // Fill form
    await page.fill('#nut-upsname', 'eaton');
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');
    
    // Submit form
    await page.click('#btn-save-nut');
    
    // Verify the UI changes state
    await expect(page.locator('#btn-save-nut')).toHaveText(/Saved!/i);
    // US-124: applied at once, the two NUT clients reconnect
    await expect(page.locator('#nut-status')).toBeVisible();
    await expect(page.locator('#nut-status')).toHaveText(
      'Applied now, no restart. 2 NUT clients disconnected: they reconnect with the new settings.');
  });

  // US-082: the UPS name is never empty and only printable ASCII without spaces
  async function openNutTab(page: Page) {
    await page.goto('http://esp32.local/');
    await page.click('button[data-target="nut"]');
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
  }

  test('UPS Name placeholder is ups', async ({ page }) => {
    await openNutTab(page);
    await expect(page.locator('#nut-upsname')).toHaveAttribute('placeholder', 'ups');
  });

  test('invalid UPS names are blocked by the form', async ({ page }) => {
    let posts = 0;
    page.on('request', req => {
      if (req.method() === 'POST' && req.url().includes('/api/nut/config')) posts++;
    });
    await page.route('**/api/nut/config', route => route.fulfill({ json: { success: true } }));
    await openNutTab(page);
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');

    const input = page.locator('#nut-upsname');
    const button = page.locator('#btn-save-nut');
    for (const name of ['my ups', 'città', 'a"b', 'a\\b', 'a#b', 'a@b']) {
      await input.fill(name);
      await button.click();
      // The browser stops the submit: pattern mismatch, a message, and no "Saving..."
      expect(await input.evaluate((el: HTMLInputElement) => el.validity.patternMismatch), name).toBe(true);
      expect(await input.evaluate((el: HTMLInputElement) => el.validationMessage), name).not.toBe('');
      await expect(button).not.toHaveText(/Saving/);
    }
    expect(posts).toBe(0);
  });

  test('the pattern accepts the printable ASCII names', async ({ page }) => {
    await openNutTab(page);
    const input = page.locator('#nut-upsname');
    for (const name of ['ups', 'Back-UPS_1', '[ups]', '!~', 'a$b?c']) {
      await input.fill(name);
      expect(await input.evaluate((el: HTMLInputElement) => el.validity.valid), name).toBe(true);
    }
  });

  test('the default name ups is saved', async ({ page }) => {
    let body: { ups_name?: string } | null = null;
    await page.route('**/api/nut/config', async route => {
      if (route.request().method() === 'POST') body = route.request().postDataJSON();
      await route.fulfill({ json: { success: true } });
    });
    await openNutTab(page);
    await page.fill('#nut-upsname', 'ups');
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');
    await page.click('#btn-save-nut');

    await expect(page.locator('#btn-save-nut')).toHaveText(/Saved!/i);
    expect(body).not.toBeNull();
    expect(body!.ups_name).toBe('ups');
  });

  // --- US-124: settings applied without restarting the board ---

  async function saveNut(page: Page, reply: { status?: number; json: object }) {
    await page.route('**/api/nut/config', route =>
      route.fulfill({ status: reply.status ?? 200, json: reply.json }));
    await openNutTab(page);
    await page.fill('#nut-upsname', 'ups');
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'newpass');
    await page.click('#btn-save-nut');
  }

  test('saving the same values keeps the NUT clients connected', async ({ page }) => {
    await saveNut(page, { json: APPLIED(false, 0) });
    await expect(page.locator('#btn-save-nut')).toHaveText(/Saved!/i);
    await expect(page.locator('#nut-status')).toHaveText('No changes: NUT clients stay connected.');
  });

  test('one disconnected client uses the singular', async ({ page }) => {
    await saveNut(page, { json: APPLIED(true, 1) });
    await expect(page.locator('#nut-status')).toHaveText(
      'Applied now, no restart. 1 NUT client disconnected: it reconnects with the new settings.');
  });

  test('a change with no client connected says applied', async ({ page }) => {
    await saveNut(page, { json: APPLIED(true, 0) });
    await expect(page.locator('#nut-status')).toHaveText('Applied now, no restart.');
  });

  test('no restart is announced', async ({ page }) => {
    const dialogs: string[] = [];
    page.on('dialog', d => { dialogs.push(d.message()); void d.dismiss(); });
    await saveNut(page, { json: APPLIED(true, 2) });
    await expect(page.locator('#nut-status')).toBeVisible();
    expect(dialogs).toEqual([]);
    await expect(page.locator('.system-status')).not.toContainText(/Restarting/i);
    await expect(page.locator('#content-nut')).not.toContainText(/Restarting/i);
    await expect(page.locator('#nut-help')).toContainText('Changes apply at once, with no restart.');
    await expect(page.locator('#content-nut')).not.toContainText(/board (will )?restart/i);
  });

  test('without a running NUT server the settings wait for the next start', async ({ page }) => {
    await saveNut(page, { json: APPLIED(false, 0, false) });
    await expect(page.locator('#nut-status')).toHaveText(
      'Saved. The NUT server uses these settings at the next start.');
    await expect(page.locator('#nut-status')).toHaveClass(/info/);
  });

  // US-125: the API message is shown in the page, no dialog
  test('an API error shows no applied message', async ({ page }) => {
    const dialogs: string[] = [];
    page.on('dialog', d => { dialogs.push(d.message()); void d.dismiss(); });
    await saveNut(page, { status: 400, json: { error: 'Invalid UPS name' } });
    await expect(page.locator('#nut-error')).toBeVisible();
    await expect(page.locator('#nut-error')).toHaveText('Invalid UPS name');
    await expect(page.locator('#nut-status')).toBeHidden();
    await expect(page.locator('#btn-save-nut')).toHaveText(/Save Credentials/);
    expect(dialogs).toEqual([]);
  });

  // --- US-125: optional NUT credentials, the saved password is kept ---

  type NutMock = { username: string; has_password: boolean; ups_name?: string };

  /** /api/config with the given NUT settings; every POST body of /api/nut/config is collected. */
  async function mockNut(page: Page, nut: NutMock, reply: object = APPLIED(true, 0)) {
    const posts: Record<string, unknown>[] = [];
    await page.route('**/api/config', route =>
      route.fulfill({ json: { wifi: { ssid: 'Home', mode: 'STA' }, nut: { ups_name: 'ups', ...nut } } }));
    await page.route('**/api/nut/config', route => {
      posts.push(route.request().postDataJSON());
      return route.fulfill({ json: reply });
    });
    await openNutTab(page);
    return posts;
  }

  test('changing the UPS name keeps the saved password', async ({ page }) => {
    const posts = await mockNut(page, { username: 'admin', has_password: true });
    await expect(page.locator('#nut-password-state')).toHaveText('A password is set: leave the field empty to keep it.');
    await expect(page.locator('#nut-password')).toHaveAttribute('placeholder', 'Leave empty to keep the current password');
    await expect(page.locator('#nut-no-credentials')).toBeHidden();
    await expect(page.locator('#nut-username')).toHaveValue('admin');

    await page.fill('#nut-upsname', 'eaton');
    await page.click('#btn-save-nut');
    await expect(page.locator('#nut-status')).toHaveText('Applied now, no restart.');
    expect(posts).toHaveLength(1);
    expect(posts[0]).toMatchObject({ ups_name: 'eaton', username: 'admin' });
    expect(posts[0].password ?? '').toBe('');
    await expect(page.locator('#nut-password-state')).toHaveText(/A password is set/);
  });

  test('a username without a password is refused before sending', async ({ page }) => {
    const posts = await mockNut(page, { username: '', has_password: false });
    await page.fill('#nut-upsname', 'ups');
    await page.fill('#nut-username', 'admin');
    await page.click('#btn-save-nut');
    await expect(page.locator('#nut-error')).toHaveText(
      'Set both the NUT username and password, or remove the NUT credentials');
    await expect(page.locator('#nut-status')).toBeHidden();
    expect(posts).toHaveLength(0);
  });

  test('an empty username with a saved password is refused before sending', async ({ page }) => {
    const posts = await mockNut(page, { username: 'admin', has_password: true });
    await page.fill('#nut-username', '');
    await page.click('#btn-save-nut');
    await expect(page.locator('#nut-error')).toBeVisible();
    expect(posts).toHaveLength(0);
  });

  test('remove NUT credentials asks and clears them', async ({ page }) => {
    const posts = await mockNut(page, { username: 'admin', has_password: true }, APPLIED(true, 2));
    const dialogs: string[] = [];
    page.on('dialog', d => { dialogs.push(d.message()); void d.accept(); });

    const remove = page.locator('#btn-remove-nut-creds');
    await expect(remove).toBeVisible();
    await remove.click();

    await expect(page.locator('#nut-status')).toHaveText(
      'Applied now, no restart. 2 NUT clients disconnected: they reconnect with the new settings.');
    expect(dialogs).toHaveLength(1);
    expect(dialogs[0]).toContain('Remove the NUT credentials?');
    expect(dialogs[0]).toContain('NUT clients will only be able to read the data');
    expect(posts).toEqual([{ remove_credentials: true }]);
    await expect(page.locator('#nut-username')).toHaveValue('');
    await expect(page.locator('#nut-no-credentials')).toBeVisible();
    await expect(page.locator('#nut-password-state')).toHaveText('No password set.');
    await expect(remove).toBeHidden();
  });

  test('cancelling the removal sends nothing', async ({ page }) => {
    const posts = await mockNut(page, { username: 'admin', has_password: true });
    page.on('dialog', d => { void d.dismiss(); });
    await page.click('#btn-remove-nut-creds');
    await expect(page.locator('#btn-remove-nut-creds')).toBeVisible();
    await expect(page.locator('#nut-username')).toHaveValue('admin');
    expect(posts).toHaveLength(0);
  });

  test('without credentials the page warns and saves empty fields', async ({ page }) => {
    const posts = await mockNut(page, { username: '', has_password: false });
    await expect(page.locator('#nut-no-credentials')).toBeVisible();
    await expect(page.locator('#nut-no-credentials')).toContainText('NUT clients can only read the data');
    await expect(page.locator('#btn-remove-nut-creds')).toBeHidden();
    await expect(page.locator('#nut-username')).not.toHaveAttribute('required', /.*/);
    await expect(page.locator('#nut-password')).not.toHaveAttribute('required', /.*/);

    await page.fill('#nut-upsname', 'ups');
    await page.click('#btn-save-nut');
    await expect(page.locator('#nut-status')).toBeVisible();
    expect(posts).toEqual([{ ups_name: 'ups', username: '', password: '' }]);
  });

  test('setting new credentials hides the read-only warning', async ({ page }) => {
    await mockNut(page, { username: '', has_password: false });
    await page.fill('#nut-upsname', 'ups');
    await page.fill('#nut-username', 'admin');
    await page.fill('#nut-password', 'secret');
    await page.click('#btn-save-nut');
    await expect(page.locator('#nut-status')).toBeVisible();
    await expect(page.locator('#nut-no-credentials')).toBeHidden();
    await expect(page.locator('#btn-remove-nut-creds')).toBeVisible();
    await expect(page.locator('#nut-password')).toHaveValue('');
    await expect(page.locator('#nut-password-state')).toHaveText(/A password is set/);
  });
});
