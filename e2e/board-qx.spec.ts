import { test, expect, Page } from '@playwright/test';
import { serveIndex } from './support/webui';

// US-138 — "Q* UPS driver" section of the Board page: protocol and novendor of the NUT
// nutdrv_qx driver (ups.conf), shown only while a Q* UPS is bound ("qx" in GET /api/board),
// saved with POST /api/board and applied without a restart. Serves the real data/www sources
// (support/webui.ts) and mocks the APIs with page.route.

interface QxJson {
  protocol: string;
  novendor: boolean;
  protocols: string[];
  detected: string;
}

interface BoardJson {
  id: string;
  label: string;
  led?: { present: boolean; on: boolean; level: number; levels: number };
  usb: { ports: { id: string; label: string }[]; port: string };
  auto_shutdown?: { on: boolean; available: boolean; reason?: string };
  qx?: QxJson;
}

interface Mock {
  board: BoardJson;
  postStatus: number;
  postError: string;
  posts: { contentType: string | undefined; body: Record<string, unknown> }[];
  getHits: number;
}

const PROTOCOLS = ['mustek', 'megatec/old', 'megatec', 'zinto', 'hunnox', 'innovatae', 'q1'];

function s3Board(qx?: QxJson): BoardJson {
  const b: BoardJson = {
    id: 'esp32s3_devkit',
    label: 'ESP32-S3 DevKitC-1',
    led: { present: true, on: true, level: 3, levels: 5 },
    usb: { ports: [{ id: 'otg', label: 'USB OTG' }], port: 'otg' },
    auto_shutdown: { on: false, available: true },
  };
  if (qx) b.qx = qx;
  return b;
}

function qxAutomatic(): QxJson {
  return { protocol: '', novendor: false, protocols: [...PROTOCOLS], detected: '' };
}

let mock: Mock;

async function openBoardTab(page: Page) {
  await page.locator('button.tab[data-target="board"]').click();
  await expect(page.locator('#content-board')).toHaveClass(/active/);
  await expect(page.locator('#board-name')).toHaveText(mock.board.label);
}

