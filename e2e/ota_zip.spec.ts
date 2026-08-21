import { test, expect } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';
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
        // Intercept routes to serve local files
        await page.route('http://esp32.local/update', async route => {
            if (route.request().method() === 'GET') {
                await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/update.html') });
            } else if (route.request().method() === 'POST') {
                await route.fulfill({
                    status: 200,
                    contentType: 'text/plain',
                    body: 'OK'
                });
            } else {
                await route.continue();
            }
        });
        
        await page.route('http://esp32.local/fflate.min.js', async route => {
            await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/fflate.min.js') });
        });

        await page.route('**/*shared.css*', async route => {
            await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/shared.css') });
        });
        
        await page.route('**/*mobile.css*', async route => {
            await route.fulfill({ path: path.resolve(process.cwd(), 'data/www/mobile.css') });
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
