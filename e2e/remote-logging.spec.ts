import { test, expect, Page } from '@playwright/test';
import { serveIndex, serveLogin } from './support/webui';

// US-116: Remote logging panel and syslog indicator of the System Logs page (ADR 0012).
// GET/POST /api/logging are mocked; the options of the selects come from the GET.

type State = 'disabled' | 'waiting' | 'ready' | 'active' | 'error';

interface Syslog {
  enabled: boolean;
  host: string;
  port: number;
  protocol: string;
  facility: string;
  min_level: string;
  rate: number;
}

interface Mock {
  syslog: Syslog;
  status: { state: State; target: string; error: string; sent: number; failed: number; rate_limited: number; not_ready: number };
  /** POST bodies received */
  posts: any[];
  /** GET requests received */
  gets: number;
  /** status of the next POST answer, and its error text */
  postStatus: number;
  postError: string;
  /** state the device reaches once a POST is applied */
  afterPost: State | null;
}

const OPTIONS = {
  protocols: ['rfc3164', 'rfc5424'],
  facilities: ['user', 'daemon', 'local0', 'local1', 'local2', 'local3', 'local4', 'local5', 'local6', 'local7'],
  levels: ['INFO', 'WARN', 'ERROR'],
  rate_min: 1,
  rate_max: 100,
};

function defaults(): Syslog {
  return { enabled: false, host: '', port: 514, protocol: 'rfc3164', facility: 'local0', min_level: 'INFO', rate: 10 };
}

function newMock(): Mock {
  return {
    syslog: defaults(),
    status: { state: 'disabled', target: '', error: '', sent: 0, failed: 0, rate_limited: 0, not_ready: 0 },
    posts: [],
    gets: 0,
    postStatus: 200,
    postError: '',
    afterPost: null,
  };
}

async function routeLogging(page: Page, mock: Mock): Promise<void> {
  await page.route('**/api/logging', async route => {
    const req = route.request();
    if (req.method() === 'GET') {
      mock.gets++;
      return route.fulfill({
        json: { syslog: mock.syslog, status: { syslog: mock.status }, options: OPTIONS },
        headers: { 'Cache-Control': 'no-store' },
      });
    }
    if (req.method() === 'POST') {
      const body = JSON.parse(req.postData() || '{}');
      mock.posts.push(body);
      if (mock.postStatus !== 200) {
        return route.fulfill({ status: mock.postStatus, json: { error: mock.postError } });
      }
      mock.syslog = { ...mock.syslog, ...body.syslog };
      if (mock.afterPost) {
        mock.status = {
          ...mock.status,
          state: mock.afterPost,
          target: mock.syslog.enabled ? `${mock.syslog.host}:${mock.syslog.port}` : '',
        };
      }
      return route.fulfill({ json: { success: true } });
    }
    return route.fallback();
  });
}

async function openLogs(page: Page): Promise<void> {
  await page.goto('http://esp32.local/');
  await page.click('[data-target="logs"]');
  await expect(page.locator('#content-logs')).toHaveClass(/active/);
}