test.describe('Board page, Q* UPS driver (US-138)', () => {
  test.beforeEach(async ({ page }) => {
    mock = { board: s3Board(qxAutomatic()), postStatus: 200, postError: '', posts: [], getHits: 0 };

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
      if (mock.board.qx) {
        if (typeof body.qx_protocol === 'string') mock.board.qx.protocol = body.qx_protocol;
        if (typeof body.qx_novendor === 'boolean') mock.board.qx.novendor = body.qx_novendor;
      }
      return route.fulfill({ json: { success: true, restart: false } });
    });

    await page.goto('http://esp32.local/');
  });

  test('no Q* section with a HID UPS or without UPS', async ({ page }) => {
    // A HID UPS: no "qx" object
    mock.board = s3Board();
    await openBoardTab(page);
    await expect(page.locator('#qx-section')).toBeHidden();
    await expect(page.locator('#autosd-section')).toBeVisible();
    await expect(page.locator('#led-section')).toBeVisible();

    // No UPS at all: the same answer without "qx", the section stays hidden
    mock.board = s3Board();
    mock.board.auto_shutdown = { on: false, available: false, reason: 'no_ups' };
    await page.locator('button.tab[data-target="ups"]').click();
    await openBoardTab(page);
    expect(mock.getHits).toBe(2);
    await expect(page.locator('#qx-section')).toBeHidden();
    await expect(page.locator('#autosd-unavailable')).toBeVisible();
  });

  test('pick hunnox with novendor and save, without a restart', async ({ page }) => {
    await openBoardTab(page);
    const section = page.locator('#qx-section');
    await expect(section).toBeVisible();
    await expect(page.locator('#qx-detected')).toHaveText('Not detected yet');
    await expect(page.locator('#qx-protocol')).toHaveValue('');
    await expect(page.locator('#qx-protocol option')).toHaveText(['Automatic', ...PROTOCOLS]);
    await expect(page.locator('#qx-novendor')).not.toBeChecked();
    await expect(page.locator('#qx-novendor-state')).toHaveText('Off');
    await expect(page.locator('#btn-save-qx')).toBeDisabled();

    await page.locator('#qx-protocol').selectOption('hunnox');
    await expect(page.locator('#btn-save-qx')).toBeEnabled();
    // The switch is styled: click its slider, as a user does
    await page.locator('#qx-section label.switch').click();
    await expect(page.locator('#qx-novendor')).toBeChecked();
    await expect(page.locator('#qx-novendor-state')).toHaveText('On');

    await page.locator('#btn-save-qx').click();
    await expect(page.locator('#qx-result')).toContainText('detecting the protocol again');
    expect(mock.posts).toHaveLength(1);
    expect(mock.posts[0].contentType).toContain('application/json');
    expect(mock.posts[0].body).toEqual({ qx_protocol: 'hunnox', qx_novendor: true });
    // Saved values: nothing left to save, no restart countdown
    await expect(page.locator('#btn-save-qx')).toBeDisabled();
    await expect(page.locator('#board-success')).toBeHidden();
    await expect(page.locator('#qx-error')).toBeHidden();
  });

  test('saved values are loaded again when the tab is opened again', async ({ page }) => {
    mock.board = s3Board({ protocol: 'hunnox', novendor: true, protocols: [...PROTOCOLS], detected: 'Hunnox 0.03' });
    await openBoardTab(page);
    await expect(page.locator('#qx-protocol')).toHaveValue('hunnox');
    await expect(page.locator('#qx-novendor')).toBeChecked();
    await expect(page.locator('#qx-detected')).toHaveText('Hunnox 0.03');
    await expect(page.locator('#btn-save-qx')).toBeDisabled();

    // Back to automatic, saved, then the tab opened again: a new GET with the saved values
    await page.locator('#qx-protocol').selectOption('');
    await page.locator('#qx-section label.switch').click();
    await page.locator('#btn-save-qx').click();
    await expect(page.locator('#qx-result')).toContainText('Saved');
    expect(mock.posts[0].body).toEqual({ qx_protocol: '', qx_novendor: false });

    mock.board.qx!.detected = 'Megatec 0.10';
    await page.locator('button.tab[data-target="ups"]').click();
    await openBoardTab(page);
    expect(mock.getHits).toBe(2);
    await expect(page.locator('#qx-protocol')).toHaveValue('');
    await expect(page.locator('#qx-novendor')).not.toBeChecked();
    await expect(page.locator('#qx-detected')).toHaveText('Megatec 0.10');
    await expect(page.locator('#btn-save-qx')).toBeDisabled();
  });

  test('detected protocol appears without opening the tab again, unsaved changes stay', async ({ page }) => {
    await openBoardTab(page);
    await expect(page.locator('#qx-detected')).toHaveText('Not detected yet');
    expect(mock.getHits).toBe(1);
    // An unsaved change of the user
    await page.locator('#qx-protocol').selectOption('zinto');

    // The board recognises the protocol meanwhile: the next read (every 5 s) shows it
    mock.board.qx!.detected = 'Hunnox 0.03';
    await expect(page.locator('#qx-detected')).toHaveText('Hunnox 0.03', { timeout: 8000 });
    await expect(page.locator('#qx-protocol')).toHaveValue('zinto');
    await expect(page.locator('#btn-save-qx')).toBeEnabled();

    // Detected: no more reads
    const hits = mock.getHits;
    expect(hits).toBeGreaterThanOrEqual(2);
    await page.waitForTimeout(6000);
    expect(mock.getHits).toBe(hits);
    expect(mock.posts).toHaveLength(0);
  });

  test('no refresh once the Board tab is left', async ({ page }) => {
    await openBoardTab(page);
    await expect(page.locator('#qx-detected')).toHaveText('Not detected yet');
    await page.locator('button.tab[data-target="ups"]').click();
    await page.waitForTimeout(6000);
    expect(mock.getHits).toBe(1);
  });

  test('the Q* UPS unplugged while waiting: the section goes away', async ({ page }) => {
    await openBoardTab(page);
    await expect(page.locator('#qx-section')).toBeVisible();
    delete mock.board.qx;
    await expect(page.locator('#qx-section')).toBeHidden({ timeout: 8000 });
    await expect(page.locator('#autosd-section')).toBeVisible();
  });

  test('save error shows the message and restores the saved values', async ({ page }) => {
    mock.postStatus = 400;
    mock.postError = 'Invalid protocol';
    await openBoardTab(page);
    await page.locator('#qx-protocol').selectOption('zinto');
    await page.locator('#qx-section label.switch').click();
    await page.locator('#btn-save-qx').click();

    await expect(page.locator('#qx-error')).toBeVisible();
    await expect(page.locator('#qx-error-text')).toHaveText('Invalid protocol');
    await expect(page.locator('#qx-protocol')).toHaveValue('');
    await expect(page.locator('#qx-novendor')).not.toBeChecked();
    await expect(page.locator('#btn-save-qx')).toBeDisabled();
    expect(mock.posts).toHaveLength(1);
  });
});
