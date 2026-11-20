// Every HMI screen, authored once. capture.mjs reads the data-* attributes to
// produce backgrounds, icon strips and the control tables; nothing about a
// screen is defined anywhere else.
//
//   data-key="NAME"            touch area -> writes key code to VP KEY
//   num(name,int,dec,size)     digit cells, one VP word each (see DIGIT_* in hmi_map.h)
//   st(name,w,h,[states])      variable icon, value selects a state
//   txt(name,len,font,...)     text from the display's built-in ASCII font
//   data-curve / data-qr       native trend curve and QR controls

const S = (d, extra = '') => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" ${extra}>${d}</svg>`;
export const I = {
  back: S('<path d="M15 5l-7 7 7 7"/>'),
  chev: S('<path d="M9 5l7 7-7 7"/>'),
  seed: S('<path d="M12 21V11"/><path d="M12 12C12 7 8 4 3 4c0 5 4 8 9 8z"/><path d="M12 15c0-4 3.5-7 9-7 0 4.500-3.500 7-9 7z"/>'),
  play: S('<path d="M7 4.500v15l13-7.500z" fill="currentColor"/>'),
  stop: S('<rect x="5" y="5" width="14" height="14" rx="2" fill="currentColor"/>'),
  sliders: S('<path d="M4 7h10M18 7h2M4 17h2M10 17h10"/><circle cx="16" cy="7" r="2.400"/><circle cx="8" cy="17" r="2.400"/>'),
  layers: S('<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/>'),
  file: S('<path d="M6 3h8l5 5v13H6z"/><path d="M14 3v5h5M9 13h7M9 17h7"/>'),
  gear: S('<circle cx="12" cy="12" r="3.200"/><path d="M12 2.500v3M12 18.500v3M2.500 12h3M18.500 12h3M5.300 5.300l2.100 2.100M16.600 16.600l2.100 2.100M18.700 5.300l-2.100 2.100M7.400 16.600l-2.100 2.100"/>'),
  wifi: S('<path d="M2.500 9a14 14 0 0119 0"/><path d="M6 12.800a9 9 0 0112 0"/><path d="M9.300 16.300a4 4 0 015.400 0"/><circle cx="12" cy="19.500" r=".8" fill="currentColor"/>'),
  cloud: S('<path d="M7 18a4.500 4.500 0 01-.6-8.960A6 6 0 0118 9.500a4.300 4.300 0 01-.5 8.500z"/>'),
  check: S('<path d="M5 12.500l4.500 4.500L19 7.500"/>'),
  x: S('<path d="M6 6l12 12M18 6L6 18"/>'),
  warn: S('<path d="M12 3.500L22 20.500H2z"/><path d="M12 10v4.500M12 17.500v.1"/>'),
  chain: S('<rect x="2.500" y="8.500" width="9" height="7" rx="3.500"/><rect x="12.500" y="8.500" width="9" height="7" rx="3.500"/>'),
  drive: S('<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M8 8h8M8 12h3M8 16h3"/><circle cx="15.500" cy="14" r="2"/>'),
  sun: S('<circle cx="12" cy="12" r="4"/><path d="M12 2.500v2M12 19.500v2M2.500 12h2M19.500 12h2M5.300 5.300l1.400 1.400M17.300 17.300l1.400 1.400M18.700 5.300l-1.400 1.400M6.700 17.300l-1.400 1.400"/>'),
  pulse: S('<path d="M2.500 12.500h4l2.500-7 5 14 2.500-7h5"/>'),
  info: S('<circle cx="12" cy="12" r="9"/><path d="M12 11v5.500M12 7.500v.1"/>'),
  remote: S('<path d="M12 21v-9"/><circle cx="12" cy="9.500" r="2.500"/><path d="M6.500 15a7.500 7.500 0 010-11M17.500 4a7.500 7.500 0 010 11"/>'),
  up: S('<path d="M6 15l6-6 6 6"/>'),
  down: S('<path d="M6 9l6 6 6-6"/>'),
  refresh: S('<path d="M20 11a8 8 0 00-14.500-4L3 9.500M3 4v5.500h5.500M4 13a8 8 0 0014.500 4l2.500-2.500M21 20v-5.500h-5.500"/>'),
  save: S('<path d="M5 3h11l3 3v15H5z"/><path d="M8 3v5h7V3M8 21v-7h8v7"/>'),
  trash: S('<path d="M4 7h16M9 7V4h6v3M6 7l1 14h10l1-14"/>'),
  lock: S('<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 018 0v3"/>'),
  shield: S('<path d="M12 3l8 3v6c0 5-3.500 8-8 9-4.500-1-8-4-8-9V6z"/><path d="M8.500 12l2.500 2.500 4.500-5"/>'),
  key: S('<circle cx="8" cy="15" r="4.500"/><path d="M11.500 11.500L20 3M16 7l3 3"/>'),
  plug: S('<path d="M9 3v6M15 3v6M6 9h12v3a6 6 0 01-12 0zM12 18v3"/>'),
  target: S('<circle cx="12" cy="12" r="8.500"/><circle cx="12" cy="12" r="3.500"/>'),
  plus: S('<path d="M12 5v14M5 12h14"/>'),
  minus: S('<path d="M5 12h14"/>'),
  eye: S('<path d="M2.500 12S6 5.500 12 5.500 21.500 12 21.500 12 18 18.500 12 18.500 2.500 12 2.500 12z"/><circle cx="12" cy="12" r="3"/>'),
  bksp: S('<path d="M9 5h11v14H9l-6-7z"/><path d="M12.500 9.500l5 5M17.500 9.500l-5 5"/>'),
  shift: S('<path d="M12 4l8 8h-4.500v7h-7v-7H4z"/>'),
  doc: S('<path d="M6 3h8l5 5v13H6z"/><path d="M14 3v5h5"/>'),
  upload: S('<path d="M12 16V5M7 10l5-5 5 5M5 20h14"/>'),
};

