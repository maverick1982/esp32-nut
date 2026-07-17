import { test, expect } from '@playwright/test';
import * as path from 'path';
import * as fs from 'fs';

test.describe('OTA Update Flow', () => {
  let dummyBinPath: string;

  test.beforeAll(() => {
    // Crea un file .bin fittizio per il test
    dummyBinPath = path.join(__dirname, 'dummy_firmware.bin');
    fs.writeFileSync(dummyBinPath, Buffer.from([0xE9, 0x01, 0x02, 0x03]));
  });

  test.afterAll(() => {
    // Pulisci il file fittizio
    if (fs.existsSync(dummyBinPath)) {
      fs.unlinkSync(dummyBinPath);
    }
  });

  test('should navigate to OTA page from index', async ({ page }) => {
    await page.goto('/');
    
    // Clicca sul tab OTA
    await page.click('button[data-target="ota"]');
    
    // Verifica che l'URL sia corretto
    await expect(page).toHaveURL(/\/update/);
  });

  test('should load OTA page and have upload elements', async ({ page }) => {
    await page.goto('/update');
    
    await expect(page.locator('h2')).toContainText('OTA');
    await expect(page.locator('#fileInput')).toBeAttached();
    await expect(page.locator('.dropzone')).toBeVisible();
  });

  test('demo__user-uploads-firmware', async ({ page }) => {
    await page.goto('/update');

    // Assicura che la richiesta OTA venga intercettata per non fallire sul backend inesistente o restituire OK fittizio
    await page.route('/update', route => {
      route.fulfill({
        status: 200,
        contentType: 'text/plain',
        body: 'OK',
      });
    });

    // Usa setInputFiles sull'input nascosto
    await page.locator('#fileInput').setInputFiles(dummyBinPath);
    
    // Verifica che lo status venga aggiornato e appaia la progress bar
    await expect(page.locator('#statusMessage')).toContainText('completato', { ignoreCase: true });
    await expect(page.locator('#progressBar')).toBeVisible();
  });
});
