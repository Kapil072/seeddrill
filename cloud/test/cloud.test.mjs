// Run: node --test cloud/test
// Payload fixtures are the bytes the C++ firmware code produced (firmware/test/host/run.sh),
// so a schema change on either side fails here.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { memStore } from '../local/memstore.mjs';
import { expiry, ingest, keys } from '../src/records.mjs';
import { route } from '../src/routes.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const fixture = (n) => JSON.parse(fs.readFileSync(path.join(here, '../../firmware/test/host/out', `msg_${n}.json`), 'utf8'));
const NOW = 1762300100;
const op = { operator: true }, viewer = { operator: false };

async function seeded() {
  const store = memStore();
  const m = (kind, body) => ingest({ ...body, thing: 'rig-1', kind }, store, NOW);
  await m('status', { online: true, fw: '1.0.0' });
  await m('telemetry', { ...fixture('telemetry'), rem: 1 });
  const samples = fixture('samples');
  await m('samples', samples);
  await m('report', fixture('report'));
  await m('event', fixture('event'));
  return { store, m, id: samples.id };
}
const get = (ctx, p, user = viewer, nowS = NOW) => route({ method: 'GET', path: p, user }, { ...ctx, nowS });
const post = (ctx, p, body, user = op, nowS = NOW) => route({ method: 'POST', path: p, body, user }, { ...ctx, nowS });

test('firmware messages are stored and served back', async () => {
  const { store, id } = await seeded();
  const list = await get({ store }, '/machines');
  assert.equal(list.body.machines.length, 1);
  assert.equal(list.body.machines[0].online, true);
  assert.equal(list.body.machines[0].fw, '1.0.0');

  const l = await get({ store }, '/machines/rig-1/live');
  assert.equal(l.body.telemetry.st, 'running');
  assert.equal(l.body.telemetry.rpm, 530);
  assert.equal(l.body.telemetry.pk, undefined, 'storage keys are not exposed');

  const tests = await get({ store }, '/machines/rig-1/tests');
  assert.equal(tests.body.tests.length, 1);
  assert.equal(tests.body.tests[0].end, 'Completed');

  const t = await get({ store }, `/machines/rig-1/tests/${id}`);
  assert.equal(t.body.samples.length, fixture('samples').d.length);
  assert.equal(t.body.test.profile.name, 'Quote" and \\ slash');

  const ev = await get({ store }, '/machines/rig-1/events');
  assert.equal(ev.body.events[0].text, 'Guard opened');
});

test('CSV matches the device export layout', async () => {
  const { store, id } = await seeded();
  const r = await get({ store }, `/machines/rig-1/tests/${id}/csv`);
  assert.equal(r.type, 'text/csv');
  const lines = r.body.trim().split('\r\n');
  assert.equal(lines[7], 't_s,target_rpm,output_rpm,drive_hz,current_a,sim_kmh,distance_m,state,fault');
  assert.equal(lines.length, 8 + fixture('samples').d.length);
  assert.match(lines[9], /^1,60\.0,/);
});

test('retention is at least 12 months and is set on every historical record', async () => {
  const { store, id } = await seeded();
  assert.ok(expiry(NOW) - NOW >= 365 * 86400);
  assert.ok((await store.get(keys.test('rig-1', id))).ttl > NOW + 365 * 86400);
  assert.ok((await store.get(keys.chunk('rig-1', id, 0))).ttl > NOW + 365 * 86400);
  assert.equal((await store.get(keys.live('rig-1'))).ttl, undefined, 'live row must not expire');
});

test('a report re-sent after adding weights replaces the old one', async () => {
  const { store, m, id } = await seeded();
  await m('report', { ...fixture('report'), seed: 99999 });
  const t = await get({ store }, '/machines/rig-1/tests');
  assert.equal(t.body.tests.length, 1);
  assert.equal(t.body.tests[0].seed, 99999);
  assert.equal((await get({ store }, `/machines/rig-1/tests/${id}`)).status, 200);
});

test('newest tests come first and paging works', async () => {
  const { store, m } = await seeded();
  for (let i = 2; i <= 25; i++) await m('report', { ...fixture('report'), id: i });
  const a = await get({ store }, '/machines/rig-1/tests');
  assert.equal(a.body.tests[0].id, 25);
  assert.equal(a.body.tests.length, 20);
  const b = await route({ method: 'GET', path: '/machines/rig-1/tests', query: { cursor: a.body.cursor }, user: viewer }, { store, nowS: NOW });
  assert.equal(b.body.tests.length, 5);
  assert.equal(b.body.cursor, null);
});

test('remote control: authorised users only, and only when it can work', async () => {
  const { store, m } = await seeded();
  const sent = [];
  const ctx = { store, publish: async (t, msg) => sent.push([t, msg]) };

  assert.equal((await post(ctx, '/machines/rig-1/cmd', { cmd: 'start' }, viewer)).status, 403);
  assert.equal(sent.length, 0);

  assert.equal((await post(ctx, '/machines/rig-1/cmd', { cmd: 'explode' })).status, 400);
  assert.equal((await post(ctx, '/machines/rig-1/cmd', { cmd: 'setpoint', value: 'fast' })).status, 400);

  const r = await post(ctx, '/machines/rig-1/cmd', { cmd: 'start' });
  assert.equal(r.status, 202);
  assert.equal(sent[0][0], 'seeddrill/rig-1/cmd');
  assert.equal(sent[0][1].exp, NOW + 30, 'commands carry a short expiry');

  await m('telemetry', { ...fixture('telemetry'), rem: 0 });
  assert.equal((await post(ctx, '/machines/rig-1/cmd', { cmd: 'start' })).status, 409);
  assert.equal((await post(ctx, '/machines/rig-1/cmd', { cmd: 'stop' })).status, 202, 'stop still goes through');
});

test('offline machines are detected and refuse commands', async () => {
  const { store, m } = await seeded();
  const ctx = { store, publish: async () => assert.fail('nothing should be sent') };
  const stale = await post(ctx, '/machines/rig-1/cmd', { cmd: 'start' }, op, NOW + 60);
  assert.equal(stale.status, 409);
  assert.equal((await get({ store }, '/machines/rig-1/live', viewer, NOW + 60)).body.online, false);

  await m('telemetry', fixture('telemetry'));
  await m('status', { online: false, fw: '1.0.0' });   // last will
  assert.equal((await get({ store }, '/machines/rig-1/live')).body.online, false);
});

test('bad input is rejected', async () => {
  const store = memStore();
  assert.equal(await ingest({ thing: '../../etc', kind: 'telemetry' }, store), 'rejected');
  assert.equal(await ingest({ thing: 'a', kind: 'samples', id: 'x', d: 1 }, store), 'rejected');
  assert.equal(await ingest({ thing: 'a', kind: 'report' }, store), 'rejected');
  assert.equal(await ingest({ thing: 'a', kind: 'whatever' }, store), 'ignored');
  assert.equal(store.rows.size, 0);
  assert.equal((await get({ store }, '/machines/a%2Fb/live')).status, 400);
  assert.equal((await get({ store }, '/nope')).status, 404);
  assert.equal((await get({ store }, '/machines/x/tests/5')).status, 404);
});