export const num = (name, int, dec, size, cls = '') => {
  const cell = (i) => `<i class="cell" data-cell="${name}" data-i="${i}"></i>`;
  let h = '';
  for (let i = 0; i < int; i++) h += cell(i);
  if (dec) { h += '<i class="dot">.</i>'; for (let i = int; i < int + dec; i++) h += cell(i); }
  return `<span class="num ${size} ${cls}" data-num="${name}" data-int="${int}" data-dec="${dec}">${h}</span>`;
};
// Clock-style field: two fixed groups around a static colon
export const mmss = (name, size, cls = '') =>
  `<span class="num ${size} ${cls}" data-num="${name}" data-int="4" data-dec="0">` +
  `<i class="cell" data-cell="${name}" data-i="0"></i><i class="cell" data-cell="${name}" data-i="1"></i><i class="dot">:</i>` +
  `<i class="cell" data-cell="${name}" data-i="2"></i><i class="cell" data-cell="${name}" data-i="3"></i></span>`;
export const txt = (name, len, font, { color = '#eef4f0', align = 'l', w } = {}) => {
  const width = w ?? len * font / 2;
  return `<span class="txt" style="width:${width}px;height:${font + 2}px;font-size:${font}px;color:${color}" data-text="${name}" data-len="${len}" data-font="${font}" data-color="${color}" data-align="${align}"></span>`;
};
export const st = (name, w, h, states, cls = '') =>
  `<div class="st ${cls}" style="width:${w}px;height:${h}px" data-state="${name}" data-n="${states.length}">${states.map((s) => `<div class="s">${s}</div>`).join('')}</div>`;
const pill = (kind, t) => `<div class="pill ${kind}"><span class="dotc"></span>${t}</div>`;

const hdr = (title, { back = true } = {}) => `
<div class="hdr">
  ${back ? `<div class="back" data-key="BACK">${I.back}</div>` : `<div class="brand">${S('<path d="M12 21V11"/><path d="M12 12C12 7 8 4 3 4c0 5 4 8 9 8z"/><path d="M12 15c0-4 3.500-7 9-7 0 4.500-3.500 7-9 7z"/>', 'width="34" height="34"')}</div>`}
  <h1>${title}</h1>
  ${st('H_SIM', 118, 32, ['<div></div>', pill('warn', 'SIMULATION')])}
  ${st('H_REMOTE', 100, 32, ['<div></div>', pill('info', 'REMOTE')])}
  ${st('H_WIFI', 40, 36, [0, 1, 2, 3, 4].map((b) => `<div class="ico" style="color:${b ? 'var(--acc)' : 'var(--dim)'};opacity:${b ? 0.55 + b * 0.11 : 1}">${I.wifi}</div>`))}
  ${st('H_CLOUD', 40, 36, ['var(--dim)', 'var(--amb)', 'var(--acc)'].map((c) => `<div class="ico" style="color:${c}">${I.cloud}</div>`))}
  ${txt('H_CLOCK', 5, 24, { color: '#93a79e', align: 'r' })}
</div>`;

const field = (key, label, value, unit = '') => `
<div class="field" data-key="${key}">
  <div class="lbl">${label}</div>
  <div class="row center" style="gap:0">${value}${unit ? `<span class="unit">${unit}</span>` : ''}<span class="chev">${I.chev}</span></div>
</div>`;
const stat = (label, value, unit = '') =>
  `<div><div class="lbl">${label}</div><div class="row end" style="gap:0;margin-top:6px">${value}${unit ? `<span class="unit">${unit}</span>` : ''}</div></div>`;
const seg2 = (name, keyA, keyB, a, b, h = 64) => {
  const w = 'width:100%;height:100%';
  const on = (x, y) => `<div class="seg" style="${w};height:${h}px"><i class="${x}">${a}</i><i class="${y}">${b}</i></div>`;
  return `<div style="position:relative;height:${h}px">${st(name, 0, h, [on('sel', ''), on('', 'sel')], 'fullw')}
    <div style="position:absolute;inset:0;display:flex"><div class="grow" data-key="${keyA}"></div><div class="grow" data-key="${keyB}"></div></div></div>`;
};

// ---- pages ---------------------------------------------------------------
export const PAGES = [];
const page = (id, html) => PAGES.push({ id, html: `<div class="page">${html}</div>` });

page('BOOT', `
<div style="position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:20px">
  <div style="width:124px;height:124px;border-radius:30px;background:linear-gradient(145deg,#a9ec60,#5fa628);display:flex;align-items:center;justify-content:center;color:#0d1a08">
    ${S('<path d="M12 21V11"/><path d="M12 12C12 7 8 4 3 4c0 5 4 8 9 8z"/><path d="M12 15c0-4 3.500-7 9-7 0 4.500-3.500 7-9 7z"/>', 'width="80" height="80"')}
  </div>
  <div style="font:800 44px Inter;letter-spacing:-1px">Seed Drill Field Simulator</div>
  <div style="font:600 19px Inter;color:var(--mut);letter-spacing:.5px">SPK ENGINEERS &nbsp;·&nbsp; WORKSHOP TEST SYSTEM</div>
  <div style="margin-top:22px">${st('BOOT_BAR', 420, 8, Array.from({ length: 11 }, (_, i) => `<div style="width:100%;height:8px;border-radius:4px;background:#1f2c27"><div style="width:${i * 10}%;height:8px;border-radius:4px;background:var(--acc)"></div></div>`))}</div>
  <div>${txt('BOOT_MSG', 40, 20, { color: '#93a79e', align: 'c' })}</div>
</div>`);

