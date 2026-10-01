#!/usr/bin/env node
// Measures every artboard in design/frames/, writes its real height into the
// <meta name="paper:artboard"> tag, and saves a full-page PNG per frame.
//   npm i -D playwright && npx playwright install chromium
//   node design/tools/render.mjs [outDir]
// Frames are served over a local HTTP server because the shader runtime is an ES
// module (file:// blocks module scripts). Shaders render one still frame: the page
// is loaded with prefers-reduced-motion, which p2p-shaders.js honours.
// Convert the PNGs to design/renders/*.webp (720 wide; 1200 for 2000-wide boards).
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFileSync, writeFileSync, readdirSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join, extname, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const design = join(dirname(fileURLToPath(import.meta.url)), '..');
const frames = join(design, 'frames');
const out = process.argv[2] ?? join(design, 'renders', 'png');
mkdirSync(out, { recursive: true });

const TYPES = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.svg': 'image/svg+xml', '.webp': 'image/webp', '.png': 'image/png', '.json': 'application/json' };
const server = createServer((req, res) => {
  const path = normalize(join(design, decodeURIComponent(new URL(req.url, 'http://x').pathname)));
  if (!path.startsWith(design) || !existsSync(path)) return res.writeHead(404).end();
  res.writeHead(200, { 'content-type': TYPES[extname(path)] ?? 'application/octet-stream' }).end(readFileSync(path));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/frames/`;

const browser = await chromium.launch();
for (const file of readdirSync(frames).filter((f) => f.endsWith('.html') && !f.startsWith('_')).sort()) {
  const path = join(frames, file);
  let html = readFileSync(path, 'utf8');
  const width = Number(html.match(/data-width="(\d+)"/)[1]);
  const pinned = /data-pinned/.test(html);
  const page = await browser.newPage({ viewport: { width, height: 800 }, reducedMotion: 'reduce' });
  const settle = async () => {
    await page.evaluate(() => document.fonts.ready);
    await page
      .waitForFunction(() => [...document.querySelectorAll('[data-p2p-shader]')].every((e) => e.dataset.p2pShaderState), null, { timeout: 30000 })
      .catch(() => console.warn(`${file}: shaders did not all mount`));
    await page.waitForTimeout(500);
  };
  await page.goto(base + file, { waitUntil: 'networkidle' });
  await settle();
  if (!pinned) {
    const height = await page.evaluate(() => {
      document.body.style.minHeight = '0px';
      return Math.ceil(document.documentElement.scrollHeight);
    });
    html = html.replace(/data-height="\d+"/, `data-height="${height}"`).replace(/--board-h: \d+px/, `--board-h: ${height}px`);
    writeFileSync(path, html);
    await page.reload({ waitUntil: 'networkidle' });
    await settle();
  }
  await page.screenshot({ path: join(out, file.replace('.html', '.png')), fullPage: true, timeout: 120000 });
  console.log(file, width);
  await page.close();
}
await browser.close();
server.close();
