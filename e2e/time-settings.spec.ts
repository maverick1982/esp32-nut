import { test, expect, Page } from '@playwright/test';
import { serveIndex, serveLogin } from './support/webui';

// US-117: Time section of Board Settings (ADR 0009). GET/POST /api/time are mocked; the
// time zones of the select come from "options.zones" of the GET.

type State = 'disabled' | 'waiting' | 'synchronized' | 'no_answer';

interface TimeSettings {
  ntp: boolean;
  server: string;
  zone: string;
}

interface Status {
  state: State;
  local?: string;
  abbr?: string;
  utc?: string;
  since_sync_s?: number;
}

interface Mock {
  time: TimeSettings;
  status: Status;
  zones: string[];
  /** POST bodies received */
  posts: any[];
  /** GET requests received */
  gets: number;
  /** status of the next POST answer, and its error text */
  postStatus: number;
  postError: string;
}

const ZONES = ['UTC', 'Europe/London', 'Europe/Rome', 'America/New_York', 'Asia/Tokyo'];

function newMock(): Mock {
  return {
    time: { ntp: true, server: 'pool.ntp.org', zone: 'UTC' },
    status: { state: 'waiting' },
    zones: [...ZONES],
    posts: [],
    gets: 0,
    postStatus: 200,
    postError: '',
  };
}

async function routeCommon(page: Page) {
  await page.route('**/api/config', route => route.fulfill({ json: {} }));
  await page.route('**/api/system-status', route => route.fulfill({
    json: { wifi: { status: 'Connected', ssid: 'lab', ip: '192.168.1.50' }, ups: { status: 'Connected' } },
  }));
  await page.route('**/api/auth/status', route => route.fulfill({
    json: { configured: true, authenticated: true, ap_mode: false, username: 'admin' },
  }));
  await page.route('**/api/board', route => route.fulfill({
    json: {
      id: 'esp32s3_devkit', label: 'ESP32-S3 DevKitC-1',
      led: { present: true, on: true, level: 3, levels: 5 },
      usb: { ports: [{ id: 'otg', label: 'USB OTG' }], port: 'otg' },
      auto_shutdown: { on: false, available: true },
    },
  }));
}

async function routeTime(page: Page, mock: Mock) {
  await page.route('**/api/time', async route => {
    const req = route.request();
    if (req.method() === 'GET') {
      mock.gets++;
      await route.fulfill({
        json: { time: mock.time, status: mock.status, options: { zones: mock.zones } },
        headers: { 'Cache-Control': 'no-store' },
      });
      return;
    }
    const body = req.postDataJSON();
    mock.posts.push(body);
    if (mock.postStatus !== 200) {
      await route.fulfill({ status: mock.postStatus, json: { error: mock.postError } });
      return;
    }
    mock.time = { ...mock.time, ...body.time };
    await route.fulfill({ json: { success: true } });
  });
}

async function openBoard(page: Page) {
  await page.goto('http://esp32.local/');
  await page.locator('button.tab[data-target="board"]').click();
  await expect(page.locator('#content-board')).toHaveClass(/active/);
  await expect(page.locator('#time-section')).toBeVisible();
}

