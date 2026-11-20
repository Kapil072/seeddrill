// Draws the wiring diagram. GPIO numbers are read from firmware/src/app.h, so the
// drawing and the firmware cannot disagree. Output: docs/circuit_diagram.{svg,png}
// Run from the repo root: node tools/make_circuit.mjs
import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const app = fs.readFileSync(path.join(root, 'firmware/src/app.h'), 'utf8');
const pin = Object.fromEntries([...app.matchAll(/(PIN_\w+) = (\d+)/g)].map((m) => [m[1], Number(m[2])]));
const W = 2240, H = 1560;
const C = { line: '#a0522d', neutral: '#2563a8', earth: '#2e8b3d', v24: '#d0342c', v5: '#e07b00', gnd: '#222', sig: '#0f7c6c', rs: '#7a3fc0', ttl: '#1e88c7', mech: '#777' };

let out = '';
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;');
const box = (x, y, w, h, title, sub, fill = '#f5f8f6', stroke = '#33413b') => {
  out += `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="8" fill="${fill}" stroke="${stroke}" stroke-width="2"/>`;
  if (title) out += `<text x="${x + w / 2}" y="${y + 24}" class="t">${esc(title)}</text>`;
  if (sub) out += `<text x="${x + w / 2}" y="${y + 42}" class="s">${esc(sub)}</text>`;
};
const label = (x, y, s, cls = 'p', anchor = 'start') => { out += `<text x="${x}" y="${y}" class="${cls}" text-anchor="${anchor}">${esc(s)}</text>`; };
// Orthogonal wire through the given points, with a dot at each end.
const wire = (color, pts, tag, tagAt) => {
  out += `<polyline points="${pts.map((p) => p.join(',')).join(' ')}" fill="none" stroke="${color}" stroke-width="2.4" stroke-linejoin="round"/>`;
  for (const p of [pts[0], pts[pts.length - 1]]) out += `<circle cx="${p[0]}" cy="${p[1]}" r="3.6" fill="${color}"/>`;
  if (tag) {
    const [x, y] = tagAt ?? [(pts[0][0] + pts[1][0]) / 2, pts[0][1] - 6];
    out += `<text x="${x}" y="${y}" class="w" fill="${color}" text-anchor="middle">${esc(tag)}</text>`;
  }
};

out += `<rect width="${W}" height="${H}" fill="#fff"/>`;
out += `<text x="40" y="46" class="h">Seed Drill Field Simulator: wiring diagram</text>`;
out += `<text x="40" y="72" class="s2">ESP32-S3 + DWIN DMG10600C070_03WTR + Delta MS300 VFD. Reference design: a qualified electrician must verify ratings, protection and the safety circuit before power-up.</text>`;

// ---- Mains and drive ---------------------------------------------------------
box(40, 110, 130, 170, 'MAINS', '230 V AC');
[['L', 158, C.line], ['N', 195, C.neutral], ['PE', 240, C.earth]].forEach(([n, y, c]) => { label(150, y + 5, n, 'p', 'end'); });
box(240, 110, 110, 170, 'Q1', 'MCB + RCD');
box(420, 110, 120, 170, 'K1', 'Line contactor');
box(640, 95, 360, 245, 'VFD: Delta MS300 2.2 kW', '230 V 1-ph in, 3-ph out, RS485');
wire(C.line, [[170, 150], [240, 150]], 'L');
wire(C.neutral, [[170, 195], [240, 195]], 'N');
wire(C.line, [[350, 150], [420, 150]]);
wire(C.neutral, [[350, 195], [420, 195]]);
wire(C.line, [[540, 150], [640, 150]], 'R/L1');
wire(C.neutral, [[540, 195], [640, 195]], 'S/L2');
wire(C.earth, [[170, 240], [640, 240]], 'PE', [400, 234]);
label(650, 155, 'R/L1'); label(650, 200, 'S/L2'); label(650, 245, 'PE');
label(990, 155, 'U', 'p', 'end'); label(990, 195, 'V', 'p', 'end'); label(990, 235, 'W', 'p', 'end');
box(1060, 120, 170, 130, 'MOTOR', '2 HP 3-ph 4-pole', '#eef2f7');
wire('#555', [[1000, 150], [1060, 150]], 'U');
wire('#555', [[1000, 190], [1060, 190]], 'V');
wire('#555', [[1000, 230], [1060, 230]], 'W');
box(1300, 120, 170, 130, 'GEARBOX', 'Helical 15:1', '#eef2f7');
box(1540, 120, 190, 130, 'CHAIN COUPLING', 'Guarded sprockets', '#eef2f7');
box(1800, 120, 190, 130, 'SEED DRILL', 'Metering transmission', '#eef2f7');
out += `<g stroke="${C.mech}" stroke-width="7" stroke-linecap="round"><line x1="1230" y1="185" x2="1300" y2="185"/><line x1="1470" y1="185" x2="1540" y2="185"/><line x1="1730" y1="185" x2="1800" y2="185"/></g>`;
label(1880, 285, '30 to 100 RPM output', 's', 'middle');

