// Simulated controller speaking the firmware's MQTT protocol, for dashboard work
// without hardware. Payload field names match firmware/lib/core/cloud_msgs.cpp.
//   node local/device.mjs [thing] [--seed]
import mqtt from 'mqtt';

const thing = process.argv[2] && !process.argv[2].startsWith('--') ? process.argv[2] : 'rig-001';
const T = (s) => `seeddrill/${thing}/${s}`;
const now = () => Math.floor(Date.now() / 1000);
const c = mqtt.connect('mqtt://localhost:1883', { clientId: thing, will: { topic: T('status'), payload: JSON.stringify({ online: false, fw: '1.0.0' }), qos: 1, retain: true } });

const profile = { name: 'Vishwakarma 11 row', wheelMm: 500, speed: 50, rpm: 500, mode: 0, stopBy: 0, length: 10, rows: 11, rowSp: 225, spacing: 200, cells: 24, s1: [20, 20], s2: [0, 0], rig: [20, 20], couple: 0, gear: 1500 };
const ST = { ready: 0, checking: 1, running: 2, stopping: 3, complete: 4, fault: 5 };
const s = { st: 'ready', test: 12, rpmT: 531, rpm: 0, el: 0, goal: 60, dist: 0, hz: 0, log: [], rem: 1, grd: 1, peak: 0 };

function send(topic, o, opts = { qos: 0 }) { c.publish(T(topic), JSON.stringify(o), opts); }

function report(test, samples, start, over = {}) {
  const steady = samples.filter((r) => r[7] === ST.running && r[0] > 6);
  const rp = steady.map((r) => r[2]);
  const avg = rp.length ? Math.round(rp.reduce((a, b) => a + b, 0) / rp.length) : 0;
  const target = samples[0]?.[1] ?? 0;
  for (let i = 0; i < samples.length; i += 120) send('samples', { id: test, first: i, d: samples.slice(i, i + 120) }, { qos: 1 });
  send('report', {
    id: test, start, dur: samples.length, dist: (samples.at(-1)?.[6] ?? 0) * 10, samples: samples.length, rpmAvg: avg,
    rpmMin: rp.length ? Math.min(...rp) : 0, rpmMax: rp.length ? Math.max(...rp) : 0, rpmT: target, hzT: Math.round(target * 5.2),
    aPeak: Math.max(0, ...samples.map((r) => r[4])), aAvg: 345, acc: 992, seed: 24550, fert: 41230, end: 'Completed', fault: 0, vfd: 0, sim: 1, meas: 0, profile, ...over,
  }, { qos: 1 });
}

function synthetic(seconds, target) {
  const d = [];
  let dist = 0;
  for (let t = 0; t < seconds; t++) {
    const ramp = Math.min(1, t / 5, (seconds - t) / 5);
    const rpm = Math.round(target * ramp + (Math.random() - 0.5) * 6);
    const hz = Math.round(rpm * 5.2);
    const kmh = Math.round(rpm * 0.942);
    dist += kmh / 36;
    d.push([t, target, Math.max(0, rpm), hz, 180 + Math.round(hz * 0.065 + Math.random() * 8), kmh, Math.round(dist), ramp < 1 && t > seconds / 2 ? ST.stopping : ST.running, 0]);
  }
  return d;
}

c.on('connect', () => {
  console.log(thing, 'connected');
  c.subscribe(T('cmd'), { qos: 1 });
  send('status', { online: true, fw: '1.0.0' }, { qos: 1, retain: true });
  if (process.argv.includes('--seed')) {
    const base = now() - 86400 * 6;
    [[300, 531], [240, 690], [480, 420], [180, 531], [600, 880]].forEach(([sec, tgt], i) => report(7 + i, synthetic(sec, tgt), base + i * 86400));
    send('event', { ts: now() - 7200, type: 'fault', fault: 2, text: 'Guard opened', vfd: 0, test: 9 }, { qos: 1 });
    send('event', { ts: now() - 86000, type: 'fault', fault: 7, text: 'Motor current over limit', vfd: 0, test: 8 }, { qos: 1 });
    report(6, synthetic(90, 531), base - 86400, { end: 'Stopped by fault', fault: 2, seed: 0, fert: 0 });
  }
  start();   // a test is already running when the dashboard opens
});

function start() {
  Object.assign(s, { st: 'running', test: s.test + 1, el: 0, dist: 0, log: [], peak: 0, begun: now() });
}

c.on('message', (_, buf) => {
  const m = JSON.parse(buf);
  let ok = true, reason = '';
  if (m.exp < now()) { ok = false; reason = 'expired'; }
  else if (m.cmd === 'start') { if (s.st === 'running') { ok = false; reason = 'already running'; } else start(); }
  else if (m.cmd === 'stop') { if (s.st === 'running') s.st = 'stopping'; }
  else if (m.cmd === 'setpoint') s.rpmT = Math.round(m.value * 10.6);
  send('ack', { id: m.id, ok, reason });
  console.log('cmd', m.cmd, ok ? 'accepted' : reason);
});

setInterval(() => {
  if (s.st === 'running' || s.st === 'stopping') {
    s.el += 1;
    const goal = s.st === 'stopping' ? 0 : s.rpmT;
    s.rpm += Math.max(-110, Math.min(110, goal - s.rpm));
    const noisy = Math.max(0, Math.round(s.rpm + (s.rpm ? (Math.random() - 0.5) * 5 : 0)));
    s.hz = Math.round(noisy * 5.2);
    s.kmh = Math.round(noisy * 0.942);
    s.dist += s.kmh / 3.6 / 10 * 10 / 10;
    s.rpmNow = noisy;
    s.log.push([s.el, s.rpmT, noisy, s.hz, 180 + Math.round(s.hz * 0.065), s.kmh, Math.round(s.dist / 10), ST[s.st], 0]);
    if (s.st === 'running' && s.el >= s.goal) s.st = 'stopping';
    if (s.st === 'stopping' && s.rpm === 0) {
      report(s.test, s.log, s.begun);
      s.st = 'complete';
      setTimeout(() => { s.st = 'ready'; }, 8000);
    }
  } else { s.rpmNow = 0; s.hz = 0; s.kmh = 0; }

  const running = s.st === 'running' || s.st === 'stopping';
  send('telemetry', {
    ts: now(), st: s.st, test: s.test, rpm: s.rpmNow, rpmT: running ? s.rpmT : 0, kmh: s.kmh, hz: s.hz, hzC: s.hz, a: running ? 180 + Math.round(s.hz * 0.065) : 0,
    v: 3195, tq: running ? 418 : 0, dist: Math.round(s.dist), el: s.el, left: running ? Math.max(0, s.goal - s.el) : 0,
    prog: running ? Math.min(100, Math.round(s.el * 100 / s.goal)) : 0, fault: 0, vfd: 0, warn: 0, est: 1, grd: s.grd, en: 1, drv: 1, rem: s.rem, remRun: 0, sim: 1, meas: 0, heap: 201344,
  });
}, 1000);
