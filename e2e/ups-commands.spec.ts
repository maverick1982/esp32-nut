import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-059 — "UPS Commands" page. Serves the real data/www sources (support/webui.ts)
// and mocks every API the page calls with page.route.

type Vars = Record<string, unknown>;

interface Mock {
  vars: Vars;
  catalog: { connected: boolean; commands: { name: string; description: string; destructive: boolean }[] };
  commandStatus: number;
  posts: unknown[];
  upsVarsHits: number;
}

const ERROR_BODIES: Record<number, string> = {
  400: 'Command not supported',
  403: 'Available via NUT only',
  500: 'Command rejected by the UPS',
  503: 'UPS not connected',
};

function defaultMock(): Mock {
  return {
    vars: {
      'ups.status': 'OL',
      'battery.charge': '100',
      'ups.load': '15',
      'ups.realpower': '120',
      'ups.mfr': 'Eaton',
      'ups.beeper.status': 'enabled',
      'ups.beeper.switchable': true,
    },
    catalog: {
      connected: true,
      commands: [
        { name: 'beeper.enable', description: 'Enable the UPS beeper', destructive: false },
        { name: 'test.battery.start.quick', description: 'Start a quick battery test', destructive: false },
        { name: 'shutdown.return', description: 'Turn off the load and return when power is back', destructive: true },
      ],
    },
    commandStatus: 200,
    posts: [],
    upsVarsHits: 0,
  };
}

async function openCommandsTab(page: Page) {
  await page.goto('http://esp32.local/');
  await page.locator('button.tab[data-target="commands"]').click();
  await expect(page.locator('#content-commands')).toHaveClass(/active/);
}

let mock: Mock;

