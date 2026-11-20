// Two operating modes:
//
//  LOCAL  — connects directly to the ESP32 on the local Wi-Fi.
//           No auth, no cloud. Set the device IP in the browser once; it is
//           remembered in localStorage.  API base: http://<ip>/api
//
//  CLOUD  — the original AWS path: Cognito auth + Lambda API.
//           Active when VITE_COGNITO_CLIENT_ID is set at build time.
//
// The mode is chosen at runtime: if a local device IP is stored we use it,
// otherwise we fall back to cloud (or show the cloud login).

const CLOUD_API = import.meta.env.VITE_API_URL || 'http://localhost:8787';
const REGION    = import.meta.env.VITE_COGNITO_REGION;
const CLIENT    = import.meta.env.VITE_COGNITO_CLIENT_ID;
const HAS_CLOUD = !!CLIENT;

const KEY_SESSION   = 'sd.session';
const KEY_LOCAL_IP  = 'sd.local_ip';     // e.g. "192.168.1.42"
const KEY_LOCAL_MODE = 'sd.local_mode';  // "1" when user chose local

// ---------- local mode -----------------------------------------------------------

export const getLocalIp  = () => { try { return localStorage.getItem(KEY_LOCAL_IP) || ''; } catch { return ''; } };
export const setLocalIp  = (ip) => { try { ip ? localStorage.setItem(KEY_LOCAL_IP, ip) : localStorage.removeItem(KEY_LOCAL_IP); } catch { } };
export const isLocalMode = () => { try { return localStorage.getItem(KEY_LOCAL_MODE) === '1'; } catch { return false; } };
export const enableLocal = (ip) => { setLocalIp(ip); try { localStorage.setItem(KEY_LOCAL_MODE, '1'); } catch { } };
export const disableLocal = () => { try { localStorage.removeItem(KEY_LOCAL_MODE); } catch { } };

function localBase() { return `http://${getLocalIp()}/api`; }

// Local fetch — no auth header needed.
async function localCall(path, opts = {}) {
  const url = localBase() + path;
  let r;
  try {
    r = await fetch(url, opts);
  } catch {
    throw new Error(`Cannot reach device at ${getLocalIp()}. Check Wi-Fi.`);
  }
  if (!r.ok) throw new Error(`Device returned ${r.status}`);
  return r;
}

// Map the ESP32 /api/status response to the shape Live.jsx expects from the cloud.
// Cloud shape: { online, fw, operator, telemetry: { st, rpm, rpmT, kmh, ... } }
function normaliseStatus(j) {
  // ESP32 already returns the right shape; just fill in fields the cloud adds.
  return {
    ...j,
    id: 'local',
    operator: true,   // local user always has operator access
  };
}

// Map ESP32 /api/reports response (array of test objects) to cloud shape.
// Cloud shape: { tests: [...], cursor }
function normaliseReports(j) {
  return j; // already { tests, cursor:null }
}

// ---------- cloud auth -----------------------------------------------------------

let session = null;
try { session = JSON.parse(localStorage.getItem(KEY_SESSION)); } catch { }

const saveSession = (s) => {
  session = s;
  try { s ? localStorage.setItem(KEY_SESSION, JSON.stringify(s)) : localStorage.removeItem(KEY_SESSION); } catch { }
};

async function cognito(target, body) {
  const r = await fetch(`https://cognito-idp.${REGION}.amazonaws.com/`, {
    method: 'POST',
    headers: { 'content-type': 'application/x-amz-json-1.1', 'x-amz-target': `AWSCognitoIdentityProviderService.${target}` },
    body: JSON.stringify(body),
  });
  const j = await r.json();
  if (!r.ok) throw new Error(j.__type?.includes('NotAuthorized') ? 'Incorrect email or password' : (j.message || 'Sign-in failed'));
  return j;
}

export const signedIn  = () => isLocalMode() || !!session;
export const signOut   = () => { saveSession(null); disableLocal(); };

export async function signIn(email, password) {
  if (!HAS_CLOUD) {
    // Dev fallback when no Cognito is configured
    const r = await fetch(`${CLOUD_API}/auth/login`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email, password }) });
    const j = await r.json();
    if (!r.ok) throw new Error(j.error);
    saveSession({ id: j.idToken, refresh: j.refreshToken, exp: Date.now() + j.expiresIn * 1000 });
    return;
  }
  const j = await cognito('InitiateAuth', { AuthFlow: 'USER_PASSWORD_AUTH', ClientId: CLIENT, AuthParameters: { USERNAME: email, PASSWORD: password } });
  if (!j.AuthenticationResult) throw new Error('Your account needs a new password. Ask your administrator.');
  const a = j.AuthenticationResult;
  saveSession({ id: a.IdToken, refresh: a.RefreshToken, exp: Date.now() + a.ExpiresIn * 1000 });
}