page('HOME', `
${hdr('Field Simulator', { back: false })}
<div class="main col">
  <div class="row" style="height:306px">
    <div class="card grow col" style="justify-content:space-between">
      <div class="row between center">
        <div><div class="cap">Active machine</div><div style="margin-top:6px">${txt('PROFILE', 24, 32)}</div></div>
        ${st('STATE', 138, 36, [pill('idle', 'READY'), pill('info', 'CHECKING'), pill('ok', 'RUNNING'), pill('warn', 'STOPPING'), pill('ok', 'COMPLETE'), pill('bad', 'FAULT')])}
      </div>
      <div>${txt('CROP', 44, 24, { color: '#93a79e' })}</div>
      <div class="row" style="gap:34px">
        ${stat('Travel speed', num('SPEED', 3, 1, 'lg', 'c-acc'), 'km/h')}
        ${stat('Gearbox output', num('OUT_RPM', 3, 1, 'lg'), 'RPM')}
      </div>
      <div class="row" style="gap:34px">
        ${stat('Drive frequency', num('HZ_CMD', 2, 2, 'md'), 'Hz')}
        ${stat('Test length', num('LENGTH', 4, 1, 'md'), '')}
        <div style="align-self:flex-end;margin-left:-26px">${st('LENGTH_U', 60, 26, ['<div class="unit" style="margin:0">min</div>', '<div class="unit" style="margin:0">m</div>'])}</div>
      </div>
    </div>
    <div class="col" style="width:318px">
      <div class="btn pri grow" data-key="GO_PRE" style="flex-direction:column;font-size:30px;font-weight:800">${S('<path d="M7 4.500v15l13-7.500z" fill="currentColor"/>', 'style="width:64px;height:64px"')}START TEST</div>
      <div class="btn" data-key="GO_SETUP" style="height:80px;flex:none">${I.sliders}Change settings</div>
    </div>
  </div>
  <div class="row grow">
    <div class="tile grow" data-key="GO_PROFILES">${I.layers}Machine profiles</div>
    <div class="tile grow" data-key="GO_CROP">${I.seed}Crop &amp; seed</div>
    <div class="tile grow" data-key="GO_REPORTS">${I.file}Test reports</div>
    <div class="tile grow" data-key="GO_SETTINGS">${I.gear}Settings</div>
  </div>
</div>`);

page('SETUP', `
${hdr('Test Setup')}
<div class="main row">
  <div class="col" style="width:520px">
    ${seg2('MODE', 'MODE_FIELD', 'MODE_RPM', 'Field emulation', 'Direct RPM')}
    <div class="field" data-key="F_SPEED">
      ${st('SPEED_L', 170, 22, ['<div class="lbl">Travel speed</div>', '<div class="lbl">Output shaft speed</div>'])}
      <div class="row center" style="gap:0">${num('F_SPEED_V', 3, 1, 'md', 'c-acc')}<div style="width:64px;margin-left:6px">${st('SPEED_U', 64, 22, ['<div class="unit" style="margin:0">km/h</div>', '<div class="unit" style="margin:0">RPM</div>'])}</div><span class="chev">${I.chev}</span></div>
    </div>
    ${field('F_WHEEL', 'Ground wheel diameter', num('F_WHEEL_V', 1, 3, 'md'), 'm')}
    ${seg2('STOPBY', 'STOP_TIME', 'STOP_DIST', 'Stop by time', 'Stop by distance')}
    <div class="field" data-key="F_LENGTH">
      ${st('LEN_L', 170, 22, ['<div class="lbl">Test duration</div>', '<div class="lbl">Simulated distance</div>'])}
      <div class="row center" style="gap:0">${num('F_LENGTH_V', 4, 1, 'md')}<div style="width:44px;margin-left:6px">${st('LEN_U', 44, 22, ['<div class="unit" style="margin:0">min</div>', '<div class="unit" style="margin:0">m</div>'])}</div><span class="chev">${I.chev}</span></div>
    </div>
    <div class="row grow">
      <div class="btn grow" data-key="GO_TRANS">${I.chain}Transmission</div>
      <div class="btn grow" data-key="GO_CROP">${I.seed}Crop</div>
    </div>
  </div>
  <div class="col grow">
    <div class="card col grow" style="gap:0;justify-content:space-between">
      <div class="row between center"><div class="cap">Calculated output</div>
        ${st('VALID', 150, 30, [pill('ok', 'WITHIN LIMITS'), pill('bad', 'OUT OF RANGE')])}</div>
      ${[['Ground wheel', 'C_WHEEL', 3, 1, 'RPM'], ['Driven shaft', 'C_SHAFT', 3, 1, 'RPM'], ['Gearbox output', 'C_OUT', 3, 1, 'RPM'], ['Motor', 'C_MOTOR', 4, 0, 'RPM'], ['VFD frequency', 'C_HZ', 2, 2, 'Hz']].map(([l, n, a, b, u], i) => `
      <div class="row between center" style="height:48px;border-top:1px solid var(--line)">
        <div class="lbl">${l}</div><div class="row end" style="gap:0">${num(n, a, b, 'md', i === 2 ? 'c-acc' : '')}<span class="unit" style="width:44px">${u}</span></div>
      </div>`).join('')}
      <div style="height:26px;margin-top:6px">${txt('VALID_MSG', 44, 20, { color: '#f6b73c', w: 440 })}</div>
    </div>
    <div class="row" style="height:68px">
      <div class="btn" data-key="SAVE_PROFILE" style="width:150px">${I.save}Save</div>
      <div class="btn pri grow" data-key="GO_PRE">Next: Pre-check${I.chev}</div>
    </div>
  </div>
</div>`);

