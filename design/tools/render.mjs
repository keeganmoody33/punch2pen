#!/usr/bin/env node
// Measures every artboard in design/frames/, writes its real height into the
// <meta name="paper:artboard"> tag, and saves a full-page PNG per frame.
//   npm i -D playwright && npx playwright install chromium
//   node design/tools/render.mjs [outDir]
// Convert the PNGs to design/renders/*.webp (720 wide; 1200 for 2000-wide boards).
import { chromium } from 'playwright';
import { readFileSync, writeFileSync, readdirSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const frames = join(dirname(fileURLToPath(import.meta.url)), '..', 'frames');
const out = process.argv[2] ?? join(frames, '..', 'renders', 'png');
mkdirSync(out, { recursive: true });

const browser = await chromium.launch();
for (const file of readdirSync(frames).filter((f) => f.endsWith('.html') && !f.startsWith('_')).sort()) {
  const path = join(frames, file);
  let html = readFileSync(path, 'utf8');
  const width = Number(html.match(/data-width="(\d+)"/)[1]);
  const pinned = /data-pinned/.test(html);
  const page = await browser.newPage({ viewport: { width, height: 800 } });
  await page.goto(`file://${path}`, { waitUntil: 'networkidle' });
  await page.evaluate(() => document.fonts.ready);
  if (!pinned) {
    const height = await page.evaluate(() => {
      document.body.style.minHeight = '0px';
      return Math.ceil(document.documentElement.scrollHeight);
    });
    html = html.replace(/data-height="\d+"/, `data-height="${height}"`).replace(/--board-h: \d+px/, `--board-h: ${height}px`);
    writeFileSync(path, html);
    await page.reload({ waitUntil: 'networkidle' });
    await page.evaluate(() => document.fonts.ready);
  }
  await page.screenshot({ path: join(out, file.replace('.html', '.png')), fullPage: true });
  console.log(file, width);
  await page.close();
}
await browser.close();
