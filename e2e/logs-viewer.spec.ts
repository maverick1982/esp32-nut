import { test, expect, type Page } from '@playwright/test';
import { serveIndex } from './support/webui';

test.describe('System Logs View', () => {
  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('should display mocked system logs in the terminal', async ({ page }) => {
    // Intercept the API call to /api/logs
    await page.route('**/api/logs', async (route) => {
      if (route.request().method() === 'GET') {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify([
            { id: 1, time: 1000, level: 'WARN', msg: 'Mocked warning message' }
          ])
        });
      } else {
        await route.continue();
      }
    });

    // 1. Navigate to /
    await page.goto('http://esp32.local/');

    // 2. Click on the "System Logs" tab
    await page.click('[data-target="logs"]');

    // 3. Wait for the terminal to update
    const terminalOutput = page.locator('#terminal-output');
    
    // Wait for the specific message to appear in the terminal
    await expect(terminalOutput).toContainText('Mocked warning message');

    // Verify the correct class is applied to the log line (e.g., span containing the log)
    const logLine = page.locator('.terminal-line', { hasText: 'Mocked warning message' });
    await expect(logLine.locator('.terminal-level-warn')).toBeVisible();
  });

  test('should pause and resume auto-refresh', async ({ page }) => {
    // 1. Navigate to /
    await page.goto('http://esp32.local/');

    // 2. Click on the "System Logs" tab
    await page.click('[data-target="logs"]');

    const toggleAutoRefresh = page.locator('#toggle-auto-refresh');
    const terminalOutput = page.locator('#terminal-output');

    // Assicurati che il toggle (label) sia visibile e l'input checkato di default
    const switchLabel = page.locator('.auto-refresh .switch');
    await expect(switchLabel).toBeVisible();
    await expect(toggleAutoRefresh).toBeChecked();

    // Clicca il toggle per spegnerlo (pause)
    await switchLabel.click();
    
    // Verifica che appaia il messaggio "Auto-refresh disabled"
    await expect(terminalOutput).toContainText('Auto-refresh disabled');

    // Cliccalo di nuovo per riaccenderlo (resume)
    await switchLabel.click();

    // Verifica che appaia "Auto-refresh enabled"
    await expect(terminalOutput).toContainText('Auto-refresh enabled');
  });

  test('should trigger USB diagnostics download', async ({ page }) => {
    await page.route('**/api/usb/dump', async route => {
      await route.fulfill({
        status: 200,
        headers: {
          'Content-Type': 'application/json',
          'Content-Disposition': 'attachment; filename="usb_diagnostics.json"'
        },
        body: JSON.stringify({ driver: "Generic", vid: "0x1234", pid: "0x5678" })
      });
    });

    await page.goto('http://esp32.local/');
    await page.click('[data-target="logs"]');

    const btnExportUsb = page.locator('#link-export-usb');
    await expect(btnExportUsb).toBeVisible();

    const requestPromise = page.waitForRequest(request => request.url().includes('/api/usb/dump'));
    await btnExportUsb.click();
    await requestPromise;

    const terminalOutput = page.locator('#terminal-output');
    await expect(terminalOutput).toContainText('Downloading USB diagnostics...');
  });
});

// US-118: board time ("board_time", time zone of Board Settings) in the terminal and in
// the export; without it the terminal rebuilds the time with the browser clock (issue #60)
type LogRow = { id: number; time: number; level: string; msg: string; board_time?: string };
type LogsReply = { uptime: number; rows: LogRow[] };

// GET /api/logs answers the replies in order, then repeats the last one
async function mockLogs(page: Page, replies: LogsReply[]): Promise<{ calls: () => number }> {
  let calls = 0;
  await page.route('**/api/logs', async (route) => {
    const reply = replies[Math.min(calls, replies.length - 1)];
    calls++;
    await route.fulfill({
      status: 200,
      contentType: 'application/json',
      headers: { 'X-Uptime-Ms': String(reply.uptime) },
      body: JSON.stringify(reply.rows),
    });
  });
  return { calls: () => calls };
}

async function openLogs(page: Page) {
  await page.goto('http://esp32.local/');
  await page.click('[data-target="logs"]');
}

function timeOfLine(page: Page, msg: string) {
  return page.locator('.terminal-line', { hasText: msg }).locator('.terminal-time');
}

