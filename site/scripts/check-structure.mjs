import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
const publicDir = resolve(import.meta.dirname, '../public');
const routes = new Map(readdirSync(publicDir).filter(p => p.endsWith('.html')).map(p => [p === 'index.html' ? '/' : '/' + p.slice(0, -5), p]));
const sitemap = readFileSync(resolve(publicDir, 'sitemap.xml'), 'utf8');
for (const [route, file] of routes) {
  const content = readFileSync(resolve(publicDir, file), 'utf8');
  assert.equal((content.match(/<h1[ >]/g) || []).length, 1, file + ': one h1');
  for (const marker of ['name="description"', 'rel="canonical"', 'property="og:title"', 'aria-label="Main navigation"', 'class="skip-link"']) assert.ok(content.includes(marker), file + ': ' + marker);
  assert.ok(!/fonts\.(googleapis|gstatic)\.com/.test(content), file + ': local fonts');
  for (const [, href] of content.matchAll(/(?:href|src)="([^" ]+)"/g)) {
    if (!href.startsWith('/')) continue;
    const [path, hash] = href.split('#');
    const target = routes.get(path) || path.slice(1);
    assert.ok(existsSync(resolve(publicDir, target)), file + ': missing ' + href);
    if (hash) assert.ok(readFileSync(resolve(publicDir, target), 'utf8').includes('id="' + hash + '"'), file + ': missing anchor ' + href);
  }
  if (route !== '/404' && route !== '/login') assert.ok(sitemap.includes('https://punch2pen.com' + route + '</loc>'), file + ': sitemap entry');
}
assert.ok(!sitemap.includes('/404</loc>') && !sitemap.includes('/login</loc>'));
assert.ok(readFileSync(resolve(publicDir, 'robots.txt'), 'utf8').includes('Sitemap: https://punch2pen.com/sitemap.xml'));
console.log('Site structure: page metadata, assets, routes, anchors and sitemap pass.');