async function cloudToken() {
  if (!session) throw new AuthError();
  if (HAS_CLOUD && Date.now() > session.exp - 60000) {
    const j = await cognito('InitiateAuth', { AuthFlow: 'REFRESH_TOKEN_AUTH', ClientId: CLIENT, AuthParameters: { REFRESH_TOKEN: session.refresh } });
    saveSession({ ...session, id: j.AuthenticationResult.IdToken, exp: Date.now() + j.AuthenticationResult.ExpiresIn * 1000 });
  }
  return session.id;
}

export class AuthError extends Error {}

async function cloudCall(path, opts = {}) {
  const r = await fetch(CLOUD_API + path, { ...opts, headers: { ...opts.headers, authorization: await cloudToken() } });
  if (r.status === 401) { saveSession(null); throw new AuthError(); }
  return r;
}

// ---------- unified API calls ----------------------------------------------------
// These are what the UI components import. They automatically route to local or cloud.

export async function get(path) {
  if (isLocalMode()) {
    // Translate cloud paths to ESP32 paths
    const esp = cloudToLocal(path, 'GET');
    if (esp === null) return cloudFallback(path);
    const r = await localCall(esp.path);
    const j = await r.json();
    return esp.transform ? esp.transform(j) : j;
  }
  const r = await cloudCall(path);
  if (!r.ok) throw new Error((await r.json().catch(() => ({}))).error || `Request failed (${r.status})`);
  return r.json();
}

export async function post(path, body) {
  if (isLocalMode()) {
    const esp = cloudToLocal(path, 'POST');
    if (esp === null) return { ok: true };
    const r = await localCall(esp.path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
    return r.json();
  }
  const r = await cloudCall(path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || `Request failed (${r.status})`);
  return j;
}

export const download = async (path, name) => {
  if (isLocalMode()) {
    // Map /machines/x/tests/123/csv → http://<ip>/r/123.csv (served by firmware directly)
    const m = path.match(/\/tests\/(\d+)\/csv/);
    if (m) {
      const url = `http://${getLocalIp()}/r/${m[1]}.csv`;
      const a = document.createElement('a'); a.href = url; a.download = name; a.click();
      return;
    }
  }
  const r = await cloudCall(path);
  if (!r.ok) throw new Error('Download failed');
  saveBlob(await r.blob(), name);
};

// ---------- cloud-path → ESP32-path translation ----------------------------------

function cloudToLocal(path, method) {
  // GET /machines           → synthetic: return a single "local" machine
  if (path === '/machines') return { path: '/status', transform: (j) => ({ machines: [{ id: 'local' }] }) };

  // GET /machines/x/live    → /status
  if (/^\/machines\/[^/]+\/live$/.test(path))
    return { path: '/status', transform: normaliseStatus };

  // GET /machines/x/tests   → /reports
  if (/^\/machines\/[^/]+\/tests(\?.*)?$/.test(path))
    return { path: '/reports', transform: normaliseReports };

  // GET /machines/x/tests/123  → /tests/123
  const detail = path.match(/^\/machines\/[^/]+\/tests\/(\d+)$/);
  if (detail) return { path: `/tests/${detail[1]}` };

  // GET /machines/x/events  → /events
  if (/^\/machines\/[^/]+\/events$/.test(path))
    return { path: '/events' };

  // POST /machines/x/cmd    → /command
  if (method === 'POST' && /^\/machines\/[^/]+\/cmd$/.test(path))
    return { path: '/command' };

  return null;
}

async function cloudFallback(path) {
  const r = await cloudCall(path);
  if (!r.ok) throw new Error(`Request failed (${r.status})`);
  return r.json();
}

// ---------- PDF generation in local mode ----------------------------------------
// In local mode there is no cloud Lambda to generate PDFs, so we redirect to the
// firmware's own /r/<id>.pdf endpoint.
export function localPdfUrl(id) {
  return `http://${getLocalIp()}/r/${id}.pdf`;
}

export function saveBlob(blob, name) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

// Last known data, shown instantly on open while fresh data loads.
export const remember = (k, v) => { try { localStorage.setItem(`sd.c.${k}`, JSON.stringify(v)); } catch { } };
export const recall   = (k)    => { try { return JSON.parse(localStorage.getItem(`sd.c.${k}`)); } catch { return null; } };
