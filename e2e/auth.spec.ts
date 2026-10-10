import { test, expect, Page } from '@playwright/test';
import { serveIndex, serveLogin, serveUpdatePage } from './support/webui';

// US-061 — Web UI authentication: /login page (first setup and sign in), session
// expired while the main UI is open, logout from the sidebar, OTA upload without
// a session. Serves the real data/www sources (support/webui.ts) and mocks the
// auth API with page.route.

const HOME = 'http://esp32.local/';
const LOGIN = 'http://esp32.local/login';

const USER = 'admin';
const GOOD_PASSWORD = 'correct-horse';

interface AuthMock {
  configured: boolean;
  authenticated: boolean;
  apMode: boolean;
  /** /api/ups-vars answers 401 once this is true */
  expired: boolean;
  statusHits: number;
  upsVarsHits: number;
  /** polling requests (/api/ups-vars, /api/system-status) after the first 401 */
  pollsAfterExpiry: number;
  loginPosts: { contentType?: string; body: unknown }[];
  credentialPosts: { contentType?: string; body: unknown }[];
  logoutPosts: number;
}

function defaultMock(): AuthMock {
  return {
    configured: true,
    authenticated: false,
    apMode: false,
    expired: false,
    statusHits: 0,
    upsVarsHits: 0,
    pollsAfterExpiry: 0,
    loginPosts: [],
    credentialPosts: [],
    logoutPosts: 0,
  };
}

let mock: AuthMock;

// Registered after serveIndex/serveLogin, so these take precedence over the
// API catch-all of support/webui.ts.
async function mockApi(page: Page) {
  await page.route('**/api/auth/status', route => {
    mock.statusHits++;
    const body: Record<string, unknown> = {
      configured: mock.configured,
      authenticated: mock.authenticated,
      ap_mode: mock.apMode,
    };
    if (mock.authenticated) body.username = USER;
    return route.fulfill({ json: body });
  });

  await page.route('**/api/auth/login', route => {
    const req = route.request();
    if (req.method() !== 'POST') return route.fallback();
    const body = req.postDataJSON() as { username?: string; password?: string };
    mock.loginPosts.push({ contentType: req.headers()['content-type'], body });
    if (!mock.configured) {
      return route.fulfill({ status: 409, json: { error: 'setup_required' } });
    }
    if (body.username === USER && body.password === GOOD_PASSWORD) {
      mock.authenticated = true;
      return route.fulfill({ json: { success: true, username: USER } });
    }
    return route.fulfill({ status: 401, json: { error: 'invalid_credentials' } });
  });

  await page.route('**/api/auth/credentials', route => {
    const req = route.request();
    if (req.method() !== 'POST') return route.fallback();
    mock.credentialPosts.push({ contentType: req.headers()['content-type'], body: req.postDataJSON() });
    if (mock.configured) {
      return route.fulfill({ status: 409, json: { error: 'already_configured' } });
    }
    mock.configured = true;
    mock.authenticated = true;
    return route.fulfill({ json: { success: true, username: USER } });
  });

  await page.route('**/api/auth/logout', route => {
    if (route.request().method() !== 'POST') return route.fallback();
    mock.logoutPosts++;
    mock.authenticated = false;
    return route.fulfill({ json: { success: true } });
  });

  await page.route('**/api/ups-vars', route => {
    mock.upsVarsHits++;
    if (mock.expired) {
      mock.pollsAfterExpiry++;
      return route.fulfill({ status: 401, json: { error: 'unauthorized' } });
    }
    return route.fulfill({ json: { 'ups.status': 'OL', 'battery.charge': '100', 'ups.load': '15' } });
  });

  await page.route('**/api/system-status', route => {
    if (mock.expired) {
      mock.pollsAfterExpiry++;
      return route.fulfill({ status: 401, json: { error: 'unauthorized' } });
    }
    return route.fulfill({
      json: { wifi: { status: 'Connected', ssid: 'lab', ip: '192.168.1.50' }, ups: { status: 'Connected' } },
    });
  });
}

