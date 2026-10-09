// Regenerates the Web UI screenshots in docs/images/ (overwrites them).
// Cross-platform replacement for `SCREENSHOTS=1 playwright test e2e/screenshot.spec.ts`
// without extra dependencies: sets SCREENSHOTS so playwright.config.ts stops
// ignoring screenshot.spec.ts. Extra arguments are passed to Playwright.
// Usage: npm run e2e:screenshots
const path = require('path');
const { spawnSync } = require('child_process');

const cli = path.resolve(__dirname, '..', '..', 'node_modules', '@playwright', 'test', 'cli.js');
const result = spawnSync(
  process.execPath,
  [cli, 'test', 'e2e/screenshot.spec.ts', ...process.argv.slice(2)],
  { stdio: 'inherit', cwd: path.resolve(__dirname, '..', '..'), env: { ...process.env, SCREENSHOTS: '1' } },
);
process.exit(result.status === null ? 1 : result.status);
