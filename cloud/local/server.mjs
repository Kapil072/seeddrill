// Local stand-in for AWS: a real MQTT broker plus the real ingest and API code.
//   MQTT  localhost:1883    (replaces IoT Core; forwards what the topic rule would)
//   HTTP  localhost:8787    (replaces API Gateway + Cognito)
// Sign in with operator@spk.local or viewer@spk.local, password demo1234.
import http from 'node:http';
import net from 'node:net';
import Aedes from 'aedes';
import { ingest } from '../src/records.mjs';
import { route } from '../src/routes.mjs';
import { memStore } from './memstore.mjs';

const store = memStore();
const broker = new Aedes();

broker.on('publish', async (packet, client) => {
  if (!client) return;
  const m = packet.topic.match(/^seeddrill\/([^/]+)\/([^/]+)$/);
  if (!m || m[2] === 'cmd') return;
  try {
    await ingest({ ...JSON.parse(packet.payload.toString()), thing: m[1], kind: m[2] }, store);
  } catch (e) {
    console.error('ingest', packet.topic, e.message);
  }
});
net.createServer(broker.handle).listen(1883, () => console.log('MQTT on 1883'));

const publish = (topic, msg) => new Promise((ok) => broker.publish({ topic, payload: JSON.stringify(msg), qos: 1, cmd: 'publish', retain: false, dup: false }, ok));

const cors = { 'access-control-allow-origin': '*', 'access-control-allow-headers': 'authorization,content-type', 'access-control-allow-methods': 'GET,POST,OPTIONS' };
const readBody = (req) => new Promise((ok) => {
  let b = '';
  req.on('data', (c) => (b += c));
  req.on('end', () => ok(b ? JSON.parse(b) : null));
});

http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  const send = (status, body, headers = {}) => {
    const text = typeof body === 'string' ? body : JSON.stringify(body);
    res.writeHead(status, { 'content-type': 'application/json', ...cors, ...headers });
    res.end(text);
  };
  if (req.method === 'OPTIONS') return send(204, '');
  try {
    const body = req.method === 'POST' ? await readBody(req) : null;
    if (url.pathname === '/auth/login') {
      const role = { 'operator@spk.local': 'operator', 'viewer@spk.local': 'viewer' }[body?.email];
      if (!role || body?.password !== 'demo1234') return send(401, { error: 'Incorrect email or password' });
      return send(200, { idToken: `local-${role}`, refreshToken: `local-${role}`, expiresIn: 3600 });
    }
    const auth = req.headers.authorization ?? '';
    if (!auth.startsWith('local-') && !auth.startsWith('Bearer local-')) return send(401, { error: 'sign in required' });
    const r = await route({
      method: req.method, path: url.pathname, query: Object.fromEntries(url.searchParams), body,
      user: { operator: auth.includes('operator') },
    }, { store, publish });
    const h = r.type ? { 'content-type': r.type, 'content-disposition': `attachment; filename="${r.filename}"` } : {};
    send(r.status, r.body, h);
  } catch (e) {
    console.error(e);
    send(500, { error: 'server error' });
  }
}).listen(8787, () => console.log('API on 8787'));