test.describe('Web UI authentication (US-061)', () => {
  test.beforeEach(async ({ page }) => {
    mock = defaultMock();
    // Both pages: the flows redirect from /login to / and back
    await serveIndex(page);
    await serveLogin(page);
    await mockApi(page);
  });

  test('first setup: client-side validation, then credentials created and redirect to /', async ({ page }) => {
    mock.configured = false;
    await page.goto(LOGIN);

    await expect(page.locator('#view-create')).toBeVisible();
    await expect(page.locator('#view-create h2')).toHaveText('Create credentials');
    await expect(page.locator('#view-signin')).toBeHidden();

    // Password and confirmation differ
    await page.locator('#new-username').fill(USER);
    await page.locator('#new-password').fill(GOOD_PASSWORD);
    await page.locator('#new-password2').fill('another-horse');
    await page.locator('#btn-create').click();
    await expect(page.locator('#err-password2')).toBeVisible();
    await expect(page.locator('#err-password2')).toHaveText('Passwords do not match');
    await expect(page.locator('#create-error')).toBeVisible();

    // Password of 7 characters
    await page.locator('#new-password').fill('short12');
    await page.locator('#new-password2').fill('short12');
    await page.locator('#btn-create').click();
    await expect(page.locator('#err-password')).toBeVisible();
    await expect(page.locator('#err-password')).toHaveText('Password is too short (minimum 8 characters)');
    await expect(page.locator('#create-error')).toBeVisible();
    await expect(page).toHaveURL(LOGIN);
    expect(mock.credentialPosts).toHaveLength(0);

    // Valid data
    await page.locator('#new-password').fill(GOOD_PASSWORD);
    await page.locator('#new-password2').fill(GOOD_PASSWORD);
    await expect(page.locator('#err-password')).toBeHidden();
    await expect(page.locator('#err-password2')).toBeHidden();
    await page.locator('#btn-create').click();

    await expect(page).toHaveURL(HOME);
    expect(mock.credentialPosts).toEqual([
      { contentType: 'application/json', body: { username: USER, password: GOOD_PASSWORD } },
    ]);
  });

  test('sign in: wrong credentials show an error and stay, right ones redirect to /', async ({ page }) => {
    await page.goto(LOGIN);

    await expect(page.locator('#view-signin')).toBeVisible();
    await expect(page.locator('#view-create')).toBeHidden();
    await expect(page.locator('#signin-expired')).toBeHidden();

    await page.locator('#login-username').fill(USER);
    await page.locator('#login-password').fill('wrong-password');
    await page.locator('#btn-signin').click();

    await expect(page.locator('#signin-error')).toBeVisible();
    await expect(page.locator('#signin-error-text')).toHaveText('Invalid username or password');
    await expect(page.locator('#login-password')).toHaveValue('');
    await expect(page).toHaveURL(LOGIN);
    expect(mock.loginPosts).toHaveLength(1);

    await page.locator('#login-password').fill(GOOD_PASSWORD);
    await page.locator('#btn-signin').click();

    await expect(page).toHaveURL(HOME);
    expect(mock.loginPosts).toEqual([
      { contentType: 'application/json', body: { username: USER, password: 'wrong-password' } },
      { contentType: 'application/json', body: { username: USER, password: GOOD_PASSWORD } },
    ]);
  });

  test('expired session: a 401 from the polling goes to /login?expired=1 and stops the polling', async ({ page }) => {
    mock.authenticated = true;
    await page.goto(HOME);

    // Main UI open and polling
    await expect(page.locator('#content-ups')).toHaveClass(/active/);
    await expect.poll(() => mock.upsVarsHits).toBeGreaterThanOrEqual(1);

    // The session expires: the next poll gets 401
    mock.authenticated = false;
    mock.expired = true;

    await expect(page).toHaveURL(`${LOGIN}?expired=1`);
    await expect(page.locator('#signin-expired')).toBeVisible();
    await expect(page.locator('#signin-expired')).toContainText('Your session has expired');
    await expect(page.locator('#view-signin')).toBeVisible();

    // Only the request that triggered the redirect reached the device. The
    // check holds for 3.5 s, longer than both polling periods (2 s ups-vars,
    // 3 s system-status): any further request would make it fail.
    const afterRedirect = mock.pollsAfterExpiry;
    expect(afterRedirect).toBeGreaterThanOrEqual(1);
    const since = Date.now();
    await expect.poll(
      () => ({ polls: mock.pollsAfterExpiry, waited: Date.now() - since >= 3500 }),
      { timeout: 6000, intervals: [250] },
    ).toEqual({ polls: afterRedirect, waited: true });
  });

  test('logout: the sidebar button posts /api/auth/logout and goes to /login', async ({ page }) => {
    mock.authenticated = true;
    await page.goto(HOME);

    await expect(page.locator('#sidebar-session')).toBeVisible();
    await expect(page.locator('#session-username')).toHaveText(USER);
    await expect(page.locator('#btn-logout')).toBeVisible();

    await page.locator('#btn-logout').click();

    await expect(page).toHaveURL(LOGIN);
    expect(mock.logoutPosts).toBe(1);
    await expect(page.locator('#view-signin')).toBeVisible();
    await expect(page.locator('#signin-expired')).toBeHidden();
  });

  test('AP mode: the logout button is not shown', async ({ page }) => {
    mock.apMode = true;
    mock.authenticated = true;
    await page.goto(HOME);

    // The status has been answered and the UI has kept polling for 2 s since:
    // the handler of /api/auth/status has long run
    await expect.poll(() => mock.statusHits).toBeGreaterThanOrEqual(1);
    await expect.poll(() => mock.upsVarsHits, { timeout: 10000 }).toBeGreaterThanOrEqual(2);

    await expect(page.locator('#sidebar-session')).toBeHidden();
    await expect(page.locator('#btn-logout')).toBeHidden();
  });
});

test.describe('OTA upload without a session (US-061)', () => {
  test('a 401 from POST /update goes to /login', async ({ page }) => {
    mock = defaultMock();
    await serveLogin(page);
    await serveUpdatePage(page);
    await mockApi(page);

    let uploads = 0;
    await page.route('http://esp32.local/update', route => {
      if (route.request().method() !== 'POST') return route.fallback();
      uploads++;
      return route.fulfill({ status: 401, json: { error: 'unauthorized' } });
    });

    await page.goto('http://esp32.local/update');
    await expect(page.locator('#fileInput')).toBeAttached();

    await page.locator('#fileInput').setInputFiles({
      name: 'firmware.bin',
      mimeType: 'application/octet-stream',
      buffer: Buffer.from([0xE9, 0x01, 0x02, 0x03]),
    });

    await expect(page).toHaveURL(LOGIN);
    expect(uploads).toBe(1);
    await expect(page.locator('#view-signin')).toBeVisible();
  });
});
