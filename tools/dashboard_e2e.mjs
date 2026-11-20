// End-to-end check of the built dashboard against cloud/local. Start first:
//   cd cloud && node local/server.mjs        (broker + API)
//   cd cloud && node local/device.mjs rig-001 --seed
//   cd dashboard && npx vite build && npx vite preview --port 4173
// Then: node tools/dashboard_e2e.mjs [shots_dir]
import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const require = createRequire(path.join(root, 'hmi', 'package.json'));
const puppeteer = require('puppeteer-core');

const shots = process.argv[2];
if (shots) fs.mkdirSync(shots, { recursive: true });
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', '/usr/bin/google-chrome'].find((p) => fs.existsSync(p));
const URL_ = 'http://localhost:4173/';
const results = [];
const check = (ok, what) => { results.push(!!ok); console.log(`${ok ? '  ok  ' : '  FAIL'} ${what}`); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const browser = await puppeteer.launch({ executablePath: chrome, headless: true });
const ctx = await browser.createBrowserContext();
const page = await ctx.newPage();
await page.setViewport({ width: 390, height: 844, deviceScaleFactor: 2, isMobile: true, hasTouch: true });
const errors = [];
page.on('pageerror', (e) => errors.push(e.message));
page.on('console', (m) => { if (m.type() === 'error' && !/favicon|401/.test(m.text())) errors.push(m.text()); });
const shot = async (n) => { if (shots) await page.screenshot({ path: path.join(shots, `${n}.png`) }); };
const text = () => page.evaluate(() => document.body.innerText);
const waitText = (s, ms = 8000) => page.waitForFunction((t) => document.body.innerText.includes(t), { timeout: ms }, s);
const clickText = (s) => page.evaluate((t) => { const b = [...document.querySelectorAll('button,a')].find((e) => e.textContent.includes(t)); b.click(); }, s);

console.log('sign in');
await page.goto(URL_);
await page.waitForSelector('input[type=email]');
await shot('01_login');
await page.type('input[type=email]', 'operator@spk.local');
await page.type('input[type=password]', 'wrong');
await page.click('button.pri');
await waitText('Incorrect email or password');
check(true, 'wrong password is refused with a clear message');
await page.$eval('input[type=password]', (e) => { e.value = ''; });
await page.click('input[type=password]', { clickCount: 3 });
await page.type('input[type=password]', 'demo1234');
await page.click('button.pri');
await waitText('Interlocks');
check(true, 'operator signed in, live screen shown');

console.log('live data');
await waitText('rig-001');
await sleep(9000);
let t = await text();
check(/Running|Stopping/.test(t), 'machine state shown as running');
check(t.includes('km/h') && t.includes('Motor current'), 'metrics shown');
const rpm1 = await page.$eval('.gauge-in b', (e) => e.textContent);
check(Number(rpm1) > 0, `gauge shows live RPM (${rpm1})`);
await sleep(4500);
check(await page.$('.chart svg path'), 'trend chart drawn');
await shot('02_live_running');
await page.evaluate(() => window.scrollTo(0, 620));
await shot('03_live_lower');
await page.evaluate(() => window.scrollTo(0, 0));

// Hover layer
const box = await (await page.$('.chart svg')).boundingBox();
await page.mouse.move(box.x + box.width / 2, box.y + 60);
await sleep(100);
check(await page.$('.tip'), 'chart tooltip appears on hover');

console.log('remote control');
await clickText('Stop test');
await waitText('Sent to machine', 4000);
check(true, 'stop command accepted');
await waitText('Complete', 15000);
check(true, 'device stopped: state goes to complete');
await sleep(9000);
await waitText('Ready', 6000);
await page.evaluate(() => window.scrollTo(0, document.body.scrollHeight));
const hold = await page.$('.hold');
const hb = await hold.boundingBox();
await page.mouse.move(hb.x + 40, hb.y + 20);
await page.mouse.down();
await sleep(300);
await page.mouse.up();
await sleep(2500);
check(!(await text()).includes('Running'), 'a short tap does not start the machine');
await shot('04_ready_control');
const hb2 = await (await page.$('.hold')).boundingBox();
await page.mouse.move(hb2.x + 40, hb2.y + 20);
await page.mouse.down();
await sleep(1500);
await page.mouse.up();
await waitText('Sent to machine', 4000);
await waitText('Running', 6000);
check(true, 'press and hold starts a test remotely');

console.log('reports');
await page.evaluate(() => { location.hash = '/tests'; });
await waitText('Test #');
t = await text();
check(/Test #1[0-9]/.test(t), 'newest report at top of list');
await sleep(400);
await shot('05_reports');
const first = await page.$eval('.list a', (a) => a.getAttribute('href'));
await page.evaluate((h) => { location.hash = h.slice(1); }, first);
await waitText('Download PDF');
await sleep(400);
await shot('06_report_detail');
await page.evaluate(() => window.scrollTo(0, 560));
await shot('07_report_lower');

const pdf = await page.evaluate(async () => {
  const saved = [];
  const orig = URL.createObjectURL;
  URL.createObjectURL = (b) => { saved.push(b); return orig.call(URL, b); };
  [...document.querySelectorAll('button')].find((b) => b.textContent.includes('PDF')).click();
  await new Promise((r) => setTimeout(r, 1500));
  const b = saved[0];
  return b ? { type: b.type, text: await b.text() } : null;
});
check(pdf?.type === 'application/pdf' && pdf.text.startsWith('%PDF-1.4'), 'PDF generated in the browser');
if (shots && pdf) fs.writeFileSync(path.join(shots, 'browser_report.pdf'), pdf.text, 'latin1');

await page.evaluate(() => { location.hash = '/events'; });
await waitText('Guard opened');
check(true, 'fault log shown');
await sleep(300);
await shot('08_faults');

console.log('viewer role');
const p2ctx = await browser.createBrowserContext();
const p2 = await p2ctx.newPage();
await p2.setViewport({ width: 390, height: 844, isMobile: true, hasTouch: true });
await p2.goto(URL_);
await p2.waitForSelector('input[type=email]');
await p2.type('input[type=email]', 'viewer@spk.local');
await p2.type('input[type=password]', 'demo1234');
await p2.click('button.pri');
await p2.waitForFunction(() => document.body.innerText.includes('view-only'), { timeout: 8000 });
check(!(await p2.evaluate(() => document.body.innerText)).includes('Stop test'), 'viewer sees no controls');
const denied = await fetch('http://localhost:8787/machines/rig-001/cmd', { method: 'POST', headers: { authorization: 'local-viewer', 'content-type': 'application/json' }, body: '{"cmd":"stop"}' });
check(denied.status === 403, 'API refuses commands from a viewer even if called directly');
await p2ctx.close();

console.log('layout and load speed');
const over = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
check(!over, 'no sideways scrolling at phone width');
await ctx.close();

// Cold and warm load on throttled 4G (9 Mbit/s, 60 ms) with 4x slower CPU.
const c3 = await browser.createBrowserContext();
const p3 = await c3.newPage();
await p3.setViewport({ width: 390, height: 844, isMobile: true, hasTouch: true });
const cdp = await p3.createCDPSession();
await cdp.send('Network.enable');
await cdp.send('Network.emulateNetworkConditions', { offline: false, latency: 60, downloadThroughput: 9e6 / 8, uploadThroughput: 3e6 / 8 });
await cdp.send('Emulation.setCPUThrottlingRate', { rate: 4 });
const paint = () => p3.evaluate(() => {
  const n = performance.getEntriesByType('navigation')[0];
  const f = performance.getEntriesByName('first-contentful-paint')[0];
  return { fcp: Math.round(f?.startTime ?? 0), bytes: performance.getEntriesByType('resource').reduce((a, r) => a + r.transferSize, n.transferSize) };
});
await p3.goto(URL_, { waitUntil: 'load' });
await p3.waitForSelector('input[type=email]');
const cold = await paint();
const t0 = Date.now();
await p3.type('input[type=email]', 'viewer@spk.local');
await p3.type('input[type=password]', 'demo1234');
await p3.click('button.pri');
await p3.waitForSelector('.gauge');
console.log(`  cold: first paint ${cold.fcp} ms, ${(cold.bytes / 1024).toFixed(0)} KB transferred`);
await sleep(1500);   // lets the service worker finish installing
await p3.reload({ waitUntil: 'load' });
const r0 = await p3.evaluate(() => performance.now());
await p3.waitForSelector('.gauge');
const warm = await paint();
console.log(`  repeat visit: first paint ${warm.fcp} ms, ${(warm.bytes / 1024).toFixed(0)} KB transferred, live screen at ${Math.round(await p3.evaluate(() => performance.now()))} ms`);
check(cold.fcp < 1500, `cold first paint under 1.5 s on 4G with slow CPU (${cold.fcp} ms)`);
check(warm.bytes < cold.bytes / 4, 'repeat visit loads from the service worker');

check(errors.length === 0, `no browser errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);
await browser.close();
const bad = results.filter((r) => !r).length;
console.log(`\n${results.length - bad} passed, ${bad} failed`);
process.exit(bad ? 1 : 0);