test.describe('UPS Commands page (US-059)', () => {
  test.beforeEach(async ({ page }) => {
    mock = defaultMock();

    // Page, assets and API catch-all first: the mocks below take precedence
    await serveIndex(page);

    await page.route('**/api/config', route => route.fulfill({ json: {} }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'Connected', ssid: 'lab', ip: '192.168.1.50' }, ups: { status: 'Connected' } },
    }));
    await page.route('**/api/ups-vars', route => {
      mock.upsVarsHits++;
      return route.fulfill({ json: mock.vars });
    });
    await page.route('**/api/ups/commands', route => route.fulfill({ json: mock.catalog }));
    await page.route('**/api/ups/command', async route => {
      const req = route.request();
      mock.posts.push({
        method: req.method(),
        contentType: req.headers()['content-type'],
        body: req.postDataJSON(),
      });
      const status = mock.commandStatus;
      if (status === 200) {
        return route.fulfill({ status, json: { success: true } });
      }
      return route.fulfill({ status, json: { error: ERROR_BODIES[status] ?? 'Error' } });
    });
  });

  test('navigation: second tab, page title and the 4 cards follow the polling', async ({ page }) => {
    await page.goto('http://esp32.local/');

    const secondTab = page.locator('nav.tabs button.tab').nth(1);
    await expect(secondTab).toHaveAttribute('data-target', 'commands');
    await expect(secondTab).toHaveText('UPS Commands');

    await secondTab.click();
    await expect(page.locator('.page-title')).toHaveText('UPS Commands');
    await expect(page.locator('#content-commands')).toBeVisible();

    await expect(page.locator('#cmd-ups-status')).toHaveText('OL');
    await expect(page.locator('#cmd-ups-charge')).toHaveText('100');
    await expect(page.locator('#cmd-bar-charge')).toHaveAttribute('style', /width:\s*100%/);
    await expect(page.locator('#cmd-ups-load')).toHaveText('15');
    await expect(page.locator('#cmd-bar-load')).toHaveAttribute('style', /width:\s*15%/);
    await expect(page.locator('#cmd-ups-realpower')).toHaveText('120');

    // Catalog rendered in the two groups
    await expect(page.locator('#cmd-group-actions .cmd-row')).toHaveCount(2);
    await expect(page.locator('#cmd-group-power .cmd-row')).toHaveCount(1);
    await expect(page.locator('#cmd-count-actions')).toHaveText('2');
    await expect(page.locator('#cmd-count-power')).toHaveText('1');
    await expect(page.locator('#cmd-empty')).toBeHidden();

    // Next polling cycle (2 s) brings the new values
    mock.vars = { ...mock.vars, 'ups.status': 'OB', 'battery.charge': '87', 'ups.load': '42', 'ups.realpower': '310' };
    await expect(page.locator('#cmd-ups-status')).toHaveText('OB', { timeout: 5000 });
    await expect(page.locator('#cmd-ups-charge')).toHaveText('87');
    await expect(page.locator('#cmd-bar-charge')).toHaveAttribute('style', /width:\s*87%/);
    await expect(page.locator('#cmd-ups-load')).toHaveText('42');
    await expect(page.locator('#cmd-ups-realpower')).toHaveText('310');
  });

  test('battery test: Run sends the POST, shows "Command sent" and the test result box', async ({ page }) => {
    // The UPS starts the test: the next poll reports it in progress
    let releasePost!: () => void;
    const postGate = new Promise<void>(r => { releasePost = r; });
    await page.route('**/api/ups/command', async route => {
      const req = route.request();
      mock.posts.push({ method: req.method(), contentType: req.headers()['content-type'], body: req.postDataJSON() });
      await postGate;
      mock.vars = { ...mock.vars, 'ups.test.result': 'In progress' };
      await route.fulfill({ status: 200, json: { success: true } });
    });

    await openCommandsTab(page);
    await expect(page.locator('#cmd-test-result')).toBeHidden();

    const row = page.locator('.cmd-row[data-cmd="test.battery.start.quick"]');
    const btn = row.locator('button.cmd-run');
    await expect(row.locator('.cmd-desc')).toHaveText('Start a quick battery test');
    await expect(btn).toBeEnabled();

    await btn.click();
    await expect(btn).toBeDisabled();
    await expect.poll(() => mock.posts.length).toBe(1);
    releasePost();

    expect(mock.posts[0]).toEqual({
      method: 'POST',
      contentType: expect.stringContaining('application/json'),
      body: { name: 'test.battery.start.quick' },
    });

    await expect(row.locator('.cmd-result')).toHaveText('Command sent');
    await expect(btn).toBeDisabled();

    await expect(page.locator('#cmd-test-result')).toBeVisible({ timeout: 5000 });
    await expect(page.locator('#cmd-test-result-value')).toHaveText('In progress');
    await expect(page.locator('#cmd-test-result')).toHaveClass(/running/);

    // The message goes away and the button comes back after ~4 s
    await expect(btn).toBeEnabled({ timeout: 7000 });
    await expect(row.locator('.cmd-result')).toHaveText('', { timeout: 3000 });
  });

  test('rejected command: 500 shows "Command rejected by the UPS" and the button comes back', async ({ page }) => {
    mock.commandStatus = 500;
    await openCommandsTab(page);

    const row = page.locator('.cmd-row[data-cmd="beeper.enable"]');
    const btn = row.locator('button.cmd-run');
    await btn.click();

    await expect(row.locator('.cmd-result')).toHaveText('Command rejected by the UPS');
    expect(mock.posts).toHaveLength(1);
    expect((mock.posts[0] as { body: unknown }).body).toEqual({ name: 'beeper.enable' });

    await expect(btn).toBeEnabled({ timeout: 7000 });
  });

  test('outcome message does not move the command name and description', async ({ page }) => {
    // The longest message ("Command rejected by the UPS") must fit the reserved space
    mock.commandStatus = 500;
    await page.setViewportSize({ width: 1280, height: 800 });
    await openCommandsTab(page);

    const row = page.locator('.cmd-row[data-cmd="beeper.enable"]');
    // Positions relative to the row: the click may scroll the page
    const layout = () => row.evaluate(r => {
      const rb = r.getBoundingClientRect();
      const rel = (sel: string) => {
        const b = r.querySelector(sel)!.getBoundingClientRect();
        return { x: b.left - rb.left, y: b.top - rb.top, width: b.width, height: b.height };
      };
      return { name: rel('.cmd-name'), desc: rel('.cmd-desc') };
    });
    const before = await layout();

    await row.locator('button.cmd-run').click();
    await expect(row.locator('.cmd-result')).toHaveText('Command rejected by the UPS');
    expect(await layout()).toEqual(before);

    await expect(row.locator('.cmd-result')).toHaveText('', { timeout: 7000 });
    expect(await layout()).toEqual(before);
  });

  test('destructive commands: listed under Power control, locked, no POST', async ({ page }) => {
    await openCommandsTab(page);

    const row = page.locator('#cmd-group-power .cmd-row[data-cmd="shutdown.return"]');
    await expect(row).toBeVisible();
    await expect(page.locator('#cmd-group-power')).toHaveClass(/locked/);
    await expect(page.locator('#cmd-group-actions .cmd-row[data-cmd="shutdown.return"]')).toHaveCount(0);

    const btn = row.locator('button.cmd-run');
    await expect(btn).toBeDisabled();
    await expect(btn).toHaveAttribute('title', 'Available via NUT only until web authentication is implemented');

    // A forced click on a disabled button must not send anything
    await btn.click({ force: true });
    await btn.dispatchEvent('click');
    // Let a full polling cycle pass, then make sure nothing was posted
    const hits = mock.upsVarsHits;
    await expect.poll(() => mock.upsVarsHits, { timeout: 5000 }).toBeGreaterThan(hits);
    expect(mock.posts).toHaveLength(0);
    await expect(row.locator('.cmd-result')).toHaveText('');
  });

  test('empty state: UPS disconnected shows "No commands available" and hides the test result', async ({ page }) => {
    mock.catalog = { connected: false, commands: [] };
    mock.vars = { _disconnected: true };

    await openCommandsTab(page);

    await expect(page.locator('#cmd-empty')).toBeVisible();
    await expect(page.locator('#cmd-empty')).toContainText('No commands available');
    await expect(page.locator('#cmd-list')).toBeHidden();
    await expect(page.locator('.cmd-row')).toHaveCount(0);
    await expect(page.locator('#cmd-test-result')).toBeHidden();
  });

  test('telemetry unchanged: beeper toggle visible and enabled with ups.beeper.switchable', async ({ page }) => {
    await openCommandsTab(page);
    await expect(page.locator('#cmd-ups-status')).toHaveText('OL');

    await page.locator('button.tab[data-target="ups"]').click();
    await expect(page.locator('.page-title')).toHaveText('UPS Telemetry');
    await expect(page.locator('#content-ups')).toBeVisible();

    await expect(page.locator('.ups-actions')).toBeVisible({ timeout: 5000 });
    // The checkbox is visually replaced by the .switch slider
    await expect(page.locator('.ups-actions .switch')).toBeVisible();
    await expect(page.locator('#toggle-beeper')).toBeEnabled();
    await expect(page.locator('#toggle-beeper')).toBeChecked();
    await expect(page.locator('#beeper-state')).toHaveText('Enabled');
  });

  test('mobile 375x812: no horizontal scroll', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await openCommandsTab(page);
    await expect(page.locator('.cmd-row')).toHaveCount(3);
    await expect(page.locator('#cmd-ups-status')).toHaveText('OL');

    const overflow = await page.evaluate(() => ({
      scrollWidth: document.documentElement.scrollWidth,
      innerWidth: window.innerWidth,
    }));
    expect(overflow.scrollWidth).toBeLessThanOrEqual(overflow.innerWidth);
  });
});