// VFD control terminals, along the bottom edge
label(655, 326, 'MI1'); label(705, 326, 'DCM'); label(770, 326, 'RB'); label(812, 326, 'RC'); label(925, 300, 'RJ45', 's', 'middle'); label(872, 326, 'SG+ 5'); label(932, 326, 'SG- 4');

// ---- 24 V supply --------------------------------------------------------------
box(240, 360, 190, 110, 'PSU1', '230 V AC to 24 V DC 60 W');
wire(C.line, [[295, 280], [295, 360]]);
wire(C.neutral, [[325, 280], [325, 360]]);
box(240, 510, 190, 90, 'BUCK', '24 V to 5 V, 3 A');
wire(C.v24, [[300, 470], [300, 510]], '+24V', [270, 495]);
wire(C.gnd, [[370, 470], [370, 510]], '0V', [395, 495]);

// ---- Hardwired safety chain ----------------------------------------------------
box(452, 370, 196, 145, 'S1 E-STOP', 'Twin NC, hardwired', '#fff3f2', '#c0302a');
wire(C.v24, [[430, 400], [452, 400]]);
label(464, 450, 'NC1: K1 coil, no', 's2');
label(464, 470, 'software in the path', 's2');
label(464, 497, 'NC2: status to ESP', 's2');
wire(C.v24, [[500, 370], [500, 320], [465, 320], [465, 280]], 'A1', [448, 312]);
wire(C.gnd, [[515, 280], [515, 345], [400, 345], [400, 360]], 'A2 to 0V', [565, 322]);

// ---- Display ---------------------------------------------------------------------
box(40, 720, 300, 280, 'DWIN 7" HMI', 'DMG10600C070_03WTR, 1024 x 600', '#eef6ff', '#1e88c7');
const hmi = [['VCC (FFC 1,2,3)', 800], ['TX2 (FFC 5)', 850], ['RX2 (FFC 6)', 900], ['GND (FFC 8,9,10)', 950]];
hmi.forEach(([n, y]) => label(325, y + 5, n, 'p', 'end'));
label(55, 985, 'R232 jumper open = RS232 (default)', 's2');
box(450, 820, 170, 110, 'U1 MAX3232', 'RS232 level shift');
label(535, 886, 'VCC from ESP 3V3', 's', 'middle');
wire(C.v5, [[395, 600], [395, 800]], '+5V', [372, 720]);
wire(C.v5, [[340, 800], [395, 800]]);
wire(C.gnd, [[280, 600], [280, 660], [420, 660], [420, 950]], '0V', [300, 652]);
wire(C.ttl, [[340, 850], [450, 850]], 'RS232', [395, 844]);
wire(C.ttl, [[340, 900], [450, 900]], 'RS232', [395, 894]);
wire(C.gnd, [[340, 950], [420, 950], [420, 1040]]);

// ---- ESP32 ------------------------------------------------------------------------
const ex = 760, ey = 700, ew = 360, eh = 640;
box(ex, ey, ew, eh, 'ESP32-S3 DevKitC-1', 'N8 or larger: 8 MB flash minimum', '#f1fbf3', '#2e8b3d');
label(ex + ew / 2, ey + 76, 'Core 1: control, VFD, HMI', 's', 'middle');
label(ex + ew / 2, ey + 96, 'Core 0: Wi-Fi, MQTT, flash, web', 's', 'middle');
const L = (y, s) => label(ex + 14, y + 5, s);
const R = (y, s) => label(ex + ew - 14, y + 5, s, 'p', 'end');
L(800, '5V'); L(850, `GPIO${pin.PIN_HMI_RX} RX1`); L(900, `GPIO${pin.PIN_HMI_TX} TX1`);
L(1090, `GPIO${pin.PIN_RS485_TX} TX2`); L(1130, `GPIO${pin.PIN_RS485_RX} RX2`); L(1170, `GPIO${pin.PIN_RS485_DE} DE/RE`); L(1040, 'GND');
wire(C.v5, [[395, 800], [ex, 800]], '+5V', [540, 794]);
wire(C.sig, [[620, 850], [ex, 850]]);
wire(C.sig, [[620, 900], [ex, 900]]);
wire(C.gnd, [[420, 1040], [ex, 1040]], 'GND', [465, 1034]);

