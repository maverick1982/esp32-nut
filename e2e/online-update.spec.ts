import { test, expect, Page } from '@playwright/test';
import { serveIndex, serveUpdatePage } from './support/webui';

// Online firmware update: dashboard notice (/api/system-status "update") and the
// GitHub section of the OTA page (/api/update, /api/update/check|install|settings).

type Status = Record<string, unknown>;

function status(over: Status = {}): Status {
  return {
    current: 'v1.16.0', latest: 'v1.17.0', available: true, installable: true,
    release_url: 'https://github.com/maverick1982/esp32-nut/releases/tag/v1.17.0',
    asset: { name: 'esp32-nut-v1.17.0-esp32-p4-eth-v1.zip', size: 3247719, verified: true },
    checked: true, last_check_ok: true, checked_age_s: 7200, error: '',
    phase: 'idle', progress: 0, auto_check: true,
    ...over,
  };
}

let current: Status;
let queue: Status[];
let posts: { url: string; body: unknown }[];
let postReply: { status: number; json: unknown };

async function mockApi(page: Page) {
  await page.route('**/api/update', route => {
    if (queue.length) current = queue.shift()!;
    return route.fulfill({ json: current });
  });
  await page.route('**/api/update/*', route => {
    const req = route.request();
    expect(req.headers()['content-type']).toContain('application/json');
    posts.push({ url: new URL(req.url()).pathname, body: req.postDataJSON() });
    return route.fulfill({ status: postReply.status, json: postReply.json });
  });
}

