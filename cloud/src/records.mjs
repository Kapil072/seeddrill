// Turns device messages into stored records. Storage is passed in, so the same code
// runs on DynamoDB in AWS and on the in-memory store in local tests.
//
//   pk                      sk              contents
//   MACHINES                <thing>         registry row
//   M#<thing>               LIVE            latest telemetry
//   M#<thing>               STATUS          online / firmware (from retained + LWT)
//   M#<thing>               T#<id>          test summary
//   M#<thing>               E#<ts>#<n>      fault event
//   M#<thing>#T#<id>        C#<first>       block of samples

const pad = (n, w = 10) => String(n).padStart(w, '0');
const days = () => Number(process.env.RETENTION_DAYS || 400);
export const expiry = (nowS) => nowS + days() * 86400;

export const keys = {
  machine: (t) => ({ pk: 'MACHINES', sk: t }),
  live: (t) => ({ pk: `M#${t}`, sk: 'LIVE' }),
  status: (t) => ({ pk: `M#${t}`, sk: 'STATUS' }),
  test: (t, id) => ({ pk: `M#${t}`, sk: `T#${pad(id)}` }),
  chunk: (t, id, first) => ({ pk: `M#${t}#T#${pad(id)}`, sk: `C#${pad(first)}` }),
  event: (t, ts, n) => ({ pk: `M#${t}`, sk: `E#${pad(ts)}#${n}` }),
};

// Topic rule adds thing and kind. Anything malformed is dropped rather than retried:
// a retry cannot fix a bad payload.
export async function ingest(msg, store, nowS = Math.floor(Date.now() / 1000)) {
  const { thing, kind, ...body } = msg;
  if (!thing || !/^[\w-]{1,64}$/.test(thing)) return 'rejected';
  switch (kind) {
    case 'telemetry':
      await store.put({ ...keys.live(thing), ...body, rx: nowS });
      await store.put({ ...keys.machine(thing), lastSeen: nowS });
      return 'telemetry';
    case 'status':
      await store.put({ ...keys.status(thing), ...body, rx: nowS });
      await store.put({ ...keys.machine(thing), lastSeen: nowS });
      return 'status';
    case 'event': {
      const ts = Number(body.ts) || nowS;
      await store.put({ ...keys.event(thing, ts, Math.floor(Math.random() * 1e6)), ...body, ttl: expiry(nowS) });
      return 'event';
    }
    case 'samples': {
      if (!Number.isInteger(body.id) || !Array.isArray(body.d)) return 'rejected';
      await store.put({ ...keys.chunk(thing, body.id, body.first), first: body.first, d: body.d, ttl: expiry(nowS) });
      return 'samples';
    }
    case 'report': {
      if (!Number.isInteger(body.id)) return 'rejected';
      // Re-sent after an operator adds weights: same key, so it simply overwrites.
      await store.put({ ...keys.test(thing, body.id), ...body, rx: nowS, ttl: expiry(nowS) });
      return 'report';
    }
    default:
      return 'ignored';
  }
}

export const num = (x, d) => (x / 10 ** d).toFixed(d);

const STATE = ['ready', 'checking', 'running', 'stopping', 'complete', 'fault'];

export function toCsv(test, samples) {
  const rows = [
    `# Seed Drill Field Simulator test report`,
    `# Test,${test.id}`,
    `# Started (UTC),${new Date(test.start * 1000).toISOString()}`,
    `# Profile,${JSON.stringify(test.profile?.name ?? '')}`,
    `# Result,${test.end}`,
    `# Simulated drive,${test.sim ? 'yes' : 'no'}`,
    `# RPM source,${test.meas ? 'measured sensor' : 'estimated from VFD'}`,
    't_s,target_rpm,output_rpm,drive_hz,current_a,sim_kmh,distance_m,state,fault',
  ];
  for (const [t, tgt, rpm, hz, a, v, dist, st, fault] of samples) {
    rows.push([t, num(tgt, 1), num(rpm, 1), num(hz, 2), num(a, 2), num(v, 1), dist, STATE[st] ?? st, fault].join(','));
  }
  return rows.join('\r\n') + '\r\n';
}
