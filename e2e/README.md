# E2E tests (Playwright)

End-to-end tests of the Web UI in `data/www`, run in Chromium against the real
sources with every API mocked via `page.route`. Config: `playwright.config.ts`.

## Running

```bash
npm run e2e                              # whole suite (= npx playwright test)
npx playwright test e2e/<file>           # a single spec
npx playwright test --repeat-each 3      # stability check
```

## Screenshots for the docs

`screenshot.spec.ts` regenerates `docs/images/ui-*.png` and **overwrites them**.
It is excluded from the normal suite (`testIgnore` in `playwright.config.ts`)
and only runs when `SCREENSHOTS=1`:

```bash
npm run e2e:screenshots                  # any shell, Windows included
SCREENSHOTS=1 npx playwright test e2e/screenshot.spec.ts           # Git Bash
$env:SCREENSHOTS=1; npx playwright test e2e/screenshot.spec.ts; Remove-Item Env:SCREENSHOTS   # PowerShell
```

Check the result with `git diff docs/images`; discard it with
`git checkout -- docs/images`.

## Serving the Web UI: `support/webui.ts`

Specs do not route asset files by name. Call the helper at the start of the
test (or `beforeEach`), then register the test's own API mocks:

- `serveIndex(page)`: `http://esp32.local/` with `bundle.css`, `bundle.js`,
  logo and favicon from `data/www`, plus a catch-all `**/api/**` so no request
  ever reaches a real `esp32.local`: unmocked GETs get `{}`, any other unmocked
  method gets 501 (a save must be mocked by its test, or the test fails).
- `serveUpdatePage(page)`: `/update` as the firmware serves it
  (`update_inlined.html`, see `scripts/inline_update.py`): `shared.css` ->
  `bundle.css` + `shared_ota.css`, `mobile.css` empty, `fflate.min.js`.
  POST `/update` is left to the tests.

In Playwright the route registered **last** wins, so mocks added after the
helper override its defaults.

## Rules

- Tests never require changes to `data/www`. If a test exposes a real UI bug,
  open an issue (or a spec), mark the test `test.fixme` with a comment citing
  the issue, and leave the UI untouched in the test change.
- Wait on locators or `expect.poll`, never on `waitForTimeout`; no retries in
  the config.
