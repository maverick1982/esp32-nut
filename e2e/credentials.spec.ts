import { test, expect, Page } from '@playwright/test';
import { serveIndex, serveLogin } from './support/webui';

// US-063 — "Web Login" tab: change the Web UI credentials in STA (current
// password required, other sessions signed out) and reset them in AP mode
// (no current password, every session closed). The NUT credentials live in
// their own tab and are never touched. Serves the real data/www sources
// (support/webui.ts) and mocks the auth API with page.route.

const HOME = 'http://esp32.local/';

const USER = 'admin';
const CURRENT_PASSWORD = 'correct-horse';
const NEW_USER = 'operator';
const NEW_PASSWORD = 'battery-staple';

type CredResponse = { status: number; json: Record<string, unknown> };

interface CredMock {
  apMode: boolean;
  /** configured of /api/auth/status; false -> the first POST creates the credentials */
  configured: boolean;
  authenticated: boolean;
  username: string;
  credentialPosts: { contentType?: string; body: unknown }[];
  /** answer of POST /api/auth/credentials; null -> success echoing the username */
  credResponse: CredResponse | null;
  /** value of sessions_closed in the success body */
  sessionsClosed: boolean;
  /** every request (any method) to /api/nut/config */
  nutConfigRequests: string[];
}

function defaultMock(): CredMock {
  return {
    apMode: false,
    configured: true,
    authenticated: true,
    username: USER,
    credentialPosts: [],
    credResponse: null,
    sessionsClosed: true,
    nutConfigRequests: [],
  };
}

let mock: CredMock;

// Registered after serveIndex, so these take precedence over its API catch-all.
async function mockApi(page: Page) {
  await page.route('**/api/auth/status', route => {
    const body: Record<string, unknown> = {
      configured: mock.configured,
      authenticated: mock.authenticated,
      ap_mode: mock.apMode,
    };
    if (mock.authenticated) body.username = mock.username;
    return route.fulfill({ json: body });
  });

  await page.route('**/api/auth/credentials', route => {
    const req = route.request();
    if (req.method() !== 'POST') return route.fallback();
    const body = req.postDataJSON() as { username?: string };
    mock.credentialPosts.push({ contentType: req.headers()['content-type'], body });
    if (mock.credResponse) {
      return route.fulfill({ status: mock.credResponse.status, json: mock.credResponse.json });
    }
    mock.username = body.username ?? mock.username;
    if (!mock.configured) {
      // First creation (TOFU): body without sessions_closed, as createInitialCredentials
      mock.configured = true;
      return route.fulfill({ json: { success: true, username: mock.username } });
    }
    return route.fulfill({
      json: { success: true, username: mock.username, sessions_closed: mock.sessionsClosed },
    });
  });

  await page.route('**/api/nut/config', route => {
    mock.nutConfigRequests.push(route.request().method());
    return route.fulfill({ json: {} });
  });

  await page.route('**/api/ups-vars', route =>
    route.fulfill({ json: { 'ups.status': 'OL', 'battery.charge': '100', 'ups.load': '15' } }));
}

/** Opens the main UI, goes to "Web Login" and waits for /api/auth/status to be applied. */
async function openWebLogin(page: Page) {
  await page.goto(HOME);
  await page.locator('.tab[data-target="weblogin"]').click();
  await expect(page.locator('#content-weblogin')).toHaveClass(/active/);
  await expect(page.locator('.page-title')).toHaveText('Web Login');
  if (mock.authenticated) {
    await expect(page.locator('#cred-username')).toHaveValue(mock.username);
  }
  if (mock.apMode) {
    await expect(page.locator('#cred-ap-banner')).toBeVisible();
  }
}

async function fillNew(page: Page, username: string, password: string, confirm = password) {
  await page.locator('#cred-username').fill(username);
  await page.locator('#cred-password').fill(password);
  await page.locator('#cred-password2').fill(confirm);
}

