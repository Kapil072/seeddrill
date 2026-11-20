// The same report the controller makes, built in the browser from cloud data.
// Lazy-loaded; base-14 fonts mean no font data and no PDF library to download.
const W = 595, H = 842;
const esc = (s) => String(s).replace(/[^\x20-\x7e]/g, '?').replace(/([()\\])/g, '\\$1');
const green = '0.11 0.30 0.20', lime = '0.42 0.71 0.18', ink = '0.10 0.13 0.12', grey = '0.45 0.50 0.48', line = '0.85 0.88 0.86';
const f1 = (x, d = 1, s = d) => (x / 10 ** s).toFixed(d);
const mmss = (s) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;

const text = (x, y, size, bold, s, rgb = ink) => `${rgb} rg BT /F${bold ? 2 : 1} ${size} Tf ${x} ${y} Td (${esc(s)}) Tj ET\n`;
const rect = (x, y, w, h, rgb) => `${rgb} rg ${x} ${y} ${w} ${h} re f\n`;
const rule = (x0, y0, x1, y1, rgb = line) => `${rgb} RG ${x0} ${y0} m ${x1} ${y1} l S\n`;

function chart(samples, x, y, w, h, top) {
  let o = '0.5 w\n';
  const ymax = Math.ceil((top / 10) * 1.2 / 10) * 10 || 120;
  for (let i = 0; i <= 4; i++) {
    o += rule(x, y + (h * i) / 4, x + w, y + (h * i) / 4);
    o += text(x - 22, y + (h * i) / 4 - 3, 8, false, Math.round((ymax * i) / 4), grey);
  }
  if (samples.length < 2) return o;
  const end = samples.at(-1)[0] || 1;
  const step = Math.ceil(samples.length / 240);
  const series = (col, stroke, style) => {
    let s = `${stroke} RG ${style}\n`, first = true;
    for (let i = 0; i < samples.length; i += step) {
      const r = samples[i];
      s += `${(x + (w * r[0]) / end).toFixed(1)} ${(y + (h * Math.min(r[col] / 10, ymax)) / ymax).toFixed(1)} ${first ? 'm' : 'l'}\n`;
      first = false;
    }
    return `${s}S\n`;
  };
  o += series(1, grey, '0.9 w [3 2] 0 d') + series(2, lime, '1.4 w [] 0 d') + '[] 0 d\n';
  return o + text(x, y - 12, 8, false, '0 s', grey) + text(x + w - 24, y - 12, 8, false, `${end} s`, grey);
}

export function reportPdf(t, samples, machine) {
  const p = t.profile ?? {};
  const when = new Date(t.start * 1000).toLocaleString('en-IN', { dateStyle: 'medium', timeStyle: 'short' });
  let c = rect(0, H - 84, W, 84, green);
  c += text(40, H - 44, 19, true, 'Seed Drill Field Simulation Report', '1 1 1');
  c += text(40, H - 65, 10, false, `SPK Engineers  |  Workshop test record  |  ${machine}`, '1 1 1');
  c += text(470, H - 44, 12, true, `Test #${t.id}`, '1 1 1') + text(470, H - 62, 9, false, when, '1 1 1');

  let y = H - 116;
  if (t.sim) {
    c += rect(40, y - 6, 515, 20, '0.96 0.72 0.24') + text(48, y, 10, true, 'SIMULATED DRIVE: no motor was run. Not a machine calibration record.');
    y -= 30;
  }
  const L = 40, R = 310, K = 118;
  const head = (x, yy, s) => text(x, yy, 11, true, s, green) + '0.8 w\n' + rule(x, yy - 5, x + 245, yy - 5);
  const kv = (x, yy, k, v) => text(x, yy, 9, false, k, grey) + text(x + K, yy, 9.5, true, v);
  c += head(L, y, 'Machine and settings') + head(R, y, 'Result');
  y -= 22;
  const left = [['Profile', p.name], ['Mode', p.mode ? 'Direct RPM' : 'Field emulation'], ['Wheel diameter', `${f1(p.wheelMm, 3)} m`],
    ['Rig coupled to', p.couple ? 'Metering shaft' : 'Wheel axle'], ['Gearbox', `${f1(p.gear, 2)} : 1`], ['Target output', `${f1(t.rpmT)} RPM`], ['Drive frequency', `${f1(t.hzT, 2)} Hz`]];
  const right = [['Outcome', t.end], ['Duration', `${mmss(t.dur)} min`], ['Distance', `${f1(t.dist)} m (calculated)`], ['Average output', `${f1(t.rpmAvg)} RPM (${t.meas ? 'measured' : 'estimated'})`],
    ['Range', `${f1(t.rpmMin)} to ${f1(t.rpmMax)} RPM`], ['Speed accuracy', `${f1(t.acc)} %`], ['Motor current', `${f1(t.aPeak, 2)} A peak, ${f1(t.aAvg, 2)} A avg`]];
  left.forEach(([k, v], i) => { c += kv(L, y - 16 * i, k, v); });
  right.forEach(([k, v], i) => { c += kv(R, y - 16 * i, k, v); });
  y -= 16 * Math.max(left.length, right.length) + 10;

  c += head(L, y, 'Output speed over time (RPM)');
  y -= 176;
  c += chart(samples, L + 22, y, 493, 160, Math.max(t.rpmT, t.rpmMax));

  y -= 40;
  c += head(L, y, 'Seed and fertilizer output') + head(R, y, 'Sign-off');
  y -= 22;
  const rate = (g) => { const a = (t.dist / 10) * p.rows * (p.rowSp / 1000); return a > 0 ? ((g / 1000) / (a / 10000)).toFixed(1) : '0.0'; };
  if (t.seed || t.fert) {
    c += kv(L, y, 'Seed collected', `${f1(t.seed)} g`) + kv(L, y - 16, 'Seed rate', `${rate(t.seed / 10)} kg/ha`);
    c += kv(L, y - 32, 'Fertilizer collected', `${f1(t.fert)} g`) + kv(L, y - 48, 'Fertilizer rate', `${rate(t.fert / 10)} kg/ha`);
  } else c += text(L, y, 9, false, 'No measured output entered.', grey);
  c += text(R, y, 9, false, 'Operator:', grey) + text(R, y - 30, 9, false, 'Checked by:', grey);
  c += rule(R + 62, y - 2, R + 245, y - 2) + rule(R + 62, y - 32, R + 245, y - 32);
  c += text(L, 34, 7.5, false, t.meas ? 'RPM from independent sensor. Distance is calculated.' : 'RPM is estimated from VFD frequency, not measured at the shaft. Distance is calculated. Verify with a tachometer.', grey);

  const objs = [
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Count 1 /Kids [5 0 R] >>',
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>',
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${W} ${H}] /Contents 6 0 R /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> >>`,
    `<< /Length ${c.length} >>\nstream\n${c}\nendstream`,
  ];
  let out = '%PDF-1.4\n';
  const at = objs.map((o, i) => { const pos = out.length; out += `${i + 1} 0 obj\n${o}\nendobj\n`; return pos; });
  const x = out.length;
  out += `xref\n0 ${objs.length + 1}\n0000000000 65535 f \n${at.map((a) => `${String(a).padStart(10, '0')} 00000 n \n`).join('')}`;
  return out + `trailer\n<< /Size ${objs.length + 1} /Root 1 0 R >>\nstartxref\n${x}\n%%EOF\n`;
}
