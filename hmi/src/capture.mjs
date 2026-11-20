// Renders pages.mjs in headless Chrome and writes, under build/:
//   bg/<id>.png        clean background (all dynamic content removed)
//   press/<id>.png     same page with every touch key in its pressed look
//   states/<id>/<n>.png  full-page frames, n = state index, cropped later by pack.py
//   layout.json        rectangles and metadata of every control
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import puppeteer from 'puppeteer-core';
import { PAGES } from './pages.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(here, '..', 'build');
fs.rmSync(out, { recursive: true, force: true });
for (const d of ['bg', 'press', 'states', 'preview']) fs.mkdirSync(path.join(out, d), { recursive: true });

const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/usr/bin/google-chrome', '/usr/bin/chromium'].find((p) => fs.existsSync(p));
if (!chrome) throw new Error('Chrome or Edge not found');

const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ['--force-device-scale-factor=1', '--allow-file-access-from-files'] });
const tab = await browser.newPage();
await tab.setViewport({ width: 1024, height: 600, deviceScaleFactor: 1 });

const cssUrl = pathToFileURL(path.join(here, 'theme.css')).href;
const layout = { pages: [] };
const GLYPHS = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', ''];

for (const pg of PAGES) {
  const file = path.join(out, `_${pg.id}.html`);
  fs.writeFileSync(file, `<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="${cssUrl}"><style>.fullw{width:100%!important}</style></head><body>${pg.html}</body></html>`);
  await tab.goto(pathToFileURL(file).href, { waitUntil: 'networkidle0' });
  await tab.evaluateHandle('document.fonts.ready');

  const info = await tab.evaluate(() => {
    const r = (e) => { const b = e.getBoundingClientRect(); return [Math.round(b.left), Math.round(b.top), Math.round(b.right), Math.round(b.bottom)]; };
    const q = (s) => [...document.querySelectorAll(s)];
    return {
      keys: q('[data-key]').map((e) => ({ name: e.dataset.key, rect: r(e) })),
      cells: q('[data-cell]').map((e) => ({ name: e.dataset.cell, i: +e.dataset.i, rect: r(e), style: e.closest('.num').className })),
      states: q('[data-state]').map((e) => ({ name: e.dataset.state, n: +e.dataset.n, rect: r(e) })),
      texts: q('[data-text]').map((e) => ({ name: e.dataset.text, len: +e.dataset.len, font: +e.dataset.font, color: e.dataset.color, align: e.dataset.align, rect: r(e) })),
      curves: q('[data-curve]').map((e) => ({ ch: +e.dataset.curve, color: e.dataset.color, min: +e.dataset.min, max: +e.dataset.max, rect: r(e) })),
      qrs: q('[data-qr]').map((e) => ({ name: e.dataset.qr, unit: +e.dataset.unit, rect: r(e) })),
    };
  });
  for (const k of info.keys) if (k.rect[2] <= k.rect[0] || k.rect[3] <= k.rect[1]) throw new Error(`${pg.id}: key ${k.name} has no area`);

  const setStates = (s, cell) => tab.evaluate(([s, cell, glyphs]) => {
    document.querySelectorAll('[data-state]').forEach((e) => {
      const n = +e.dataset.n, i = Math.min(s, n - 1);
      [...e.children].forEach((c, k) => c.classList.toggle('on', k === i));
    });
    document.querySelectorAll('[data-cell]').forEach((e) => { e.textContent = cell == null ? '' : glyphs[cell]; });
  }, [s, cell, GLYPHS]);

  // Background: state 0 of every control sits in the image but is overlaid at
  // runtime; digit cells stay empty.
  await setStates(0, null);
  await tab.screenshot({ path: path.join(out, 'bg', `${pg.id}.png`) });
  await tab.evaluate(() => document.querySelectorAll('[data-key]').forEach((e) => e.classList.add('pressed')));
  await tab.screenshot({ path: path.join(out, 'press', `${pg.id}.png`) });
  await tab.evaluate(() => document.querySelectorAll('[data-key]').forEach((e) => e.classList.remove('pressed')));

  // State frames: digit cells are blank so an overlapping gauge crops clean.
  const dir = path.join(out, 'states', pg.id);
  fs.mkdirSync(dir, { recursive: true });
  const maxN = Math.max(0, ...info.states.map((s) => s.n));
  for (let s = 0; s < maxN; s++) {
    await setStates(s, null);
    await tab.screenshot({ path: path.join(dir, `s${s}.png`) });
  }
  // Digit frames: states rest at 0 so cells over a card crop identical to the page.
  for (let g = 0; g < GLYPHS.length; g++) {
    if (!info.cells.length) break;
    await setStates(0, g);
    const moved = await tab.evaluate((want) => [...document.querySelectorAll('[data-cell]')].findIndex((e, i) => {
      const b = e.getBoundingClientRect();
      return Math.round(b.left) !== want[i][0] || Math.round(b.top) !== want[i][1];
    }), info.cells.map((c) => c.rect));
    if (moved >= 0) throw new Error(`${pg.id}: digit cell ${info.cells[moved].name} moves when filled`);
    await tab.screenshot({ path: path.join(dir, `g${g}.png`) });
  }
  layout.pages.push({ id: pg.id, ...info, maxN });
  fs.rmSync(file);
  console.log(`${pg.id.padEnd(9)} keys=${String(info.keys.length).padEnd(3)} cells=${String(info.cells.length).padEnd(3)} states=${String(info.states.length).padEnd(3)} texts=${info.texts.length}`);
}
fs.writeFileSync(path.join(out, 'layout.json'), JSON.stringify(layout));
await browser.close();
