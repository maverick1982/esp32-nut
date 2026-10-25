import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-094 — "Board" page: status LED (on/off + 5 brightness levels, applied
// immediately) and the UPS USB port (boards with 2+ host ports, saved with a
// restart). Serves the real data/www sources (support/webui.ts) and mocks
// GET/POST /api/board with page.route.

interface BoardJson {
  id: string;
  label: string;
  led?: { present: boolean; on: boolean; level: number; levels: number };
  usb: { ports: { id: string; label: string }[]; port: string };
}

interface Mock {
  board: BoardJson;
  postStatus: number;
  postError: string;
  posts: { contentType: string | undefined; body: Record<string, unknown> }[];
  getHits: number;
}

function s3Board(): BoardJson {
  return {
    id: 'esp32s3_devkit',
    label: 'ESP32-S3 DevKitC-1',
    led: { present: true, on: true, level: 3, levels: 5 },
    usb: { ports: [{ id: 'otg', label: 'USB OTG' }], port: 'otg' },
  };
}

function dualBoard(withLed: boolean): BoardJson {
  const b: BoardJson = {
    id: 'esp32p4_dual_usb_test',
    label: 'ESP32-P4 dual USB (test profile)',
    usb: { ports: [{ id: 'hs', label: 'High-Speed' }, { id: 'fs', label: 'Full-Speed' }], port: 'hs' },
  };
  if (withLed) b.led = { present: true, on: true, level: 3, levels: 5 };
  return b;
}

let mock: Mock;

async function openBoardTab(page: Page) {
  await page.goto('http://esp32.local/');
  await page.locator('button.tab[data-target="board"]').click();
  await expect(page.locator('#content-board')).toHaveClass(/active/);
  await expect(page.locator('#board-name')).toHaveText(mock.board.label);
}

function levelButton(page: Page, n: number) {
  return page.locator(`#level-picker button[data-level="${n}"]`);
}

test.describe('Board page (US-094)', () => {
  test.beforeEach(async ({ page }) => {
    mock = { board: s3Board(), postStatus: 200, postError: '', posts: [], getHits: 0 };

    // Page, assets and API catch-all first: the mocks below take precedence
    await serveIndex(page);

    await page.route('**/api/config', route => route.fulfill({ json: {} }));
    await page.route('**/api/system-status', route => route.fulfill({
      json: { wifi: { status: 'Connected', ssid: 'lab', ip: '192.168.1.50' }, ups: { status: 'Connected' } },
    }));
    await page.route('**/api/ups-vars', route => route.fulfill({
      json: { 'ups.status': 'OL', 'battery.charge': '100' },
    }));
    await page.route('**/api/auth/status', route => route.fulfill({
      json: { configured: true, authenticated: true, ap_mode: false, username: 'admin' },
    }));
    await page.route('**/api/board', route => {
      const req = route.request();
      if (req.method() === 'GET') {
        mock.getHits++;
        return route.fulfill({ json: mock.board });
      }
      const body = req.postDataJSON() as Record<string, unknown>;
      mock.posts.push({ contentType: req.headers()['content-type'], body });
      if (mock.postStatus !== 200) {
        return route.fulfill({ status: mock.postStatus, json: { error: mock.postError } });
      }
      let restart = false;
      if (typeof body.led_on === 'boolean' && mock.board.led) mock.board.led.on = body.led_on;
      if (typeof body.led_level === 'number' && mock.board.led) mock.board.led.level = body.led_level;
      if (typeof body.usb_port === 'string' && body.usb_port !== mock.board.usb.port) {
        mock.board.usb.port = body.usb_port;
        restart = true;
      }
      return route.fulfill({ json: { success: true, restart } });
    });
  });

  test('menu entry between NUT Server and Web Login, one GET per opening', async ({ page }) => {
    await page.goto('http://esp32.local/');
    const tab = page.locator('button.tab[data-target="board"]');
    await expect(tab).toHaveText('Board Settings');
    const targets = await page.locator('nav.tabs button.tab').evaluateAll(
      els => els.map(e => e.getAttribute('data-target')));
    expect(targets.indexOf('board')).toBe(targets.indexOf('nut') + 1);
    expect(targets.indexOf('weblogin')).toBe(targets.indexOf('board') + 1);
    expect(mock.getHits).toBe(0);

    await tab.click();
    await expect(page.locator('.page-title')).toHaveText('Board Settings');
    await expect(page.locator('#board-name')).toHaveText('ESP32-S3 DevKitC-1');
    await expect(page.locator('#board-profile')).toHaveAttribute('title', /esp32s3_devkit/);
    expect(mock.getHits).toBe(1);
  });

  test('S3: LED on at level 3, no USB section; level 5 and LED off are applied immediately', async ({ page }) => {
    await openBoardTab(page);

    await expect(page.locator('#led-section')).toBeVisible();
    await expect(page.locator('#usb-section')).toBeHidden();
    await expect(page.locator('#led-on')).toBeChecked();
    await expect(page.locator('#level-picker button[data-level]')).toHaveCount(5);
    await expect(levelButton(page, 3)).toHaveAttribute('aria-checked', 'true');
    await expect(page.locator('#level-readout')).toHaveText('Level 3');
    // No Save button for the LED
    await expect(page.locator('#led-section button[type="submit"]')).toHaveCount(0);

    await levelButton(page, 5).click();
    await expect(page.locator('#led-result')).toContainText('Applied');
    await expect(page.locator('#led-result')).toContainText('level 3 →');
    await expect(page.locator('#led-result')).toContainText('5');
    expect(mock.posts).toHaveLength(1);
    expect(mock.posts[0].contentType).toContain('application/json');
    expect(mock.posts[0].body).toEqual({ led_level: 5 });
    await expect(levelButton(page, 5)).toHaveAttribute('aria-checked', 'true');
    await expect(levelButton(page, 3)).toHaveAttribute('aria-checked', 'false');
    await expect(page.locator('#level-readout')).toHaveText('Level 5');
    // Only the level number, never the 0-255 value
    await expect(page.locator('#led-section')).not.toContainText('255');

    // The result fades after ~4 s
    await expect(page.locator('#led-result')).toBeEmpty({ timeout: 8000 });

    await page.locator('#led-section label.switch').click();
    await expect(page.locator('#led-result')).toContainText('LED off');
    expect(mock.posts).toHaveLength(2);
    expect(mock.posts[1].body).toEqual({ led_on: false });
    await expect(page.locator('#led-on')).not.toBeChecked();
    for (let n = 1; n <= 5; n++) await expect(levelButton(page, n)).toBeDisabled();
    await expect(page.locator('#row-brightness')).toHaveClass(/is-disabled/);
    await expect(page.locator('#level-readout')).toHaveText('LED off · level 5 kept');
  });

  test('two USB ports: choose Full-Speed, Save & restart posts the port and shows the restart', async ({ page }) => {
    mock.board = dualBoard(true);
    await openBoardTab(page);

    const usb = page.locator('#usb-section');
    await expect(usb).toBeVisible();
    await expect(usb).toContainText('Changing the port restarts the board');
    await expect(page.locator('#usb-port input[name="usb-port"]')).toHaveCount(2);
    await expect(page.locator('#usb-port input[value="hs"]')).toBeChecked();
    await expect(page.locator('#btn-save-usb')).toBeDisabled();

    await page.locator('#usb-port .port-card', { hasText: 'Full-Speed' }).click();
    await expect(page.locator('#usb-port input[value="fs"]')).toBeChecked();
    await expect(page.locator('#btn-save-usb')).toBeEnabled();

    // Back to the saved port: nothing to save
    await page.locator('#usb-port .port-card', { hasText: 'High-Speed' }).click();
    await expect(page.locator('#btn-save-usb')).toBeDisabled();
    await page.locator('#usb-port .port-card', { hasText: 'Full-Speed' }).click();

    await page.locator('#btn-save-usb').click();
    await expect(page.locator('#board-success')).toBeVisible();
    await expect(page.locator('#board-success')).toContainText('The board is restarting');
    expect(mock.posts).toHaveLength(1);
    expect(mock.posts[0].body).toEqual({ usb_port: 'fs' });
    await expect(page.locator('#btn-save-usb')).toBeDisabled();
    await expect(page.locator('#btn-save-usb')).toContainText('Restarting');
  });

  test('board without status LED: no LED section, USB section only', async ({ page }) => {
    mock.board = dualBoard(false);
    await openBoardTab(page);

    await expect(page.locator('#usb-section')).toBeVisible();
    await expect(page.locator('#led-section')).not.toBeVisible();
  });

  test('POST rejected with 400: error shown, control back to the previous value', async ({ page }) => {
    await openBoardTab(page);
    mock.postStatus = 400;
    mock.postError = 'Invalid LED level';

    await levelButton(page, 4).click();
    await expect(page.locator('#led-error')).toBeVisible();
    await expect(page.locator('#led-error')).toContainText('Invalid LED level');
    await expect(page.locator('#level-picker')).toHaveClass(/invalid/);
    expect(mock.posts[0].body).toEqual({ led_level: 4 });
    await expect(levelButton(page, 3)).toHaveAttribute('aria-checked', 'true');
    await expect(levelButton(page, 4)).toHaveAttribute('aria-checked', 'false');
    await expect(page.locator('#level-readout')).toHaveText('Level 3');
    await expect(page.locator('#led-result')).toBeEmpty();

    // The switch goes back to "on" as well
    await page.locator('#led-section label.switch').click();
    await expect.poll(() => mock.posts.length).toBe(2);
    await expect(page.locator('#led-error')).toContainText('Invalid LED level');
    expect(mock.posts[1].body).toEqual({ led_on: false });
    await expect(page.locator('#led-on')).toBeChecked();
    await expect(levelButton(page, 2)).toBeEnabled();
  });

  test('375 px: no horizontal scroll with LED and two USB ports', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    mock.board = dualBoard(true);
    await openBoardTab(page);
    await expect(page.locator('#usb-section')).toBeVisible();

    const overflow = await page.evaluate(() => {
      const doc = document.documentElement;
      const area = document.querySelector('.content-area')!;
      return { page: doc.scrollWidth - doc.clientWidth, area: area.scrollWidth - area.clientWidth };
    });
    expect(overflow.page, 'page horizontal overflow').toBeLessThanOrEqual(0);
    expect(overflow.area, '.content-area horizontal overflow').toBeLessThanOrEqual(0);

    const box = (await page.locator('#btn-save-usb').boundingBox())!;
    expect(box.x + box.width).toBeLessThanOrEqual(375);
  });
});