test.describe('System Logs board time (US-118)', () => {
  // A browser far from the board's time zone: its clock would give another hour
  test.use({ timezoneId: 'America/New_York' });

  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('the terminal shows the board time, not the browser time', async ({ page }) => {
    await page.clock.setFixedTime(new Date('2026-11-05T08:12:10Z'));
    await mockLogs(page, [{
      uptime: 60000,
      rows: [
        { id: 1, time: 1000, level: 'INFO', msg: 'Boot line', board_time: '2026-11-05T09:11:13.000+01:00' },
        { id: 2, time: 53000, level: 'WARN', msg: 'UPS on battery', board_time: '2026-11-05T09:12:03.007+01:00' },
      ],
    }]);

    await openLogs(page);

    await expect(timeOfLine(page, 'UPS on battery')).toHaveText('[09:12:03]');
    // Written before the synchronization: its time comes from the uptime, on the board
    await expect(timeOfLine(page, 'Boot line')).toHaveText('[09:11:13]');
    await expect(page.locator('.terminal-line', { hasText: 'UPS on battery' }).locator('.terminal-level-warn')).toBeVisible();
  });

  test('without the board time the terminal uses the browser clock as before', async ({ page }) => {
    // 03:12:10 in New York; the line was written 6 s before the list
    await page.clock.setFixedTime(new Date('2026-11-05T08:12:10Z'));
    await mockLogs(page, [{
      uptime: 10000,
      rows: [{ id: 1, time: 4000, level: 'INFO', msg: 'No NTP yet' }],
    }]);

    await openLogs(page);

    await expect(timeOfLine(page, 'No NTP yet')).toHaveText('[03:12:04]');
  });

  test('a line that comes after the synchronization gets the board time', async ({ page }) => {
    await page.clock.setFixedTime(new Date('2026-11-05T08:12:10Z'));
    const first: LogRow = { id: 1, time: 4000, level: 'INFO', msg: 'Before sync' };
    const logs = await mockLogs(page, [
      { uptime: 10000, rows: [first] },
      {
        uptime: 12000,
        rows: [
          { ...first, board_time: '2026-11-05T09:12:04.000+01:00' },
          { id: 2, time: 11500, level: 'INFO', msg: '[TIME] Synchronized', board_time: '2026-11-05T09:12:11.500+01:00' },
        ],
      },
    ]);

    await openLogs(page);
    await expect(timeOfLine(page, 'Before sync')).toHaveText('[03:12:04]');
    await expect.poll(() => logs.calls()).toBeGreaterThan(0);
    await page.click('#btn-refresh');

    await expect(timeOfLine(page, '[TIME] Synchronized')).toHaveText('[09:12:11]');
    // Shown once, by id: the line already in the terminal is not drawn again
    await expect(page.locator('.terminal-line', { hasText: 'Before sync' })).toHaveCount(1);
    await expect(timeOfLine(page, 'Before sync')).toHaveText('[03:12:04]');
  });

  test('after a board restart the new lines are shown, with or without the board time', async ({ page }) => {
    await page.clock.setFixedTime(new Date('2026-11-05T08:12:10Z'));
    await mockLogs(page, [
      {
        uptime: 50000,
        rows: [
          { id: 1, time: 1000, level: 'INFO', msg: 'Old boot', board_time: '2026-11-05T09:11:21.000+01:00' },
          { id: 2, time: 40000, level: 'INFO', msg: 'Old line', board_time: '2026-11-05T09:12:00.000+01:00' },
        ],
      },
      {
        uptime: 3000,
        rows: [
          { id: 1, time: 500, level: 'INFO', msg: 'New boot' },
        ],
      },
    ]);

    await openLogs(page);
    await expect(timeOfLine(page, 'Old line')).toHaveText('[09:12:00]');
    await page.click('#btn-refresh');

    await expect(page.locator('#terminal-output')).toContainText('Device restarted');
    // No board time yet after the restart: browser clock, 2.5 s before the list
    await expect(timeOfLine(page, 'New boot')).toHaveText('[03:12:07]');
  });

  test('Export downloads the lines with the board time and the fields of today', async ({ page }) => {
    const rows: LogRow[] = [
      { id: 1, time: 1000, level: 'INFO', msg: 'Boot line', board_time: '2026-11-05T09:11:13.000+01:00' },
      { id: 2, time: 53000, level: 'WARN', msg: 'UPS on battery', board_time: '2026-11-05T09:12:03.007+01:00' },
    ];
    await mockLogs(page, [{ uptime: 60000, rows }]);

    await openLogs(page);
    await expect(timeOfLine(page, 'UPS on battery')).toHaveText('[09:12:03]');

    const exported = page.waitForResponse(r => r.url().endsWith('/api/logs') && r.request().isNavigationRequest());
    await page.click('#btn-download');
    const response = await exported;
    const lines = await response.json();

    expect(lines).toHaveLength(2);
    for (const line of lines) {
      expect(Object.keys(line).sort()).toEqual(['board_time', 'id', 'level', 'msg', 'time']);
    }
    expect(lines[1]).toEqual(rows[1]);
    await expect(page).toHaveURL(/\/api\/logs$/);
  });
});