const teeth = (key, label, name) => field(key, label, num(name, 3, 0, 'md'), 'T');
page('TRANS', `
${hdr('Transmission')}
<div class="main row">
  <div class="col" style="width:640px">
    <div class="lbl">Rig is coupled to</div>
    ${seg2('COUPLE', 'CPL_WHEEL', 'CPL_SHAFT', 'Ground-wheel axle', 'Metering shaft')}
    <div class="row">
      <div class="col grow"><div class="cap">Stage 1 (wheel side)</div>${teeth('F_S1A', 'Driver', 'F_S1A_V')}${teeth('F_S1B', 'Driven', 'F_S1B_V')}</div>
      <div class="col grow"><div class="cap">Stage 2 (0 = none)</div>${teeth('F_S2A', 'Driver', 'F_S2A_V')}${teeth('F_S2B', 'Driven', 'F_S2B_V')}</div>
    </div>
    <div class="cap">Test rig adapter</div>
    <div class="row">
      <div class="grow">${teeth('F_RA', 'Rig sprocket', 'F_RA_V')}</div>
      <div class="grow">${teeth('F_RB', 'Machine sprocket', 'F_RB_V')}</div>
    </div>
  </div>
  <div class="col grow">
    <div class="card col" style="gap:14px">
      <div class="cap">Ratios</div>
      ${stat('Wheel to shaft', num('C_RATIO', 2, 3, 'md', 'c-acc'), ': 1')}
      ${stat('Rig to machine', num('C_RIG', 2, 3, 'md'), ': 1')}
    </div>
    <div class="card col" style="gap:10px">
      <div class="cap">Seeding width</div>
      ${field('F_ROWS', 'Rows / shovels', num('F_ROWS_V', 2, 0, 'md'))}
      ${field('F_ROWSP', 'Row spacing', num('F_ROWSP_V', 3, 1, 'md'), 'cm')}
    </div>
    <div class="btn pri grow" data-key="BACK">${I.check}Done</div>
  </div>
</div>`);

const cropRow = (i, name, sub) => `
<div class="rowbtn" data-key="CROP${i}" style="height:76px;justify-content:space-between">
  <div><div style="font:700 21px Inter">${name}</div><div class="lbl" style="margin-top:3px">${sub}</div></div>
  ${st(`CROP${i}_SEL`, 34, 34, [`<div style="width:30px;height:30px;border-radius:50%;border:2px solid var(--line);margin:2px"></div>`, `<div style="width:32px;height:32px;border-radius:50%;background:var(--acc);color:#0d1a08;display:flex;align-items:center;justify-content:center;padding:6px">${I.check}</div>`])}
</div>`;
page('CROP', `
${hdr('Crop &amp; Seed')}
<div class="main row">
  <div class="col" style="width:510px;gap:10px">
    ${cropRow(0, 'Maize / Corn', '20 cm spacing · 24 cells')}
    ${cropRow(1, 'Soybean', '5 cm spacing · 30 cells')}
    ${cropRow(2, 'Chickpea (Gram)', '10 cm spacing · 12 cells')}
    ${cropRow(3, 'Mustard', '3 cm spacing · 60 cells')}
    ${cropRow(4, 'Custom crop', 'Set spacing and cells')}
    <div class="grow"></div>
  </div>
  <div class="col grow">
    ${field('F_SPACING', 'Seed spacing', num('F_SPACING_V', 3, 1, 'md', 'c-acc'), 'cm')}
    ${field('F_CELLS', 'Metering disc cells', num('F_CELLS_V', 3, 0, 'md', 'c-acc'))}
    <div class="card col grow" style="justify-content:space-around;gap:6px">
      <div class="cap">At current travel speed</div>
      ${[['Seed drop rate', 'K_FREQ', 3, 1, 'seeds/s'], ['Drop interval', 'K_INT', 4, 1, 'ms'], ['Required disc speed', 'K_DISC', 3, 1, 'RPM']].map(([l, n, a, b, u]) => `
      <div class="row between center"><div class="lbl">${l}</div><div class="row end" style="gap:0">${num(n, a, b, 'md')}<span class="unit" style="width:68px">${u}</span></div></div>`).join('')}
    </div>
    <div class="btn pri" data-key="BACK" style="height:66px;flex:none">${I.check}Done</div>
  </div>
</div>`);

const nk = (k, l, cls = '') => `<div class="kk ${cls}" data-key="${k}">${l}</div>`;
page('NUMPAD', `
${hdr('Enter Value')}
<div class="main row">
  <div class="col" style="width:470px">
    <div class="card col grow" style="justify-content:center;gap:14px">
      <div>${txt('NP_TITLE', 34, 24, { color: '#93a79e', w: 430 })}</div>
      <div style="background:#0e1714;border:2px solid var(--acc-d);border-radius:14px;padding:18px 16px;height:104px;display:flex;align-items:center">${txt('NP_VALUE', 12, 64, { align: 'r', w: 400 })}</div>
      <div class="row between center"><div>${txt('NP_RANGE', 26, 20, { color: '#62756c', w: 280 })}</div><div>${txt('NP_UNIT', 8, 24, { color: '#93a79e', align: 'r', w: 100 })}</div></div>
      <div style="height:26px">${txt('NP_ERR', 38, 20, { color: '#f0524a', w: 430 })}</div>
    </div>
  </div>
  <div class="grow" style="display:grid;grid-template-columns:repeat(3,1fr);grid-template-rows:repeat(4,1fr) 80px;gap:10px">
    ${['7', '8', '9', '4', '5', '6', '1', '2', '3'].map((d) => nk(`CH_${d.charCodeAt(0).toString(16)}`, d)).join('')}
    ${nk('CH_2e', '.')}${nk('CH_30', '0')}${nk('KB_BKSP', I.bksp.replace('<svg', '<svg width="32" height="32"'), 'fn')}
    ${nk('NP_CLEAR', 'Clear', 'fn')}${nk('NP_CANCEL', 'Cancel', 'fn')}${nk('NP_OK', 'OK', 'ok')}
  </div>
</div>`);

