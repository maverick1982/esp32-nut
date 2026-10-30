import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// Issue #69 — "Remote Logging" page: syslog settings (GET/POST /api/logging, applied
// without a restart) and the state of the syslog output. Serves the real data/www
// sources (support/webui.ts) and mocks the API with page.route.

interface LoggingJson {
  syslog: {
    enabled: boolean; host: string; port: number; protocol: string;
    facility: string; min_level: string; rate: number;
  };
  status: { syslog: Record<string, unknown> };
  options: Record<string, unknown>;
}

function loggingJson(): LoggingJson {
  return {
    syslog: { enabled: false, host: '', port: 514, protocol: 'rfc3164', facility: 'local0', min_level: 'INFO', rate: 10 },
    status: { syslog: { state: 'disabled', target: '', error: '', sent: 0, failed: 0, rate_limited: 0, not_ready: 0 } },
    options: {
      protocols: ['rfc3164', 'rfc5424'],
      facilities: ['user', 'daemon', 'local0', 'local1', 'local2', 'local3', 'local4', 'local5', 'local6', 'local7'],
      levels: ['INFO', 'WARN', 'ERROR'],
      rate_min: 1,
      rate_max: 100,
    },
  };
}

let data: LoggingJson;
let posts: Record<string, unknown>[];
let getHits: number;
let postStatus: number;
let postError: string;

async function openTab(page: Page) {
  await page.goto('http://esp32.local/');
  await page.locator('button.tab[data-target="remotelog"]').click();
  await expect(page.locator('#content-remotelog')).toHaveClass(/active/);
}