// RS485
box(450, 1070, 170, 130, 'U2 MAX3485', 'RS485, VCC from 3V3');
label(535, 1136, 'A and B leave the top', 's', 'middle');
wire(C.sig, [[620, 1090], [ex, 1090]]);
wire(C.sig, [[620, 1130], [ex, 1130]]);
wire(C.sig, [[620, 1170], [ex, 1170]]);
wire(C.rs, [[590, 1070], [590, 1000], [700, 1000], [700, 660], [905, 660], [905, 340]], 'A to SG+', [800, 654]);
wire(C.rs, [[510, 1070], [510, 1020], [660, 1020], [660, 640], [945, 640], [945, 340]], 'B to SG-', [800, 634]);
label(40, 1290, 'RS485: twisted shielded pair. Join SGND (RJ45 pin 3) to GND. 120 ohm across A/B on long runs.', 's2');

// ---- Inputs -------------------------------------------------------------------------
box(1240, 700, 230, 410, 'OPTO INPUTS', '7 x PC817, 24 V field side', '#fff9ef', '#b8860b');
box(1590, 700, 400, 410, 'FIELD DEVICES (24 V)', '', '#fff');
const ins = [
  ['PIN_ESTOP_OK', 'E-stop NC2', 'closed = healthy', 760],
  ['PIN_GUARD_OK', 'Chain guard switch', 'closed = guard shut', 810],
  ['PIN_ENABLE', 'Local enable key', 'closed = enabled', 860],
  ['PIN_START_PB', 'START button (NO)', 'closed = pressed', 910],
  ['PIN_STOP_PB', 'STOP button (NC)', 'open = stop', 960],
  ['PIN_DRIVE_OK', 'VFD relay RB-RC', 'closed = no fault', 1010],
  ['PIN_RPM', 'RPM proximity sensor', 'optional, pulses', 1060],
];
for (const [k, name, note, y] of ins) {
  R(y, `GPIO${pin[k]}`);
  wire(C.sig, [[ex + ew, y], [1240, y]]);
  wire(C.v24, [[1470, y], [1590, y]]);
  label(1605, y + 2, name, 'p');
  label(1975, y + 2, note, 's2', 'end');
}
label(1355, 1090, '4.7 k series, 10 k pull-down', 's', 'middle');
wire(C.v24, [[430, 450], [445, 450], [445, 540], [1790, 540], [1790, 700]], '+24V field supply', [1200, 533]);
wire(C.gnd, [[430, 462], [436, 462], [436, 560], [1355, 560], [1355, 700]], '0V field', [1200, 553]);

// ---- Outputs -------------------------------------------------------------------------
box(1240, 1140, 230, 150, 'U3 ULN2003', 'Relay driver, 10 k pull-downs', '#fff9ef', '#b8860b');
R(1200, `GPIO${pin.PIN_RUN_PERMIT}`); R(1250, `GPIO${pin.PIN_HORN}`);
wire(C.sig, [[ex + ew, 1200], [1240, 1200]]);
wire(C.sig, [[ex + ew, 1250], [1240, 1250]]);
box(1590, 1140, 190, 80, 'K2 RUN PERMIT', 'Coil to +24V, NO contact');
box(1820, 1140, 170, 80, 'H1 HORN', 'Beacon, to +24V');
wire(C.v24, [[1470, 1180], [1590, 1180]]);
wire(C.v24, [[1470, 1260], [1905, 1260], [1905, 1220]]);
wire('#555', [[1650, 1140], [1650, 1124], [2100, 1124], [2100, 400], [672, 400], [672, 340]], 'K2 contact to MI1', [1500, 394]);
wire('#555', [[1720, 1140], [1720, 1132], [2120, 1132], [2120, 420], [722, 420], [722, 340]], 'K2 contact to DCM', [1500, 414]);
wire('#555', [[1990, 1000], [2040, 1000], [2040, 440], [780, 440], [780, 340]], 'Fault relay RB', [1500, 434]);
wire('#555', [[1990, 1020], [2060, 1020], [2060, 460], [822, 460], [822, 340]], 'Fault relay RC', [1500, 454]);