const chk = (i, label, sub) => `
<div class="rowbtn" style="height:68px;background:var(--card)">
  ${st(`PRE${i}`, 42, 42, [
    `<div style="width:40px;height:40px;border-radius:50%;background:#40201e;color:var(--red);display:flex;align-items:center;justify-content:center;padding:8px">${I.x}</div>`,
    `<div style="width:40px;height:40px;border-radius:50%;background:#20391e;color:var(--acc);display:flex;align-items:center;justify-content:center;padding:8px">${I.check}</div>`])}
  <div><div style="font:700 20px Inter">${label}</div><div class="lbl" style="font-size:14px;margin-top:2px">${sub}</div></div>
</div>`;
page('PRE', `
${hdr('Pre-start Check')}
<div class="main row">
  <div class="col grow" style="gap:9px">
    ${chk(0, 'Emergency stop released', 'Hardwired E-stop circuit healthy')}
    ${chk(1, 'Guards closed', 'Chain and sprocket guard switch')}
    ${chk(2, 'Local enable on', 'Key switch on panel')}
    ${chk(3, 'Drive ready', 'VFD online, no fault')}
    ${chk(4, 'Target within limits', 'Speed inside the configured range')}
    <div class="rowbtn" data-key="PRE_CONFIRM" style="height:84px">
      ${st('PRE5', 42, 42, [`<div style="width:38px;height:38px;border-radius:9px;border:3px solid var(--amb);margin:2px"></div>`,
        `<div style="width:40px;height:40px;border-radius:9px;background:var(--acc);color:#0d1a08;display:flex;align-items:center;justify-content:center;padding:6px">${I.check}</div>`])}
      <div><div style="font:700 20px Inter">I confirm the machine is secured</div><div class="lbl" style="font-size:14px;margin-top:2px">Drill and rig fixed in place, area clear of people</div></div>
    </div>
  </div>
  <div class="col" style="width:330px">
    <div class="card col grow" style="justify-content:space-between">
      <div class="cap">Test to run</div>
      <div>${txt('PROFILE', 24, 24, { w: 296 })}</div>
      ${stat('Target output', num('OUT_RPM', 3, 1, 'lg', 'c-acc'), 'RPM')}
      ${stat('Drive frequency', num('HZ_CMD', 2, 2, 'md'), 'Hz')}
    </div>
    ${st('PRE_GO', 330, 118, [
      `<div class="btn" style="width:100%;height:100%;font-size:26px;color:var(--dim);background:#141d1a;flex-direction:column;gap:4px">${I.lock}Checks not met</div>`,
      `<div class="btn pri" style="width:100%;height:100%;font-size:30px;font-weight:800">${S('<path d="M7 4.500v15l13-7.500z" fill="currentColor"/>', 'style="width:40px;height:40px"')}START</div>`])}
    <div data-key="RUN_START" style="position:absolute;right:16px;bottom:14px;width:330px;height:118px"></div>
  </div>
</div>`);

const GAUGE_N = 61, GAUGE_MAX = 120;
const arc = (frac, color, w) => {
  const r = 150, c = 2 * Math.PI * r, span = 0.75;
  return `<circle cx="170" cy="170" r="${r}" fill="none" stroke="${color}" stroke-width="${w}" stroke-linecap="round"
    stroke-dasharray="${c * span * frac} ${c}" transform="rotate(135 170 170)"/>`;
};
const gauge = (i) => {
  const f = i / (GAUGE_N - 1);
  return `<svg width="340" height="340" viewBox="0 0 340 340">${arc(1, '#22302a', 20)}${i ? arc(f, 'url(#gg)', 20) : ''}
  <defs><linearGradient id="gg" x1="0" y1="1" x2="1" y2="0"><stop offset="0" stop-color="#5fa628"/><stop offset="1" stop-color="#b4f06b"/></linearGradient></defs></svg>`;
};
page('RUN', `
${hdr('Test Running', { back: false })}
<div class="main row">
  <div class="col" style="width:372px">
    <div class="card" style="position:relative;height:364px;padding:0;display:flex;align-items:center;justify-content:center">
      ${st('GAUGE', 340, 340, Array.from({ length: GAUGE_N }, (_, i) => gauge(i)))}
      <div style="position:absolute;left:0;right:0;top:92px;text-align:center" class="col center">
        ${st('RPM_SRC', 170, 22, ['<div class="lbl" style="width:100%;text-align:center">OUTPUT RPM (EST.)</div>', '<div class="lbl" style="width:100%;text-align:center">OUTPUT RPM (MEAS.)</div>'])}
        ${num('RPM', 3, 1, 'xl')}
        <div class="row center" style="gap:6px;margin-top:-4px"><span class="lbl">Target</span>${num('RPM_T', 3, 1, 'sm', 'c-acc')}</div>
      </div>
    </div>
    <div class="btn dan grow" data-key="RUN_STOP" style="font-size:34px;font-weight:800;letter-spacing:1px">${S('<rect x="5" y="5" width="14" height="14" rx="2" fill="currentColor"/>', 'style="width:40px;height:40px"')}STOP</div>
  </div>
  <div class="col grow">
    <div class="row">
      <div class="card grow">${stat('Simulated speed', num('V_SIM', 2, 1, 'lg', 'c-acc'), 'km/h')}</div>
      <div class="card grow">${stat('Distance', num('DIST', 5, 1, 'lg'), 'm')}</div>
    </div>
    <div class="card" style="padding:12px 16px">
      <div class="row between center">
        <div class="row center" style="gap:10px"><span class="lbl">Elapsed</span>${mmss('T_ELAPSED', 'md')}</div>
        <div class="row center" style="gap:10px"><span class="lbl">Remaining</span>${mmss('T_LEFT', 'md', 'c-acc')}</div>
      </div>
      <div style="margin-top:10px">${st('PROG', 556, 12, Array.from({ length: 51 }, (_, i) => `<div style="width:100%;height:12px;border-radius:6px;background:#0e1714"><div style="width:${i * 2}%;height:12px;border-radius:6px;background:var(--acc)"></div></div>`))}</div>
    </div>
    <div class="row">
      <div class="card grow" style="padding:10px 14px">${stat('Current', num('AMPS', 2, 1, 'md'), 'A')}</div>
      <div class="card grow" style="padding:10px 14px">${stat('Drive', num('HZ_OUT', 2, 2, 'md'), 'Hz')}</div>
      <div class="card grow" style="padding:10px 14px">${stat('Torque', num('TORQUE', 3, 0, 'md'), '%')}</div>
      <div class="card grow" style="padding:10px 14px">${stat('DC bus', num('VBUS', 3, 0, 'md'), 'V')}</div>
    </div>
    <div class="card grow" style="padding:8px 12px;position:relative">
      <div class="cap" style="position:absolute;left:14px;top:8px">Speed trend</div>
      <div data-curve="0" data-color="#9ae34f" data-min="0" data-max="120" style="position:absolute;left:12px;right:12px;top:28px;bottom:8px;border-bottom:1px solid var(--line)"></div>
    </div>
  </div>
</div>`);