test.describe('Remote logging (US-116)', () => {
  let mock: Mock;

  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
    mock = newMock();
    await routeLogging(page, mock);
  });

  test('configures and saves syslog without a restart, then the indicator shows Active', async ({ page }) => {
    mock.afterPost = 'active';
    await openLogs(page);

    // Panel under the terminal, defaults from the GET
    const terminalBox = await page.locator('#terminal-output').boundingBox();
    const panelBox = await page.locator('#remote-log').boundingBox();
    expect(panelBox!.y).toBeGreaterThan(terminalBox!.y);
    await expect(page.locator('#remote-log h2')).toHaveText('Remote logging');
    await expect(page.locator('#syslog-title')).toHaveText('Syslog');
    await expect(page.locator('#syslog-on')).not.toBeChecked();
    await expect(page.locator('#syslog-port')).toHaveValue('514');
    await expect(page.locator('#syslog-format')).toHaveValue('rfc3164');
    await expect(page.locator('#syslog-facility')).toHaveValue('local0');
    await expect(page.locator('#syslog-level')).toHaveValue('INFO');
    await expect(page.locator('#syslog-rate')).toHaveValue('10');
    await expect(page.locator('#syslog-chip-text')).toHaveText('Off');
    await expect(page.locator('#remote-log')).toContainText('not encrypted');

    await page.locator('#syslog-form .switch').click();
    await expect(page.locator('#syslog-on-state')).toHaveText('On');
    await page.fill('#syslog-host', '192.168.1.10');
    await page.click('#btn-save-syslog');

    await expect(page.locator('#syslog-result')).toContainText('Saved, sending to 192.168.1.10:514');
    expect(mock.posts).toHaveLength(1);
    expect(mock.posts[0]).toEqual({
      syslog: { enabled: true, host: '192.168.1.10', port: 514, protocol: 'rfc3164', facility: 'local0', min_level: 'INFO', rate: 10 },
    });
    // The GET after the save reads the new state
    await expect(page.locator('#syslog-chip-text')).toHaveText('Active → 192.168.1.10:514');
    await expect(page.locator('#syslog-chip-dot')).toHaveClass(/success/);
    await expect(page.locator('#syslog-badge')).toHaveClass(/connected/);
    await expect(page.locator('#syslog-target')).toHaveText('192.168.1.10:514');
  });

  const STATES: { state: State; error: string; dot: string; badge: string; chip: string }[] = [
    { state: 'disabled', error: '', dot: 'off', badge: 'idle', chip: 'Off' },
    { state: 'waiting', error: 'No network connection', dot: 'warning', badge: 'connecting', chip: 'Waiting → 192.168.1.10:514' },
    { state: 'ready', error: '', dot: 'info', badge: 'ap', chip: 'Ready → 192.168.1.10:514' },
    { state: 'active', error: '', dot: 'success', badge: 'connected', chip: 'Active → 192.168.1.10:514' },
    { state: 'error', error: 'Send to 192.168.1.10 failed', dot: 'danger', badge: 'down', chip: 'Error → 192.168.1.10:514' },
  ];
  for (const s of STATES) {
    test(`indicator and status row for the state "${s.state}"`, async ({ page }) => {
      const on = s.state !== 'disabled';
      mock.syslog = { ...defaults(), enabled: on, host: '192.168.1.10' };
      mock.status = {
        state: s.state, target: on ? '192.168.1.10:514' : '', error: s.error,
        sent: 1284, failed: 17, rate_limited: 3, not_ready: 14,
      };
      await openLogs(page);

      await expect(page.locator('#syslog-chip-text')).toHaveText(s.chip);
      await expect(page.locator('#syslog-chip-dot')).toHaveClass(new RegExp(`status-indicator ${s.dot}`));
      await expect(page.locator('#syslog-badge')).toHaveClass(new RegExp(`link-badge ${s.badge}`));
      if (on) {
        await expect(page.locator('#syslog-sent')).toHaveText('1,284');
        await expect(page.locator('#syslog-failed')).toHaveText('17');
        await expect(page.locator('#syslog-limited')).toHaveText('3');
        await expect(page.locator('#syslog-dropped')).toHaveText('14');
      } else {
        await expect(page.locator('#syslog-sent')).toHaveText('—');
      }
      const problem = page.locator('#syslog-problem');
      if (s.error) {
        await expect(problem).toBeVisible();
        await expect(problem).toContainText(s.error);
        await expect(page.locator('#syslog-chip')).toHaveAttribute('title', s.error);
        if (s.state === 'waiting') await expect(problem).toHaveClass(/warn/);
        else await expect(problem).not.toHaveClass(/warn/);
      } else {
        await expect(problem).toBeHidden();
      }
    });
  }

  test('client-side validation blocks the POST', async ({ page }) => {
    await openLogs(page);
    const error = page.locator('#syslog-error');

    // On without a server
    await page.locator('#syslog-form .switch').click();
    await page.click('#btn-save-syslog');
    await expect(error).toBeVisible();
    await expect(error).toContainText('Server required to enable syslog');
    await expect(page.locator('#f-host')).toHaveClass(/invalid/);

    // Port out of range
    await page.fill('#syslog-host', '192.168.1.10');
    await expect(error).toBeHidden();
    await page.fill('#syslog-port', '70000');
    await page.click('#btn-save-syslog');
    await expect(error).toContainText('Invalid port');
    await expect(page.locator('#f-port')).toHaveClass(/invalid/);

    // Lines per second out of range
    await page.fill('#syslog-port', '514');
    await page.fill('#syslog-rate', '0');
    await page.click('#btn-save-syslog');
    await expect(error).toContainText('Invalid lines per second');
    await expect(page.locator('#f-rate')).toHaveClass(/invalid/);

    // Server with characters the firmware refuses
    await page.fill('#syslog-rate', '10');
    await page.fill('#syslog-host', 'udp://x');
    await page.click('#btn-save-syslog');
    await expect(error).toContainText('Invalid server');

    expect(mock.posts).toHaveLength(0);
  });

  test('shows the message of a 400 from the server', async ({ page }) => {
    mock.postStatus = 400;
    mock.postError = 'Invalid server';
    await openLogs(page);
    await page.locator('#syslog-form .switch').click();
    await page.fill('#syslog-host', 'rsyslog.lan');
    await page.click('#btn-save-syslog');

    const error = page.locator('#syslog-error');
    await expect(error).toBeVisible();
    await expect(page.locator('#syslog-error-text')).toHaveText('Invalid server');
    await expect(page.locator('#f-host')).toHaveClass(/invalid/);
    expect(mock.posts).toHaveLength(1);
    // The form keeps what the user typed
    await expect(page.locator('#syslog-host')).toHaveValue('rsyslog.lan');
  });

  test('selects are filled from the options of the GET', async ({ page }) => {
    await openLogs(page);
    await expect(page.locator('#syslog-format option')).toHaveText(['RFC 3164 (BSD)', 'RFC 5424']);
    await expect(page.locator('#syslog-facility option')).toHaveText(OPTIONS.facilities);
    await expect(page.locator('#syslog-level option')).toHaveText(['INFO and above', 'WARN and above', 'ERROR only']);
    const levels = await page.locator('#syslog-level option').evaluateAll(els => els.map(e => (e as HTMLOptionElement).value));
    expect(levels).toEqual(OPTIONS.levels);
  });

  test('the form is not overwritten by the refresh while it is being edited', async ({ page }) => {
    await page.clock.install();
    await openLogs(page);
    await expect(page.locator('#syslog-port')).toHaveValue('514');
    await page.fill('#syslog-host', 'typed.lan');
    // The device changes meanwhile: the 10 s refresh updates the state, not the form
    mock.syslog = { ...defaults(), enabled: true, host: '10.0.0.5' };
    mock.status = { ...mock.status, state: 'active', target: '10.0.0.5:514' };
    const before = mock.gets;
    await page.clock.runFor(10500);
    await expect.poll(() => mock.gets).toBeGreaterThan(before);
    await expect(page.locator('#syslog-chip-text')).toHaveText('Active → 10.0.0.5:514');
    await expect(page.locator('#syslog-host')).toHaveValue('typed.lan');
  });

  test('no refresh of the state with Auto-refresh off', async ({ page }) => {
    await page.clock.install();
    await openLogs(page);
    await expect.poll(() => mock.gets).toBe(1);
    await page.locator('.auto-refresh .switch').click();
    await page.clock.runFor(25000);
    expect(mock.gets).toBe(1);
  });

  test('a click on the indicator brings the panel into view', async ({ page }) => {
    await page.setViewportSize({ width: 1280, height: 600 });
    await openLogs(page);
    await expect(page.locator('#syslog-chip-text')).toHaveText('Off');
    await expect(page.locator('#btn-save-syslog')).not.toBeInViewport();
    await page.click('#syslog-chip');
    await expect(page.locator('#remote-log h2')).toBeInViewport();
    await expect(page.locator('#remote-log')).toBeFocused();
  });

  test('Refresh reads the state again', async ({ page }) => {
    await openLogs(page);
    await expect.poll(() => mock.gets).toBe(1);
    mock.syslog = { ...defaults(), enabled: true, host: '192.168.1.10' };
    mock.status = { ...mock.status, state: 'ready', target: '192.168.1.10:514' };
    await page.click('#btn-rlog-refresh');
    await expect(page.locator('#syslog-chip-text')).toHaveText('Ready → 192.168.1.10:514');
    await expect(page.locator('#syslog-host')).toHaveValue('192.168.1.10');
  });
});

