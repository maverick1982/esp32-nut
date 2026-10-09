import { Page, Route } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';

// Serves the Web UI to the e2e tests the way the firmware serves it, so the
// specs never need to know the asset file names in data/www.
//
// Playwright route precedence: when several routes match a request, the one
// registered LAST wins. These helpers must therefore be called at the start of
// a test (or beforeEach); any page.route() a test registers afterwards (API
// mocks, POST /update handlers, ...) takes precedence over what is set here.

const WWW = path.resolve(__dirname, '..', '..', 'data', 'www');

function www(file: string): string {
  return path.join(WWW, file);
}

function fulfillFile(route: Route, file: string, contentType: string) {
  const full = www(file);
  if (!fs.existsSync(full)) {
    return route.fulfill({ status: 200, contentType, body: '' });
  }
  return route.fulfill({ path: full, contentType });
}

/**
 * Serves the main page (http://esp32.local/) with its real assets.
 *
 * - Catch-all on `**\/api/**`, registered first so that every test-specific
 *   API mock registered later overrides it. Without it, API calls the test does
 *   not mock would go out to the real `esp32.local`: the slow DNS failure keeps
 *   the shared `isFetchingAPI` flag of bundle.js busy and delays the polling the
 *   tests are waiting for. Unmocked GETs get `{}`; any other unmocked method
 *   gets 501, so a save whose endpoint or method changed makes its test fail
 *   instead of showing a success message.
 * - `http://esp32.local/` -> data/www/index.html
 * - `bundle.css` / `bundle.js` (any `?v=` query) -> data/www
 * - `logo.png` / `favicon.ico` -> real files (empty body if missing)
 */
export async function serveIndex(page: Page): Promise<void> {
  await page.route('**/api/**', route => {
    const req = route.request();
    if (req.method() === 'GET') return route.fulfill({ json: {} });
    return route.fulfill({ status: 501, json: { error: `unmocked ${req.method()} ${req.url()}` } });
  });

  await page.route('http://esp32.local/', route =>
    fulfillFile(route, 'index.html', 'text/html'));
  await page.route('**/bundle.css*', route =>
    fulfillFile(route, 'bundle.css', 'text/css'));
  await page.route('**/bundle.js*', route =>
    fulfillFile(route, 'bundle.js', 'application/javascript'));
  await page.route('**/logo.png*', route =>
    fulfillFile(route, 'logo.png', 'image/png'));
  await page.route('**/favicon.ico*', route =>
    fulfillFile(route, 'favicon.ico', 'image/x-icon'));
}

/**
 * Serves the OTA page as the firmware does (update_inlined.html, produced by
 * scripts/inline_update.py from data/www/update.html):
 *
 * - GET `http://esp32.local/update` and `/update.html` -> data/www/update.html.
 *   Other methods fall through (`route.fallback()`), so POST /update stays to
 *   the single tests.
 * - `shared.css?v=…` placeholder -> bundle.css + shared_ota.css concatenated,
 *   exactly like inline_update.py does.
 * - `mobile.css?v=…` placeholder -> empty CSS (inline_update.py drops it).
 * - `fflate.min.js?v=…` -> data/www/fflate.min.js (the glob accepts the query).
 */
export async function serveUpdatePage(page: Page): Promise<void> {
  const serveUpdateHtml = (route: Route) => {
    if (route.request().method() === 'GET') {
      return fulfillFile(route, 'update.html', 'text/html');
    }
    return route.fallback();
  };
  await page.route('http://esp32.local/update', serveUpdateHtml);
  await page.route('http://esp32.local/update.html', serveUpdateHtml);

  await page.route('**/shared.css*', route => {
    const css = ['bundle.css', 'shared_ota.css']
      .map(f => (fs.existsSync(www(f)) ? fs.readFileSync(www(f), 'utf-8') + '\n' : ''))
      .join('');
    return route.fulfill({ status: 200, contentType: 'text/css', body: css });
  });
  await page.route('**/mobile.css*', route =>
    route.fulfill({ status: 200, contentType: 'text/css', body: '' }));
  await page.route('**/fflate.min.js*', route =>
    fulfillFile(route, 'fflate.min.js', 'application/javascript'));
}