page('DONE', `
${hdr('Test Report', { back: false })}
<div class="main row">
  <div class="card col grow" style="gap:8px;justify-content:space-between">
    <div class="row between center">
      <div>${txt('R_TITLE', 26, 24)}</div>
      ${st('R_STATE', 148, 34, [pill('ok', 'COMPLETED'), pill('warn', 'STOPPED'), pill('bad', 'FAULTED'), pill('warn', 'INTERRUPTED')])}
    </div>
    <div>${txt('R_SUB', 46, 20, { color: '#93a79e' })}</div>
    <div class="row" style="gap:28px">
      ${stat('Average output', num('R_RPM', 3, 1, 'lg', 'c-acc'), 'RPM')}
      ${stat('Speed accuracy', num('R_ACC', 3, 1, 'lg'), '%')}
    </div>
    <div class="row" style="gap:28px">
      ${stat('Duration', mmss('R_TIME', 'md'), '')}
      ${stat('Distance', num('R_DIST', 5, 1, 'md'), 'm')}
      ${stat('Peak current', num('R_AMPS', 2, 1, 'md'), 'A')}
    </div>
    <div class="row">
      ${st('R_SYNC', 300, 30, [pill('idle', 'SAVED ON DEVICE'), pill('info', 'UPLOADING TO CLOUD'), pill('ok', 'UPLOADED TO CLOUD')])}
    </div>
  </div>
  <div class="col" style="width:420px">
    <div class="card col" style="gap:10px">
      <div class="cap">Measured output (weigh by hand)</div>
      ${field('F_SEED', 'Seed collected', num('F_SEED_V', 5, 1, 'sm'), 'g')}
      ${field('F_FERT', 'Fertilizer collected', num('F_FERT_V', 5, 1, 'sm'), 'g')}
      <div class="row between" style="margin-top:2px">
        ${stat('Seed rate', num('R_SEED', 4, 1, 'sm', 'c-acc'), 'kg/ha')}
        ${stat('Fertilizer', num('R_FERT', 4, 1, 'sm', 'c-acc'), 'kg/ha')}
      </div>
    </div>
    <div class="row grow">
      <div class="card" style="width:190px;padding:10px;display:flex;flex-direction:column;align-items:center;gap:6px">
        <div data-qr="R_QR" data-unit="3" style="width:141px;height:141px;background:#fff;border-radius:6px"></div>
        <div class="lbl" style="font-size:12px;text-align:center">Scan on same Wi-Fi<br>for PDF report</div>
      </div>
      <div class="col grow">
        <div class="btn grow" data-key="R_SAVE">${I.save}Save</div>
        <div class="btn pri grow" data-key="R_HOME">Finish</div>
      </div>
    </div>
  </div>
</div>`);

const listRow = (i, prefix, right) => `
<div class="rowbtn" data-key="${prefix}${i}" style="height:64px">${right}</div>`;
page('PROFILES', `
${hdr('Machine Profiles')}
<div class="main col" style="gap:10px">
  ${[0, 1, 2, 3, 4, 5].map((i) => listRow(i, 'PF', `
    ${st(`PF${i}_ON`, 26, 26, ['<div></div>', `<div style="width:24px;height:24px;border-radius:50%;background:var(--acc);margin:1px"></div>`])}
    ${txt(`PF${i}_T`, 66, 24, { w: 860 })}`)).join('')}
  <div class="row grow">
    <div class="btn grow" data-key="PF_LOAD">${I.upload}Load selected</div>
    <div class="btn grow" data-key="PF_SAVE">${I.save}Save current here</div>
    <div class="btn grow" data-key="PF_RENAME">${I.doc}Rename</div>
    <div class="btn grow" data-key="PF_DELETE">${I.trash}Delete</div>
    <div class="btn" data-key="PF_PREV" style="width:76px">${I.up}</div>
    <div class="btn" data-key="PF_NEXT" style="width:76px">${I.down}</div>
  </div>
</div>`);

const pager = (a, b, lbl) => `
<div class="row center" style="gap:10px"><div class="btn" data-key="${a}" style="width:84px">${I.up}</div>
${txt(lbl, 10, 24, { color: '#93a79e', align: 'c', w: 120 })}
<div class="btn" data-key="${b}" style="width:84px">${I.down}</div></div>`;
page('REPORTS', `
${hdr('Test Reports')}
<div class="main col" style="gap:10px">
  ${[0, 1, 2, 3, 4, 5].map((i) => listRow(i, 'RP', `
    ${txt(`RP${i}_T`, 70, 24, { w: 880 })}
    ${st(`RP${i}_UP`, 40, 36, ['<div></div>', ...['var(--dim)', 'var(--amb)', 'var(--acc)'].map((c) => `<div class="ico" style="display:flex;align-items:center;justify-content:center;width:100%;color:${c}">${I.cloud.replace('<svg', '<svg width="26" height="26"')}</div>`)])}`)).join('')}
  <div class="row between center grow">${pager('RP_PREV', 'RP_NEXT', 'RP_PAGE')}
    <div>${txt('RP_MSG', 40, 20, { color: '#93a79e', w: 420, align: 'r' })}</div></div>
</div>`);