test.describe('Time section (US-117)', () => {
  let mock: Mock;

  test.beforeEach(async ({ page }) => {
    mock = newMock();
    await serveIndex(page);
    await routeCommon(page);
    await routeTime(page, mock);
  });

  test('shows the defaults and saves a new time zone without a restart', async ({ page }) => {
    await openBoard(page);
    await expect(page.locator('#time-ntp')).toBeChecked();
    await expect(page.locator('#time-ntp-state')).toHaveText('On');
    await expect(page.locator('#time-server')).toHaveValue('pool.ntp.org');
    await expect(page.locator('#time-zone')).toHaveValue('UTC');

    await page.locator('#time-zone').selectOption('Europe/Rome');
    await page.locator('#btn-save-time').click();
    await expect(page.locator('#time-result')).toContainText('Saved');
    expect(mock.posts).toHaveLength(1);
    expect(mock.posts[0]).toEqual({ time: { ntp: true, server: 'pool.ntp.org', zone: 'Europe/Rome' } });
    // Applied live: no restart countdown
    await expect(page.locator('#board-success')).toBeHidden();
    await expect(page.locator('#time-zone')).toHaveValue('Europe/Rome');
  });

  test('the select holds exactly the zones of the GET', async ({ page }) => {
    mock.zones = ['UTC', 'Australia/Sydney', 'America/Sao_Paulo'];
    await openBoard(page);
    const values = await page.locator('#time-zone option').evaluateAll(
      opts => opts.map(o => (o as HTMLOptionElement).value));
    expect(values).toEqual(['UTC', 'Australia/Sydney', 'America/Sao_Paulo']);
    // Underscores are shown as spaces
    await expect(page.locator('#time-zone option[value="America/Sao_Paulo"]')).toHaveText('America/Sao Paulo');
  });

  const STATES: { status: Status; badge: string; cls: RegExp; now: string }[] = [
    { status: { state: 'disabled' }, badge: 'Off', cls: /idle/, now: '—' },
    { status: { state: 'waiting' }, badge: 'Waiting', cls: /connecting/, now: '—' },
    {
      status: { state: 'synchronized', local: '2026-11-05 09:12:03', abbr: 'CET', utc: '2026-11-05T08:12:03Z', since_sync_s: 42 },
      badge: 'Synchronized', cls: /connected/, now: '2026-11-05 09:12:03 CET',
    },
    { status: { state: 'no_answer' }, badge: 'No answer', cls: /down/, now: '—' },
  ];
  for (const s of STATES) {
    test(`state ${s.status.state}: badge and board time`, async ({ page }) => {
      mock.status = s.status;
      if (s.status.state === 'disabled') mock.time = { ntp: false, server: '', zone: 'UTC' };
      await openBoard(page);
      await expect(page.locator('#time-state .badge-text')).toHaveText(s.badge);
      await expect(page.locator('#time-state')).toHaveClass(s.cls);
      await expect(page.locator('#time-now')).toHaveText(s.now);
    });
  }

  test('client-side checks stop the POST', async ({ page }) => {
    await openBoard(page);
    await page.locator('#time-server').fill('');
    await page.locator('#btn-save-time').click();
    await expect(page.locator('#time-error-text')).toHaveText('Server required to enable NTP');
    await expect(page.locator('#f-time-server')).toHaveClass(/invalid/);

    await page.locator('#time-server').fill('bad host!');
    await page.locator('#btn-save-time').click();
    await expect(page.locator('#time-error-text')).toContainText('Invalid server');
    expect(mock.posts).toHaveLength(0);
  });

  test('a 400 of the server is shown as it is', async ({ page }) => {
    mock.postStatus = 400;
    mock.postError = 'Invalid time zone';
    await openBoard(page);
    await page.locator('#time-zone').selectOption('Asia/Tokyo');
    await page.locator('#btn-save-time').click();
    await expect(page.locator('#time-error-text')).toHaveText('Invalid time zone');
    await expect(page.locator('#f-time-zone')).toHaveClass(/invalid/);
    expect(mock.posts).toHaveLength(1);
  });

  test('with NTP off the server is not required', async ({ page }) => {
    await openBoard(page);
    await page.locator('#time-form label.switch').click();
    await expect(page.locator('#time-ntp')).not.toBeChecked();
    await expect(page.locator('#time-ntp-state')).toHaveText('Off');
    await page.locator('#time-server').fill('');
    await page.locator('#btn-save-time').click();
    await expect(page.locator('#time-result')).toContainText('NTP off');
    expect(mock.posts).toEqual([{ time: { ntp: false, server: '', zone: 'UTC' } }]);
  });

  test('the Syslog hint links to the Time section', async ({ page }) => {
    await page.goto('http://esp32.local/');
    await page.locator('[data-target="logs"]').dispatchEvent('click');
    const link = page.locator('#syslog-section [data-goto="board"]');
    await expect(link).toHaveText('Board Settings → Time');
    await expect(page.locator('#syslog-section')).not.toContainText('The board has no clock');
    await link.click();
    await expect(page.locator('#content-board')).toHaveClass(/active/);
  });
});

test.describe('Time section (US-117): expired session', () => {
  test('a 401 on GET /api/time goes to the login page', async ({ page }) => {
    await serveIndex(page);
    await serveLogin(page);
    await page.route('**/api/time', route => route.fulfill({ status: 401, json: { error: 'unauthorized' } }));
    await page.goto('http://esp32.local/');
    await page.click('[data-target="board"]');
    await expect(page).toHaveURL('http://esp32.local/login?expired=1');
  });
});

test.describe('Time section (US-117): phone', () => {
  test.use({ viewport: { width: 375, height: 812 } });

  test('the section causes no horizontal scroll', async ({ page }) => {
    await serveIndex(page);
    await routeCommon(page);
    const mock = newMock();
    mock.time = { ntp: true, server: 'a-very-long-ntp-server-name.example.lan', zone: 'America/New_York' };
    mock.status = { state: 'synchronized', local: '2026-11-05 03:12:03', abbr: 'EST', utc: '2026-11-05T08:12:03Z', since_sync_s: 5 };
    await routeTime(page, mock);
    await page.goto('http://esp32.local/');
    await page.locator('[data-target="board"]').dispatchEvent('click');
    await expect(page.locator('#time-now')).toHaveText('2026-11-05 03:12:03 EST');
    const sizes = await page.evaluate(() => ({
      scroll: document.documentElement.scrollWidth,
      client: document.documentElement.clientWidth,
    }));
    expect(sizes.scroll).toBeLessThanOrEqual(sizes.client);
  });
});
