// HTTP API, independent of how it is hosted. Takes a normalised request and returns
// { status, body, type? }. store and publish are injected.
import { keys, toCsv } from './records.mjs';

const ONLINE_WINDOW_S = 35;       // telemetry is at least every 10 s when idle
const COMMAND_TTL_S = 30;         // a command older than this is refused by the device
const json = (status, body) => ({ status, body });
const THING = /^[\w-]{1,64}$/;

const clean = ({ pk, sk, ttl, ...r }) => r;

async function live(store, id, nowS) {
  const [t, s] = await Promise.all([store.get(keys.live(id)), store.get(keys.status(id))]);
  const age = t ? nowS - t.rx : null;
  // LWT marks a clean or unclean disconnect at once; the age check covers everything else.
  const online = !!t && age <= ONLINE_WINDOW_S && s?.online !== false;
  return { id, online, age, fw: s?.fw ?? null, telemetry: t ? clean(t) : null };
}

async function samplesOf(store, id, test) {
  const rows = await store.query(keys.chunk(id, test, 0).pk, 'C#', { limit: 1000 });
  return rows.items.flatMap((c) => c.d);
}

export async function route(req, { store, publish, nowS = Math.floor(Date.now() / 1000) }) {
  const { method, path, query = {}, body, user } = req;
  const m = path.match(/^\/machines(?:\/([^/]+))?(?:\/(live|tests|events|cmd))?(?:\/(\d+))?(?:\/(csv))?$/);
  if (!m) return json(404, { error: 'not found' });
  const [, id, sub, test, csv] = m;
  if (id && !THING.test(id)) return json(400, { error: 'bad machine id' });

  if (method === 'GET' && !id) {
    const rows = await store.query('MACHINES', '', { limit: 100 });
    return json(200, { machines: await Promise.all(rows.items.map((r) => live(store, r.sk, nowS))), operator: user.operator });
  }
  if (method === 'GET' && sub === 'live') return json(200, { ...(await live(store, id, nowS)), operator: user.operator });

  if (method === 'GET' && sub === 'tests' && !test) {
    const limit = Math.min(Number(query.limit) || 20, 50);
    const r = await store.query(`M#${id}`, 'T#', { limit, reverse: true, cursor: query.cursor });
    return json(200, { tests: r.items.map(clean), cursor: r.cursor ?? null });
  }

  if (method === 'GET' && sub === 'tests' && test) {
    const t = await store.get(keys.test(id, Number(test)));
    if (!t) return json(404, { error: 'no such test' });
    const samples = await samplesOf(store, id, Number(test));
    if (csv) return { status: 200, type: 'text/csv', filename: `seeddrill-${id}-test-${test}.csv`, body: toCsv(t, samples) };
    return json(200, { test: clean(t), samples });
  }

  if (method === 'GET' && sub === 'events') {
    const r = await store.query(`M#${id}`, 'E#', { limit: Math.min(Number(query.limit) || 30, 100), reverse: true });
    return json(200, { events: r.items.map(clean) });
  }

  if (method === 'POST' && sub === 'cmd') {
    if (!user.operator) return json(403, { error: 'operator access required' });
    const { cmd, value } = body ?? {};
    if (!['start', 'stop', 'setpoint'].includes(cmd)) return json(400, { error: 'unknown command' });
    if (cmd === 'setpoint' && !(Number.isInteger(value) && value > 0 && value < 65536)) return json(400, { error: 'bad value' });
    const l = await live(store, id, nowS);
    if (!l.online) return json(409, { error: 'machine is offline' });
    // The device decides whether it is safe; this only rules out pointless requests.
    if (cmd === 'start' && !l.telemetry?.rem) return json(409, { error: 'remote control is off at the panel' });
    const msg = { cmd, id: `${nowS}-${Math.random().toString(36).slice(2, 10)}`, exp: nowS + COMMAND_TTL_S, value: value ?? 0 };
    await publish(`seeddrill/${id}/cmd`, msg);
    return json(202, { sent: msg.id });
  }
  return json(405, { error: 'method not allowed' });
}