const tile = (k, ic, l) => `<div class="tile" data-key="${k}">${ic}${l}</div>`;
page('SETTINGS', `
${hdr('Settings')}
<div class="main col">
  <div class="grow" style="display:grid;grid-template-columns:repeat(4,1fr);gap:12px">
    ${tile('GO_WIFI', I.wifi, 'Wi-Fi')}${tile('GO_DRIVE', I.drive, 'Drive &amp; limits')}${tile('GO_DIAG', I.pulse, 'Diagnostics')}${tile('GO_TRANS', I.chain, 'Transmission')}
  </div>
  <div class="row" style="height:206px">
    <div class="card col grow" style="justify-content:space-between">
      <div class="cap">Remote control</div>
      <div class="lbl" style="line-height:1.35">Allows authorised users to start and stop tests from the dashboard. Needs the local enable key on. Turns off at every power-up.</div>
      ${seg2('REMOTE', 'REMOTE_OFF', 'REMOTE_ON', 'Off', 'Allowed', 58)}
    </div>
    <div class="card col grow" style="justify-content:space-between">
      <div class="cap">Drive mode</div>
      <div class="lbl" style="line-height:1.35">Simulation runs without a VFD for training and bench tests. Reports are marked as simulated.</div>
      ${seg2('SIM', 'SIM_OFF', 'SIM_ON', 'Real drive', 'Simulation', 58)}
    </div>
    <div class="card col" style="width:262px;justify-content:space-between">
      <div class="cap">Brightness</div>
      <div class="row center" style="justify-content:center;gap:0">${num('BRIGHT', 3, 0, 'lg')}<span class="unit">%</span></div>
      <div class="row"><div class="btn grow" data-key="BR_DOWN" style="min-height:58px">${I.minus}</div><div class="btn grow" data-key="BR_UP" style="min-height:58px">${I.plus}</div></div>
    </div>
  </div>
</div>`);

const bars = [1, 2, 3, 4].map((b) => `<div style="display:flex;align-items:flex-end;gap:3px;height:26px">${[1, 2, 3, 4].map((k) => `<div style="width:6px;height:${k * 6 + 2}px;border-radius:2px;background:${k <= b ? 'var(--acc)' : '#33463e'}"></div>`).join('')}</div>`);
page('WIFI', `
${hdr('Wi-Fi Networks')}
<div class="main col" style="gap:10px">
  <div class="row between center" style="height:30px">
    <div>${txt('W_STATUS', 62, 20, { color: '#93a79e', w: 640 })}</div>
    ${st('W_STATE', 170, 30, [pill('idle', 'NOT CONNECTED'), pill('warn', 'CONNECTING'), pill('ok', 'CONNECTED'), pill('info', 'SCANNING'), pill('bad', 'WRONG PASSWORD')])}
  </div>
  ${[0, 1, 2, 3, 4, 5].map((i) => `
  <div class="rowbtn" data-key="WF${i}" style="height:60px">
    ${st(`WF${i}_SIG`, 34, 28, ['<div></div>', ...bars])}
    ${txt(`WF${i}_T`, 32, 24, { w: 420 })}
    <div class="grow"></div>
    ${st(`WF${i}_ON`, 110, 26, ['<div></div>', pill('ok', 'CONNECTED')])}
    ${st(`WF${i}_L`, 30, 30, ['<div></div>', `<div style="color:var(--mut);padding:3px">${I.lock}</div>`])}
  </div>`).join('')}
  <div class="row between center grow">${pager('WF_PREV', 'WF_NEXT', 'WF_PAGE')}
    <div class="row"><div class="btn" data-key="WF_FORGET" style="width:150px">${I.trash}Forget</div>
    <div class="btn pri" data-key="WF_SCAN" style="width:190px">${I.refresh}Scan again</div></div></div>
</div>`);

// Three keyboard layouts share one geometry so the firmware only swaps pages.
const keyboard = (id, rows) => {
  const ch = (c) => nk(`CH_${c.charCodeAt(0).toString(16)}`, c === '&' ? '&amp;' : c === '<' ? '&lt;' : c === '>' ? '&gt;' : c);
  const row = (html) => `<div style="display:flex;gap:7px;height:72px;justify-content:center">${html}</div>`;
  const fx = (h, grow = 1) => h.replace('class="kk', `style="flex:${grow}" class="kk`);
  page(id, `
${hdr('<span data-kb-title></span>Keyboard')}
<div class="main col" style="gap:7px;padding-top:10px">
  <div class="row center" style="gap:12px;height:58px">
    <div>${txt('KB_TITLE', 18, 20, { color: '#93a79e', w: 190 })}</div>
    <div class="grow" style="background:#0e1714;border:2px solid var(--acc-d);border-radius:10px;height:58px;display:flex;align-items:center;padding:0 12px">${txt('KB_VALUE', 48, 32, { w: 760 })}</div>
  </div>
  ${rows.map((r) => row(r.map((k) => {
    if (k === 'SHIFT') return fx(nk(id === 'KB_UP' ? 'KB_LOWER' : 'KB_UPPER', I.shift.replace('<svg', '<svg width="28" height="28"'), 'fn'), 1.5);
    if (k === 'BKSP') return fx(nk('KB_BKSP', I.bksp.replace('<svg', '<svg width="30" height="30"'), 'fn'), 1.5);
    if (k === 'SYM') return fx(nk(id === 'KB_SYM' ? 'KB_LOWER' : 'KB_SYM', id === 'KB_SYM' ? 'abc' : '#+=', 'fn'), 1.5);
    if (k === 'SPACE') return fx(nk('CH_20', 'space', 'fn'), 5);
    if (k === 'CANCEL') return fx(nk('KB_CANCEL', 'Cancel', 'fn'), 1.5);
    if (k === 'SHOW') return fx(nk('KB_SHOW', I.eye.replace('<svg', '<svg width="30" height="30"'), 'fn'), 1.2);
    if (k === 'OK') return fx(nk('KB_OK', 'OK', 'ok'), 2);
    return fx(ch(k));
  }).join(''))).join('')}
</div>`);
};
const C = (s) => [...s];
keyboard('KB_LOW', [C('1234567890'), C('qwertyuiop'), C('asdfghjkl'), ['SHIFT', ...C('zxcvbnm'), 'BKSP'], ['SYM', 'CANCEL', 'SPACE', 'SHOW', 'OK']]);
keyboard('KB_UP', [C('1234567890'), C('QWERTYUIOP'), C('ASDFGHJKL'), ['SHIFT', ...C('ZXCVBNM'), 'BKSP'], ['SYM', 'CANCEL', 'SPACE', 'SHOW', 'OK']]);
keyboard('KB_SYM', [C('!@#$%^&*()'), C('-_=+[]{}\\|'), C(';:\'",.<>/?'), ['SYM', ...C('`~'), 'SPACE', 'BKSP'], ['CANCEL', 'SHOW', 'OK']]);