test.describe('Web Login credentials form (US-063)', () => {
  test.beforeEach(async ({ page }) => {
    mock = defaultMock();
    await serveIndex(page);
    await mockApi(page);
  });

  test('STA change: prefilled username, mismatch blocked client-side, then saved and fields cleared', async ({ page }) => {
    await openWebLogin(page);

    // STA layout: current password asked, no AP banner, "Save credentials"
    await expect(page.locator('#cur-password')).toBeVisible();
    await expect(page.locator('#cred-ap-banner')).toBeHidden();
    await expect(page.locator('#cred-badge')).toBeHidden();
    await expect(page.locator('#btn-cred')).toHaveText('Save credentials');
    await expect(page.locator('#cred-fineprint')).toContainText('Saving signs out every other device');
    await expect(page.locator('#session-username')).toHaveText(USER);

    // Confirmation differs: error on the field, no request
    await page.locator('#cur-password').fill(CURRENT_PASSWORD);
    await fillNew(page, NEW_USER, NEW_PASSWORD, 'battery-stapler');
    await page.locator('#btn-cred').click();
    await expect(page.locator('#err-password2')).toBeVisible();
    await expect(page.locator('#err-password2')).toHaveText('Passwords do not match');
    await expect(page.locator('#cred-error-text')).toHaveText('Please fix the highlighted fields.');
    await expect(page.locator('#cred-success')).toBeHidden();
    expect(mock.credentialPosts).toHaveLength(0);

    // Valid data
    await page.locator('#cred-password2').fill(NEW_PASSWORD);
    await expect(page.locator('#err-password2')).toBeHidden();
    await page.locator('#btn-cred').click();

    await expect(page.locator('#cred-success')).toBeVisible();
    await expect(page.locator('#cred-success-text')).toHaveText(
      'Credentials saved. Other devices have been signed out.');
    await expect(page.locator('#cred-error')).toBeHidden();
    expect(mock.credentialPosts).toEqual([{
      contentType: 'application/json',
      body: { current_password: CURRENT_PASSWORD, username: NEW_USER, password: NEW_PASSWORD },
    }]);

    // Password fields emptied, username kept, sidebar updated, still on the page
    await expect(page.locator('#cur-password')).toHaveValue('');
    await expect(page.locator('#cred-password')).toHaveValue('');
    await expect(page.locator('#cred-password2')).toHaveValue('');
    await expect(page.locator('#cred-username')).toHaveValue(NEW_USER);
    await expect(page.locator('#session-username')).toHaveText(NEW_USER);
    await expect(page.locator('#btn-cred')).toBeEnabled();
    await expect(page).toHaveURL(HOME);
  });

  test('STA change: missing current password is blocked client-side', async ({ page }) => {
    await openWebLogin(page);

    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();

    await expect(page.locator('#err-current')).toBeVisible();
    await expect(page.locator('#err-current')).toHaveText('Enter your current password');
    await expect(page.locator('#cred-error-text')).toHaveText('Please fix the highlighted fields.');
    expect(mock.credentialPosts).toHaveLength(0);
  });

  test('STA change: sessions_closed false adds the "Log out of all devices" hint', async ({ page }) => {
    mock.sessionsClosed = false;
    await openWebLogin(page);

    await page.locator('#cur-password').fill(CURRENT_PASSWORD);
    await fillNew(page, USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();

    await expect(page.locator('#cred-success-text')).toHaveText(
      'Credentials saved. Other sessions could not be closed, use Log out of all devices');
    expect(mock.credentialPosts).toHaveLength(1);
    await expect(page.locator('#cred-password')).toHaveValue('');
  });

  test('wrong current password: 401 shows "Current password is wrong" and stays on the page', async ({ page }) => {
    mock.credResponse = { status: 401, json: { error: 'invalid_current_password' } };
    await openWebLogin(page);

    await page.locator('#cur-password').fill('wrong-password');
    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();

    await expect(page.locator('#cred-error')).toBeVisible();
    await expect(page.locator('#cred-error-text')).toHaveText('Current password is wrong');
    await expect(page.locator('#cred-success')).toBeHidden();
    await expect(page.locator('#cur-password')).toHaveValue('');
    await expect(page.locator('#cur-password')).toHaveClass(/invalid/);
    await expect(page.locator('#btn-cred')).toBeEnabled();
    // Not a logout: the 401 does not go through the session-expired redirect
    await expect(page).toHaveURL(HOME);
    await expect(page.locator('#session-username')).toHaveText(USER);
    expect(mock.credentialPosts).toEqual([{
      contentType: 'application/json',
      body: { current_password: 'wrong-password', username: NEW_USER, password: NEW_PASSWORD },
    }]);
  });

  test('too many attempts: 429 counts down with the button disabled, then re-enables it', async ({ page }) => {
    mock.credResponse = { status: 429, json: { error: 'too_many_attempts', retry_after: 3 } };
    await openWebLogin(page);

    const btn = page.locator('#btn-cred');

    // Each countdown step lasts 1 s, shorter than the slowest expect polling
    // interval: record every change of the alert and the button in the page,
    // so no step can be missed under load.
    await page.evaluate(() => {
      const w = window as unknown as { __throttleSteps: { text: string; shown: boolean; disabled: boolean }[] };
      const text = document.getElementById('cred-error-text')!;
      const alert = document.getElementById('cred-error')!;
      const button = document.getElementById('btn-cred') as HTMLButtonElement;
      w.__throttleSteps = [];
      const snap = () => w.__throttleSteps.push({
        text: text.textContent ?? '', shown: !alert.hidden, disabled: button.disabled,
      });
      new MutationObserver(snap).observe(document.getElementById('cred-form')!, {
        subtree: true, childList: true, characterData: true, attributes: true,
      });
    });

    await page.locator('#cur-password').fill('wrong-password');
    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await btn.click();

    await expect(page.locator('#cred-error-text')).toContainText('Too many attempts. Try again in');
    await expect(page.locator('#cur-password')).toHaveValue('');

    // At zero the message goes away and the button comes back
    await expect(btn).toBeEnabled({ timeout: 6000 });
    await expect(page.locator('#cred-error')).toBeHidden();

    const steps = await page.evaluate(() =>
      (window as unknown as { __throttleSteps: { text: string; shown: boolean; disabled: boolean }[] }).__throttleSteps);
    const shown = steps.filter(st => st.shown && st.text.startsWith('Too many attempts'));
    expect([...new Set(shown.map(st => st.text))]).toEqual([
      'Too many attempts. Try again in 3 s',
      'Too many attempts. Try again in 2 s',
      'Too many attempts. Try again in 1 s',
    ]);
    expect(shown.every(st => st.disabled)).toBe(true);
    expect(mock.credentialPosts).toHaveLength(1);
    await expect(page).toHaveURL(HOME);
  });

  test('AP reset: no current password, banner, body without current_password, all sessions closed', async ({ page }) => {
    mock.apMode = true;
    await openWebLogin(page);

    await expect(page.locator('#grp-current')).toBeHidden();
    await expect(page.locator('#cur-password')).toBeHidden();
    await expect(page.locator('#cred-ap-banner')).toContainText('Access Point mode');
    await expect(page.locator('#cred-badge')).toBeVisible();
    await expect(page.locator('#btn-cred')).toHaveText('Reset credentials');
    await expect(page.locator('#cred-fineprint')).toContainText('Resetting closes every session on every device');
    await expect(page.locator('#cred-fineprint')).toContainText('NUT clients cannot reach it');
    // No session row in AP mode
    await expect(page.locator('#sidebar-session')).toBeHidden();

    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();

    await expect(page.locator('#cred-success-text')).toHaveText(
      'Credentials reset. All sessions have been closed.');
    expect(mock.credentialPosts).toEqual([{
      contentType: 'application/json',
      body: { username: NEW_USER, password: NEW_PASSWORD },
    }]);
    await expect(page.locator('#cred-password')).toHaveValue('');
    await expect(page.locator('#cred-password2')).toHaveValue('');
    await expect(page.locator('#cred-username')).toHaveValue(NEW_USER);
    await expect(page).toHaveURL(HOME);
  });

  test('AP reset: sessions_closed false asks to try the reset again (no "Log out of all devices" in AP)', async ({ page }) => {
    mock.apMode = true;
    mock.sessionsClosed = false;
    await openWebLogin(page);

    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();

    await expect(page.locator('#cred-success-text')).toHaveText(
      'Credentials reset, but the existing sessions could not be closed. Try the reset again.');
    await expect(page.locator('#cred-success')).not.toContainText('Log out of all devices');
    await expect(page.locator('#btn-logout-all')).toBeHidden();
    expect(mock.credentialPosts).toHaveLength(1);
    await expect(page.locator('#btn-cred')).toHaveText('Reset credentials');
    await expect(page).toHaveURL(HOME);
  });

  test('AP first boot without credentials: "Create credentials", then "Credentials created."', async ({ page }) => {
    mock.apMode = true;
    mock.configured = false;
    mock.authenticated = false;
    await openWebLogin(page);

    await expect(page.locator('#cur-password')).toBeHidden();
    await expect(page.locator('#btn-cred')).toHaveText('Create credentials');

    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();

    await expect(page.locator('#cred-success-text')).toHaveText('Credentials created.');
    expect(mock.credentialPosts).toEqual([{
      contentType: 'application/json',
      body: { username: NEW_USER, password: NEW_PASSWORD },
    }]);
    await expect(page.locator('#cred-password')).toHaveValue('');
    await expect(page.locator('#cred-username')).toHaveValue(NEW_USER);
    // Now they exist: the next submit is a reset
    await expect(page.locator('#btn-cred')).toHaveText('Reset credentials');
    await expect(page).toHaveURL(HOME);
  });

  test('session gone: 401 unauthorized from the credentials POST goes to /login?expired=1', async ({ page }) => {
    // /login page too; the API mocks are registered again so they win over its catch-all
    await serveLogin(page);
    await mockApi(page);
    mock.credResponse = { status: 401, json: { error: 'unauthorized' } };
    await openWebLogin(page);

    await page.locator('#cur-password').fill(CURRENT_PASSWORD);
    await fillNew(page, NEW_USER, NEW_PASSWORD);
    // Session closed meanwhile: /login must not send us back home
    mock.authenticated = false;
    await page.locator('#btn-cred').click();

    await expect(page).toHaveURL('http://esp32.local/login?expired=1');
    await expect(page.locator('#signin-expired')).toBeVisible();
    expect(mock.credentialPosts).toHaveLength(1);
  });

  test('NUT credentials are separate: no NUT fields in the form, saving never calls /api/nut/config', async ({ page }) => {
    await openWebLogin(page);

    const form = page.locator('#cred-form');
    const ids = await form.locator('input').evaluateAll(els => els.map(el => el.id));
    expect(ids).toEqual(['cur-password', 'cred-username', 'cred-password', 'cred-password2']);
    await expect(page.locator('#content-weblogin [id^="nut-"]')).toHaveCount(0);

    await page.locator('#cur-password').fill(CURRENT_PASSWORD);
    await fillNew(page, NEW_USER, NEW_PASSWORD);
    await page.locator('#btn-cred').click();
    await expect(page.locator('#cred-success')).toBeVisible();

    expect(mock.credentialPosts).toHaveLength(1);
    expect(mock.nutConfigRequests).toEqual([]);

    // The fineprint link leads to the NUT Server tab, where the NUT fields live
    await page.locator('#cred-fineprint [data-goto="nut"]').click();
    await expect(page.locator('#content-nut')).toHaveClass(/active/);
    await expect(page.locator('#nut-username')).toBeVisible();
    expect(mock.nutConfigRequests).toEqual([]);
  });
});
