import { test, expect } from '@playwright/test';
import { serveIndex } from './support/webui';

test.describe('Responsive Web UI', () => {
  // Impostiamo la viewport a una risoluzione tipica per mobile
  test.use({ viewport: { width: 375, height: 812 } });

  test.beforeEach(async ({ page }) => {
    await serveIndex(page);
  });

  test('should display mobile layout and prevent horizontal scroll', async ({ page }) => {
    await page.goto('http://esp32.local/');

    const appContainer = page.locator('.app-container');
    await expect(appContainer).toBeVisible();

    // Verifichiamo che i container principali abbiano ricevuto il layout a colonna
    const containerStyle = await appContainer.evaluate((el) => window.getComputedStyle(el).flexDirection);
    expect(containerStyle).toBe('column');

    // Verifichiamo che non ci sia scroll orizzontale (nessun elemento eccede la larghezza)
    const scrollWidth = await page.evaluate(() => document.documentElement.scrollWidth);
    const clientWidth = await page.evaluate(() => document.documentElement.clientWidth);
    
    expect(scrollWidth).toBeLessThanOrEqual(clientWidth);
  });
});