test.describe('Remote Logging page (issue #69)', () => {
  test.beforeEach(async ({ page }) => {
    data = loggingJson();
    posts = [];
    getHits = 0;
    postStatus = 200;
    postError = '';
    await serveIndex(page);
    await page.route('**/api/logging', async route => {
      const req = route.request();
      if (req.method() === 'GET') {
        getHits++;
        return route.fulfill({ json: data });
      }
      expect(req.headers()['content-type']).toContain('application/json');
      const body = req.postDataJSON();
      posts.push(body);
      if (postStatus !== 200) return route.fulfill({ status: postStatus, json: postError ? { error: postError } : {} });
      data.syslog = { ...data.syslog, ...(body.syslog as object) };
      data.status.syslog = { state: 'active', target: `${data.syslog.host}:${data.syslog.port}`, error: '', sent: 3, failed: 0, rate_limited: 0, not_ready: 0 };
      return route.fulfill({ json: { success: true } });
    });
  });

  test('shows the saved settings and the disabled state', async ({ page }) => {
    await openTab(page);
    await expect(page.locator('#rlog-state')).toHaveText('Disabled');
    await expect(page.locator('#rlog-enabled')).not.toBeChecked();
    await expect(page.locator('#rlog-port')).toHaveValue('514');
    await expect(page.locator('#rlog-protocol')).toHaveValue('rfc3164');
    await expect(page.locator('#rlog-facility option')).toHaveCount(10);
    await expect(page.locator('#rlog-facility')).toHaveValue('local0');
    await expect(page.locator('#rlog-level')).toHaveValue('INFO');
    await expect(page.locator('#rlog-rate')).toHaveValue('10');
    await expect(page.locator('#rlog-counters')).toHaveText('');
    expect(getHits).toBe(1);
  });

  test('saves the settings and shows the output state', async ({ page }) => {
    await openTab(page);
    await page.locator('#content-remotelog .switch').click();
    await page.fill('#rlog-host', ' rsyslog.lan ');
    await page.fill('#rlog-port', '1514');
    await page.selectOption('#rlog-protocol', 'rfc5424');
    await page.selectOption('#rlog-facility', 'daemon');
    await page.selectOption('#rlog-level', 'WARN');
    await page.fill('#rlog-rate', '25');
    await page.click('#btn-save-rlog');

    await expect(page.locator('#rlog-result')).toHaveText('Saved');
    expect(posts).toEqual([{
      syslog: { enabled: true, host: 'rsyslog.lan', port: 1514, rate: 25, protocol: 'rfc5424', facility: 'daemon', min_level: 'WARN' },
    }]);
    // The status is read again after the log task applied the settings
    await expect(page.locator('#rlog-state')).toHaveText('Sending');
    await expect(page.locator('#rlog-ind')).toHaveClass('status-indicator success');
    await expect(page.locator('#rlog-target')).toHaveText('rsyslog.lan:1514');
    await expect(page.locator('#rlog-counters')).toHaveText('3 sent');
  });

  test('enabling without a server is refused before any request', async ({ page }) => {
    await openTab(page);
    await page.locator('#content-remotelog .switch').click();
    await page.click('#btn-save-rlog');
    await expect(page.locator('#rlog-result')).toHaveText('Enter the syslog server to enable it');
    await expect(page.locator('#rlog-result')).toHaveClass(/error/);
    await page.fill('#rlog-host', 'bad host');
    await page.click('#btn-save-rlog');
    await expect(page.locator('#rlog-result')).toHaveText('Server: a host name or an IPv4 address');
    await page.fill('#rlog-host', '10.0.0.5');
    await page.fill('#rlog-port', '70000');
    await page.click('#btn-save-rlog');
    await expect(page.locator('#rlog-result')).toHaveText('Port: 1-65535');
    await page.fill('#rlog-port', '514');
    await page.fill('#rlog-rate', '0');
    await page.click('#btn-save-rlog');
    await expect(page.locator('#rlog-result')).toHaveText('Max lines per second: 1-100');
    expect(posts).toHaveLength(0);
  });

  test('a refused save shows the server message', async ({ page }) => {
    postStatus = 400;
    postError = 'Invalid server';
    await openTab(page);
    await page.fill('#rlog-host', '10.0.0.5');
    await page.click('#btn-save-rlog');
    await expect(page.locator('#rlog-result')).toHaveText('Invalid server');
    postError = '';
    postStatus = 500;
    await page.click('#btn-save-rlog');
    await expect(page.locator('#rlog-result')).toHaveText('Failed to save settings');
  });

  test('waiting and error states, counters and Refresh', async ({ page }) => {
    data.syslog = { ...data.syslog, enabled: true, host: 'rsyslog.lan' };
    data.status.syslog = { state: 'waiting', target: 'rsyslog.lan:514', error: 'Cannot resolve rsyslog.lan', sent: 0, failed: 0, rate_limited: 0, not_ready: 4 };
    await openTab(page);
    await expect(page.locator('#rlog-state')).toHaveText('Waiting');
    await expect(page.locator('#rlog-ind')).toHaveClass('status-indicator warning');
    await expect(page.locator('#rlog-problem')).toHaveText('Cannot resolve rsyslog.lan');
    await expect(page.locator('#rlog-counters')).toHaveText('0 sent · 4 dropped while offline');

    data.status.syslog = { state: 'error', target: 'rsyslog.lan (10.0.0.5):514', error: 'Send to 10.0.0.5 failed', sent: 10, failed: 2, rate_limited: 5, not_ready: 4 };
    await page.click('#btn-rlog-refresh');
    await expect(page.locator('#rlog-state')).toHaveText('Error');
    await expect(page.locator('#rlog-ind')).toHaveClass('status-indicator danger');
    await expect(page.locator('#rlog-counters')).toHaveText('10 sent · 5 rate-limited · 2 failed · 4 dropped while offline');
    // Refresh reads the status only: the form keeps what the user is typing
    expect(getHits).toBe(2);
  });

  test('ready: network up and server known, nothing sent yet', async ({ page }) => {
    data.syslog = { ...data.syslog, enabled: true, host: '10.0.0.5', min_level: 'ERROR' };
    data.status.syslog = { state: 'ready', target: '10.0.0.5:514', error: '', sent: 0, failed: 0, rate_limited: 0, not_ready: 0 };
    await openTab(page);
    await expect(page.locator('#rlog-state')).toHaveText('Ready');
    await expect(page.locator('#rlog-ind')).toHaveClass('status-indicator info');
    await expect(page.locator('#rlog-target')).toHaveText('10.0.0.5:514');
    await expect(page.locator('#rlog-counters')).toHaveText('0 sent');
    await expect(page.locator('#rlog-problem')).toBeHidden();
  });

  test('a failed read shows the error banner', async ({ page }) => {
    await page.route('**/api/logging', route => route.fulfill({ status: 500, body: '' }));
    await openTab(page);
    await expect(page.locator('#rlog-error')).toBeVisible();
  });

  test('mobile 375x812: no horizontal scroll', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await openTab(page);
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    expect(overflow).toBeLessThanOrEqual(0);
  });
});