page('DRIVE', `
${hdr('Drive &amp; Limits')}
<div class="main col">
  <div class="row grow">
    <div class="col grow" style="gap:9px">
      <div class="cap">Motor</div>
      ${field('F_POLES', 'Poles', num('F_POLES_V', 2, 0, 'sm'))}
      ${field('F_NRATED', 'Rated speed', num('F_NRATED_V', 4, 0, 'sm'), 'RPM')}
      ${field('F_FRATED', 'Rated frequency', num('F_FRATED_V', 2, 1, 'sm'), 'Hz')}
      ${field('F_SLIP', 'Slip compensation', num('F_SLIP_V', 2, 1, 'sm'), '%')}
      ${field('F_GEAR', 'Gearbox ratio', num('F_GEAR_V', 3, 2, 'sm'), ': 1')}
    </div>
    <div class="col grow" style="gap:9px">
      <div class="cap">Operating limits</div>
      ${field('F_RPMMIN', 'Min output', num('F_RPMMIN_V', 3, 1, 'sm'), 'RPM')}
      ${field('F_RPMMAX', 'Max output', num('F_RPMMAX_V', 3, 1, 'sm'), 'RPM')}
      ${field('F_HZMAX', 'Max frequency', num('F_HZMAX_V', 2, 1, 'sm'), 'Hz')}
      ${field('F_AMAX', 'Current limit', num('F_AMAX_V', 2, 1, 'sm'), 'A')}
      ${field('F_DEV', 'Max speed deviation', num('F_DEV_V', 2, 1, 'sm'), '%')}
    </div>
    <div class="col grow" style="gap:9px">
      <div class="cap">Ramps &amp; feedback</div>
      ${field('F_ACC', 'Acceleration', num('F_ACC_V', 3, 1, 'sm'), 's')}
      ${field('F_DEC', 'Deceleration', num('F_DEC_V', 3, 1, 'sm'), 's')}
      ${field('F_PPR', 'RPM sensor pulses (0 = none)', num('F_PPR_V', 3, 0, 'sm'), '/rev')}
      ${field('F_VFDID', 'VFD Modbus address', num('F_VFDID_V', 3, 0, 'sm'))}
      <div class="btn pri grow" data-key="BACK" style="min-height:66px">${I.check}Save &amp; close</div>
    </div>
  </div>
</div>`);

page('DIAG', `
${hdr('Diagnostics')}
<div class="main row">
  <div class="card col grow" style="gap:5px;padding:12px 14px">
    ${Array.from({ length: 16 }, (_, i) => `<div>${txt(`DG${i}`, 46, 20, { w: 460 })}</div>`).join('')}
  </div>
  <div class="card col grow" style="gap:5px;padding:12px 14px">
    ${Array.from({ length: 16 }, (_, i) => `<div>${txt(`DG${i + 16}`, 46, 20, { w: 460 })}</div>`).join('')}
  </div>
</div>`);

page('FAULT', `
<div style="position:absolute;inset:0;background:linear-gradient(180deg,#2b1210,#150c0b)"></div>
<div style="position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px;padding:0 60px">
  <div style="width:112px;height:112px;border-radius:50%;background:var(--red);color:#fff;display:flex;align-items:center;justify-content:center">${S('<path d="M12 3.500L22 20.500H2z"/><path d="M12 10v4.500M12 17.500v.1"/>', 'width="68" height="68"')}</div>
  <div style="font:800 40px Inter;letter-spacing:-.5px">Machine stopped</div>
  <div>${txt('FT_TITLE', 40, 32, { color: '#ffffff', align: 'c', w: 640 })}</div>
  <div>${txt('FT_DETAIL', 60, 24, { color: '#f2b5b1', align: 'c', w: 720 })}</div>
  <div>${txt('FT_CODE', 40, 20, { color: '#b98a87', align: 'c', w: 400 })}</div>
  <div class="row" style="margin-top:14px;width:560px">
    <div class="btn grow" data-key="FT_DIAG" style="height:84px;font-size:22px">${I.pulse}Details</div>
    <div class="btn dan grow" data-key="FT_RESET" style="height:84px;font-size:24px">${I.refresh}Reset fault</div>
  </div>
  <div class="lbl" style="color:#b98a87">The machine will not restart by itself. Fix the cause, reset, then start again.</div>
</div>`);

page('CONFIRM', `
<div style="position:absolute;inset:0;background:#0a100e"></div>
<div class="card col" style="position:absolute;left:162px;right:162px;top:130px;height:340px;padding:30px;justify-content:space-between;background:var(--card2)">
  <div class="col" style="gap:18px">
    <div>${txt('CF_TITLE', 38, 32, { w: 640 })}</div>
    <div>${txt('CF_MSG1', 52, 24, { color: '#93a79e', w: 640 })}</div>
    <div>${txt('CF_MSG2', 52, 24, { color: '#93a79e', w: 640 })}</div>
  </div>
  <div class="row"><div class="btn grow" data-key="CF_NO" style="height:84px;font-size:24px">Cancel</div>
  <div class="btn pri grow" data-key="CF_YES" style="height:84px;font-size:24px">Confirm</div></div>
</div>`);
