import { test, expect } from '@playwright/test';
import fs from 'fs';
import path from 'path';

test.describe('ESP Web Installer', () => {
  test('should display the web install button', async ({ page }) => {
    // Caricamento del file locale
    const indexPath = `file:///${path.resolve(__dirname, '../web-installer/index.html').replace(/\\/g, '/')}`;
    
    await page.goto(indexPath);
    
    // Verifica che il bottone sia presente
    const installButton = page.locator('esp-web-install-button');
    await expect(installButton).toBeAttached();
    // Non testiamo toBeVisible() perchè il custom element web-install-button 
    // ha una sua logica shadow DOM e potrebbe richiedere script asincroni
    
    // Verifica dell'attributo manifest
    await expect(installButton).toHaveAttribute('manifest', 'manifest.json');
  });

  test('manifest.json is valid and contains correct offsets', () => {
    const manifestPath = path.resolve(__dirname, '../web-installer/manifest.json');
    const content = fs.readFileSync(manifestPath, 'utf-8');
    const manifest = JSON.parse(content);
    
    expect(manifest.name).toBe('ESP32 NUT Server');
    expect(manifest.builds[0].chipFamily).toBe('ESP32-S3');
    
    const parts = manifest.builds[0].parts;
    expect(parts.find((p: any) => p.path === 'bootloader.bin').offset).toBe(0);
    expect(parts.find((p: any) => p.path === 'partitions.bin').offset).toBe(32768);
    expect(parts.find((p: any) => p.path === 'firmware.bin').offset).toBe(65536);
  });
});