// ---- Notes -----------------------------------------------------------------------------
const nx = 40, ny = 1356;
box(nx, ny, 1000, 186, '', '', '#fafafa', '#bbb');
label(nx + 16, ny + 26, 'Before power-up', 'b');
[
  'E-stop opens K1 through hardware only. Firmware reads its state but is not part of the stop path.',
  'Measure display TX at idle: about -6 V means RS232 (use U1). 3.3 V means TTL: skip U1, wire direct.',
  'A 3.3 V ESP32 pin must never see RS232 levels. Confirm FFC pin numbers on your exact unit.',
  'Motor winding must suit 230 V 3-phase output: check the nameplate. Shield RS485, separate from motor cable.',
  'Pull-down holds K2 off while the ESP32 resets. Keep the factory S1/S2 links on the VFD unless you wire STO.',
  'Common ground: buck 0V, ESP32 GND, display GND and U1/U2 GND all join.',
].forEach((s, i) => label(nx + 16, ny + 50 + i * 21, s, 's2'));

box(1060, ny, 500, 186, '', '', '#fafafa', '#bbb');
label(1076, ny + 26, 'MS300 parameters', 'b');
[
  '00-20 = 1 (frequency from RS485)      00-21 = 2 (run from RS485)',
  '09-00 = 1      09-01 = 19.2      09-04 = 14 (8E1 RTU)',
  '09-02 = 1 (warn, ramp to stop)      09-03 = 1.0 s (comms time-out)',
  '02-01 = 49 (MI1 = drive enable)      02-13 = 11 (relay = fault)',
  '01-00 = 60 Hz      01-12 / 01-13 = 2 s (controller sets the real ramp)',
].forEach((s, i) => label(1076, ny + 50 + i * 21, s, 's2'));

box(1580, ny, 620, 186, '', '', '#fafafa', '#bbb');
label(1596, ny + 26, 'Wire colours', 'b');
[['Mains line', C.line], ['Mains neutral', C.neutral], ['Protective earth', C.earth], ['+24 V DC', C.v24], ['+5 V DC', C.v5],
  ['0 V / GND', C.gnd], ['3.3 V logic', C.sig], ['RS232', C.ttl], ['RS485', C.rs], ['Relay contact', '#555']].forEach(([n, c], i) => {
  const x = 1596 + (i % 2) * 300, y = ny + 48 + Math.floor(i / 2) * 22;
  out += `<line x1="${x}" y1="${y - 4}" x2="${x + 40}" y2="${y - 4}" stroke="${c}" stroke-width="3"/>`;
  label(x + 50, y, n, 's2');
});

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" font-family="Segoe UI,Arial,sans-serif">
<style>.h{font-size:30px;font-weight:700;fill:#16322a}.t{font-size:17px;font-weight:700;fill:#1c2a25;text-anchor:middle}.s{font-size:13px;fill:#50605a;text-anchor:middle}.s2{font-size:14px;fill:#3a4641}.p{font-size:14px;font-weight:600;fill:#1c2a25}.b{font-size:15px;font-weight:700;fill:#16322a}.w{font-size:13px;font-weight:600}</style>
${out}</svg>`;

fs.mkdirSync(path.join(root, 'docs'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/circuit_diagram.svg'), svg);

const require = createRequire(path.join(root, 'hmi/package.json'));
const puppeteer = require('puppeteer-core');
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', '/usr/bin/google-chrome'].find((p) => fs.existsSync(p));
const b = await puppeteer.launch({ executablePath: chrome, headless: true });
const p = await b.newPage();
await p.setViewport({ width: W, height: H, deviceScaleFactor: 1.5 });
await p.setContent(`<!doctype html><body style="margin:0">${svg}</body>`);
await p.screenshot({ path: path.join(root, 'docs/circuit_diagram.png') });
await b.close();
console.log('wrote docs/circuit_diagram.svg and .png; pins:', pin);