test.describe('Remote logging (US-116): expired session', () => {
  test('a 401 on GET /api/logging goes to the login page', async ({ page }) => {
    await serveIndex(page);
    await serveLogin(page);
    await page.route('**/api/logging', route => route.fulfill({ status: 401, json: { error: 'unauthorized' } }));
    await page.goto('http://esp32.local/');
    await page.click('[data-target="logs"]');
    await expect(page).toHaveURL('http://esp32.local/login?expired=1');
  });
});

test.describe('Remote logging (US-116): phone', () => {
  test.use({ viewport: { width: 375, height: 812 } });

  test('panel and indicator cause no horizontal scroll', async ({ page }) => {
    await serveIndex(page);
    const mock = newMock();
    mock.syslog = { ...defaults(), enabled: true, host: 'a-very-long-syslog-server-name.example.lan', port: 65535 };
    mock.status = {
      state: 'error', target: 'a-very-long-syslog-server-name.example.lan (192.168.100.200):65535',
      error: 'Send to 192.168.100.200 failed', sent: 123456, failed: 7890, rate_limited: 12, not_ready: 3,
    };
    await routeLogging(page, mock);
    await page.goto('http://esp32.local/');
    await page.locator('[data-target="logs"]').dispatchEvent('click');
    await expect(page.locator('#syslog-chip-text')).toContainText('Error');
    await expect(page.locator('#remote-log')).toBeVisible();
    const sizes = await page.evaluate(() => ({
      scroll: document.documentElement.scrollWidth,
      client: document.documentElement.clientWidth,
    }));
    expect(sizes.scroll).toBeLessThanOrEqual(sizes.client);
  });
});
