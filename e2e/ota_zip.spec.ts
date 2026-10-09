import { test, expect } from '@playwright/test';
import { serveUpdatePage } from './support/webui';
import * as fflate from 'fflate';

test.describe('OTA Update via ZIP', () => {
    let mockBinBuffer: Buffer;
    let validZipBuffer: Buffer;
    let invalidZipBuffer: Buffer;

    test.beforeAll(() => {
        // Create a mock binary file
        mockBinBuffer = Buffer.from('mock firmware data');

        // Create a valid zip containing firmware.bin
        const validZip = fflate.zipSync({
            'firmware.bin': mockBinBuffer
        });
        validZipBuffer = Buffer.from(validZip);

        // Create an invalid zip without firmware.bin
        const invalidZip = fflate.zipSync({
            'wrong_file.txt': Buffer.from('some text')
        });
        invalidZipBuffer = Buffer.from(invalidZip);
    });

    test.beforeEach(async ({ page }) => {
        // Serve the OTA page as the firmware does; only the upload POST is mocked here
        await serveUpdatePage(page);
        await page.route('http://esp32.local/update', async route => {
            if (route.request().method() === 'POST') {
                await route.fulfill({
                    status: 200,
                    contentType: 'text/plain',
                    body: 'OK'
                });
            } else {
                await route.fallback();
            }
        });

        await page.goto('http://esp32.local/update');
    });

    test('Uploads standard .bin file', async ({ page }) => {
        const fileChooserPromise = page.waitForEvent('filechooser');
        await page.click('#dropzone');
        const fileChooser = await fileChooserPromise;

        await fileChooser.setFiles({
            name: 'firmware.bin',
            mimeType: 'application/octet-stream',
            buffer: mockBinBuffer
        });

        await expect(page.locator('#statusMessage')).toContainText('Update complete!');
    });

    test('Uploads valid .zip file', async ({ page }) => {
        const fileChooserPromise = page.waitForEvent('filechooser');
        await page.click('#dropzone');
        const fileChooser = await fileChooserPromise;

        await fileChooser.setFiles({
            name: 'release.zip',
            mimeType: 'application/zip',
            buffer: validZipBuffer
        });

        // Wait for the upload success message
        await expect(page.locator('#statusMessage')).toContainText('Update complete!', { timeout: 10000 });
    });

    test('Shows error for .zip without firmware.bin', async ({ page }) => {
        const fileChooserPromise = page.waitForEvent('filechooser');
        await page.click('#dropzone');
        const fileChooser = await fileChooserPromise;

        await fileChooser.setFiles({
            name: 'bad_release.zip',
            mimeType: 'application/zip',
            buffer: invalidZipBuffer
        });

        // Wait for the error message
        await expect(page.locator('#statusMessage')).toContainText('Il file .zip non contiene alcun firmware.bin');
    });
});
