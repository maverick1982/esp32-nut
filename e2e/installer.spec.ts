import { test, expect } from '@playwright/test';
import { execFileSync } from 'child_process';
import fs from 'fs';
import os from 'os';
import path from 'path';

const INSTALLER = path.resolve(__dirname, '../web-installer');
const indexUrl = `file:///${path.join(INSTALLER, 'index.html').replace(/\\/g, '/')}`;

function readManifest(dir: string, name: string): any {
  return JSON.parse(fs.readFileSync(path.join(dir, name), 'utf-8'));
}

function offsets(manifest: any): Record<string, number> {
  const result: Record<string, number> = {};
  for (const part of manifest.builds[0].parts) {
    result[path.basename(part.path)] = part.offset;
  }
  return result;
}

test.describe('ESP Web Installer', () => {
  test('the main install button is the ESP32-S3 one', async ({ page }) => {
    await page.goto(indexUrl);

    // The custom element renders its shadow DOM asynchronously: check attributes only
    const buttons = page.locator('esp-web-install-button');
    await expect(buttons.first()).toBeAttached();
    await expect(buttons.first()).toHaveAttribute('manifest', 'manifest.json');
  });

  test('ESP32-P4 section: one button per silicon revision, with the test status', async ({ page }) => {
    await page.goto(indexUrl);

    const section = page.locator('.p4-section');
    await expect(section).toBeVisible();
    const v3 = section.locator('.p4-option').filter({ has: page.locator('esp-web-install-button[manifest="manifest-esp32-p4-eth.json"]') });
    const v1 = section.locator('.p4-option').filter({ has: page.locator('esp-web-install-button[manifest="manifest-esp32-p4-eth-v1.json"]') });
    await expect(v3).toHaveCount(1);
    await expect(v1).toHaveCount(1);
    await expect(v3).toContainText('ESP32-P4 v3.1 or later');
    await expect(v3).toContainText('Not yet tested on hardware');
    await expect(v1).toContainText('ESP32-P4 before v3 (v1.x)');
    await expect(v1).toContainText('Tested on a v1.3 board');
    await expect(page.locator('esp-web-install-button')).toHaveCount(3);
  });

  test('the revision help is closed and explains how to read the revision', async ({ page }) => {
    await page.goto(indexUrl);

    const help = page.locator('details.p4-help');
    await expect(help).not.toHaveAttribute('open', /.*/);
    await help.locator('summary').click();
    await expect(help).toHaveAttribute('open', '');
    await expect(help).toContainText('esptool chip-id');
    await expect(help).toContainText('Chip is ESP32-P4 (revision v1.3)');
    await expect(help).toContainText('chip revision: v1.3');
    await expect(help).toContainText('Not supported by either image');
  });

  test('ESP Web Tools is pinned to a version that knows the ESP32-P4', async ({ page }) => {
    await page.goto(indexUrl);
    await expect(page.locator('script[src*="esp-web-tools@"]')).toHaveAttribute('src', /esp-web-tools@10\.4\.0\//);
  });

  test('no horizontal scroll on a 375 px phone', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 812 });
    await page.goto(indexUrl);
    await page.locator('details.p4-help summary').click();
    const scrollWidth = await page.evaluate(() => document.documentElement.scrollWidth);
    expect(scrollWidth).toBeLessThanOrEqual(375);
  });

  test('manifest.json is ESP32-S3 only, with the offsets of the releases before the P4', () => {
    const manifest = readManifest(INSTALLER, 'manifest.json');
    expect(manifest.name).toBe('ESP32 NUT Server');
    expect(manifest.builds).toHaveLength(1);
    expect(manifest.builds[0].chipFamily).toBe('ESP32-S3');
    expect(offsets(manifest)).toEqual({
      'bootloader.bin': 0,
      'partitions.bin': 32768,
      'boot_app0.bin': 57344,
      'firmware.bin': 65536,
    });
    // The ESP32-S3 binaries stay at the root of the site, as before the P4
    for (const part of manifest.builds[0].parts) {
      expect(part.path).not.toContain('/');
    }
  });

  for (const env of ['esp32-p4-eth', 'esp32-p4-eth-v1']) {
    test(`manifest-${env}.json: ESP32-P4 with the bootloader at 0x2000, in the board folder`, () => {
      const manifest = readManifest(INSTALLER, `manifest-${env}.json`);
      expect(manifest.builds).toHaveLength(1);
      expect(manifest.builds[0].chipFamily).toBe('ESP32-P4');
      expect(offsets(manifest)).toEqual({
        'bootloader.bin': 8192,
        'partitions.bin': 32768,
        'boot_app0.bin': 57344,
        'firmware.bin': 65536,
      });
      for (const part of manifest.builds[0].parts) {
        expect(part.path.startsWith(`latest/${env}/`)).toBe(true);
      }
    });
  }

  test('release packages: zip names per board and the files inside', () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'esp32-nut-package-'));
    const script = path.resolve(__dirname, '../scripts/release_package.py');
    try {
      // Fake build output: the script only copies and zips the files
      const build = path.join(tmp, 'build');
      fs.mkdirSync(build);
      for (const name of ['firmware.bin', 'bootloader.bin', 'partitions.bin', 'firmware.factory.bin', 'boot_app0.bin']) {
        fs.writeFileSync(path.join(build, name), name);
      }
      const zips = path.join(tmp, 'dist');
      const expected: Record<string, string> = {
        'esp32-s3-standard': 'esp32-nut-v9.9.9.zip',
        'esp32-p4-eth': 'esp32-nut-v9.9.9-esp32-p4-eth.zip',
        'esp32-p4-eth-v1': 'esp32-nut-v9.9.9-esp32-p4-eth-v1.zip',
      };
      for (const env of Object.keys(expected)) {
        execFileSync('python', [script, 'package', '--env', env, '--version', 'v9.9.9',
          '--build-dir', build, '--out', path.join(tmp, 'pkg', env), '--zip-dir', zips,
          '--boot-app0', path.join(build, 'boot_app0.bin')]);
        const listing = execFileSync('python', ['-m', 'zipfile', '-l', path.join(zips, expected[env])]).toString();
        const names = listing.split(/\r?\n/).slice(1).map(l => l.split(/\s+/)[0]).filter(Boolean).sort();
        expect(names).toEqual(['README.txt', 'boot_app0.bin', 'bootloader.bin', 'firmware.bin',
          'firmware.factory.bin', 'manifest.json', 'partitions.bin']);
      }
      expect(fs.readdirSync(zips).sort()).toEqual(Object.values(expected).sort());

      // A branch name as version (manual run from feature/...) is refused, not half-written
      expect(() => execFileSync('python', [script, 'package', '--env', 'esp32-s3-standard',
        '--version', 'feature/x', '--build-dir', build, '--out', path.join(tmp, 'bad'), '--zip-dir', zips,
        '--boot-app0', path.join(build, 'boot_app0.bin')], { stdio: 'pipe' })).toThrow();
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true });
    }
  });

  test('the committed manifests are the ones scripts/release_package.py generates', () => {
    const out = fs.mkdtempSync(path.join(os.tmpdir(), 'esp32-nut-manifests-'));
    try {
      execFileSync('python', [path.resolve(__dirname, '../scripts/release_package.py'), 'manifests',
        '--version', 'latest', '--out', out]);
      const generated = fs.readdirSync(out).sort();
      expect(generated).toEqual(['manifest-esp32-p4-eth-v1.json', 'manifest-esp32-p4-eth.json', 'manifest.json']);
      for (const name of generated) {
        expect(readManifest(INSTALLER, name)).toEqual(readManifest(out, name));
      }
    } finally {
      fs.rmSync(out, { recursive: true, force: true });
    }
  });
});