test.describe('Online firmware update', () => {
  test.beforeEach(async ({ page }) => {
    current = status();
    queue = [];
    posts = [];
    postReply = { status: 202, json: { success: true } };
    await serveIndex(page);
    await serveUpdatePage(page);
    await mockApi(page);
  });

  test('dashboard shows a notice when a newer release exists', async ({ page }) => {
    await page.route('**/api/system-status', route => route.fulfill({
      json: { version: 'v1.16.0', wifi: { status: 'Home' }, ups: { status: 'Back-UPS' }, update: { available: true, latest: 'v1.17.0' } },
    }));
    await page.goto('http://esp32.local/');
    const banner = page.locator('#update-banner');
    await expect(banner).toBeVisible();
    await expect(banner).toHaveText('Firmware v1.17.0 is available. Open Firmware OTA to install it.');
    await expect(banner).toHaveAttribute('href', '/update');
  });

  test('dashboard shows no notice when up to date', async ({ page }) => {
    await page.route('**/api/system-status', route => route.fulfill({
      json: { version: 'v1.17.0', wifi: { status: 'Home' }, ups: { status: 'Back-UPS' }, update: { available: false, latest: 'v1.17.0' } },
    }));
    await page.goto('http://esp32.local/');
    await expect(page.locator('#fw-version')).toHaveText('v1.17.0');
    await expect(page.locator('#update-banner')).toBeHidden();
  });

  test('OTA page shows versions and offers the install', async ({ page }) => {
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#ouCurrent')).toHaveText('v1.16.0');
    await expect(page.locator('#ouLatest a')).toHaveText('v1.17.0');
    await expect(page.locator('#ouLatest a')).toHaveAttribute('href', 'https://github.com/maverick1982/esp32-nut/releases/tag/v1.17.0');
    await expect(page.locator('#ouChecked')).toHaveText('2 hours ago');
    await expect(page.locator('#ouMsg')).toHaveText('Firmware v1.17.0 is available.');
    await expect(page.locator('#btnInstallUpdate')).toBeVisible();
    await expect(page.locator('#btnInstallUpdate')).toHaveText('Download and install v1.17.0');
  });

  test('up to date: no install button', async ({ page }) => {
    current = status({ current: 'v1.17.0', available: false, installable: false });
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#ouMsg')).toHaveText('The firmware is up to date.');
    await expect(page.locator('#btnInstallUpdate')).toBeHidden();
  });

  test('check button posts and shows the result', async ({ page }) => {
    current = status({ latest: '', available: false, installable: false, checked: false, checked_age_s: null });
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#ouChecked')).toHaveText('never');
    queue = [status({ phase: 'checking', latest: '', available: false, installable: false }), status({ checked_age_s: 1 })];
    await page.click('#btnCheckUpdate');
    await expect(page.locator('#ouMsg')).toHaveText('Firmware v1.17.0 is available.');
    await expect(page.locator('#ouChecked')).toHaveText('just now');
    await expect(page.locator('#btnCheckUpdate')).toBeEnabled();
    expect(posts).toEqual([{ url: '/api/update/check', body: {} }]);
  });

  test('check refused by the board shows its message', async ({ page }) => {
    postReply = { status: 429, json: { error: 'Checked a moment ago, try again in 30 seconds' } };
    await page.goto('http://esp32.local/update');
    await page.click('#btnCheckUpdate');
    await expect(page.locator('#ouMsg')).toHaveText('Checked a moment ago, try again in 30 seconds');
    await expect(page.locator('#ouMsg')).toHaveClass(/error/);
    await expect(page.locator('#btnCheckUpdate')).toBeEnabled();
  });

  test('failed check shows the error', async ({ page }) => {
    current = status({ latest: '', available: false, installable: false, last_check_ok: false, error: 'GitHub rate limit reached, try again later' });
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#ouMsg')).toHaveText('Check failed: GitHub rate limit reached, try again later');
  });

  test('install: confirm, progress, restart, new version', async ({ page }) => {
    await page.goto('http://esp32.local/update');
    page.once('dialog', d => d.accept());
    queue = [
      status({ phase: 'downloading', progress: 40 }),
      status({ phase: 'restarting', progress: 100 }),
      status({ current: 'v1.17.0', available: false, installable: false }),
    ];
    await page.click('#btnInstallUpdate');
    await expect(page.locator('#ouMsg')).toContainText('40%');
    await expect(page.locator('#ouProgressContainer')).toBeVisible();
    await expect(page.locator('#ouMsg')).toHaveText('Update complete: now running v1.17.0.', { timeout: 15000 });
    await expect(page.locator('#btnInstallUpdate')).toBeHidden();
    expect(posts).toEqual([{ url: '/api/update/install', body: {} }]);
  });

  test('install cancelled in the confirmation sends nothing', async ({ page }) => {
    await page.goto('http://esp32.local/update');
    page.once('dialog', d => d.dismiss());
    await page.click('#btnInstallUpdate');
    expect(posts).toHaveLength(0);
  });

  test('failed install shows the reason', async ({ page }) => {
    await page.goto('http://esp32.local/update');
    page.once('dialog', d => d.accept());
    queue = [status({ phase: 'failed', error: 'SHA-256 does not match the release' })];
    await page.click('#btnInstallUpdate');
    await expect(page.locator('#ouMsg')).toHaveText('Update failed: SHA-256 does not match the release. The running firmware is unchanged.');
    await expect(page.locator('#btnInstallUpdate')).toBeEnabled();
  });

  test('release without a checksum or a file for this board cannot be installed', async ({ page }) => {
    current = status({ installable: false, asset: { name: 'x.zip', size: 1, verified: false } });
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#ouMsg')).toContainText('publishes no checksum');
    await expect(page.locator('#btnInstallUpdate')).toBeHidden();
    current = status({ installable: false, asset: { name: '', size: 0, verified: false } });
    await page.reload();
    await expect(page.locator('#ouMsg')).toContainText('no file for this board');
  });

  test('Board Settings: daily check switch is saved, and restored when saving fails', async ({ page }) => {
    postReply = { status: 200, json: { success: true } };
    await page.goto('http://esp32.local/');
    await page.locator('button.tab[data-target="board"]').click();
    const sw = page.locator('#fwcheck-on');
    await expect(sw).toBeChecked();
    await expect(page.locator('#fwcheck-state')).toHaveText('On');
    await page.locator('#fwcheck-section .switch').click();
    await expect(page.locator('#fwcheck-result')).toHaveText('✓ Daily update check off · saved');
    await expect(page.locator('#fwcheck-state')).toHaveText('Off');
    expect(posts).toEqual([{ url: '/api/update/settings', body: { auto_check: false } }]);

    postReply = { status: 500, json: { error: 'Failed to save settings' } };
    await page.locator('#fwcheck-section .switch').click();
    await expect(page.locator('#fwcheck-error-text')).toHaveText('Failed to save settings');
    await expect(sw).not.toBeChecked();
  });

  test('Board Settings: switch disabled when the setting cannot be read', async ({ page }) => {
    await page.route('**/api/update', route => route.fulfill({ status: 503, json: { error: 'Online update not available' } }));
    await page.goto('http://esp32.local/');
    await page.locator('button.tab[data-target="board"]').click();
    await expect(page.locator('#fwcheck-error')).toBeVisible();
    await expect(page.locator('#fwcheck-on')).toBeDisabled();
  });

  test('OTA page has no daily check option', async ({ page }) => {
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#btnCheckUpdate')).toBeVisible();
    await expect(page.getByText('Check for updates daily')).toHaveCount(0);
  });

  test('manual upload still works on the same page', async ({ page }) => {
    await page.goto('http://esp32.local/update');
    await expect(page.locator('#dropzone')).toBeVisible();
    await expect(page.locator('#fileInput')).toHaveAttribute('accept', '.bin,.zip');
  });
});
