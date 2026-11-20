// Talks straight to Cognito and the API with fetch: no SDK, nothing extra to download.
const API = import.meta.env.VITE_API_URL || 'http://localhost:8787';
const REGION = import.meta.env.VITE_COGNITO_REGION;
const CLIENT = import.meta.env.VITE_COGNITO_CLIENT_ID;
const LOCAL = !CLIENT;
const KEY = 'sd.session';

let session = null;
try { session = JSON.parse(localStorage.getItem(KEY)); } catch { /* private mode */ }

const save = (s) => {
  session = s;
  try { s ? localStorage.setItem(KEY, JSON.stringify(s)) : localStorage.removeItem(KEY); } catch { /* private mode */ }
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

export const signedIn = () => !!session;
export const signOut = () => save(null);

export async function signIn(email, password) {
  if (LOCAL) {
    const r = await fetch(`${API}/auth/login`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ email, password }) });
    const j = await r.json();
    if (!r.ok) throw new Error(j.error);
    save({ id: j.idToken, refresh: j.refreshToken, exp: Date.now() + j.expiresIn * 1000 });
    return;
  }
  const j = await cognito('InitiateAuth', { AuthFlow: 'USER_PASSWORD_AUTH', ClientId: CLIENT, AuthParameters: { USERNAME: email, PASSWORD: password } });
  if (!j.AuthenticationResult) throw new Error('Your account needs a new password. Ask your administrator.');
  const a = j.AuthenticationResult;
  save({ id: a.IdToken, refresh: a.RefreshToken, exp: Date.now() + a.ExpiresIn * 1000 });
}

async function token() {
  if (!session) throw new AuthError();
  if (!LOCAL && Date.now() > session.exp - 60000) {
    const j = await cognito('InitiateAuth', { AuthFlow: 'REFRESH_TOKEN_AUTH', ClientId: CLIENT, AuthParameters: { REFRESH_TOKEN: session.refresh } });
    save({ ...session, id: j.AuthenticationResult.IdToken, exp: Date.now() + j.AuthenticationResult.ExpiresIn * 1000 });
  }
  return session.id;
}

export class AuthError extends Error {}

async function call(path, opts = {}) {
  const r = await fetch(API + path, { ...opts, headers: { ...opts.headers, authorization: await token() } });
  if (r.status === 401) { save(null); throw new AuthError(); }
  return r;
}

export async function get(path) {
  const r = await call(path);
  if (!r.ok) throw new Error((await r.json().catch(() => ({}))).error || `Request failed (${r.status})`);
  return r.json();
}

export async function post(path, body) {
  const r = await call(path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || `Request failed (${r.status})`);
  return j;
}

export const download = async (path, name) => {
  const r = await call(path);
  if (!r.ok) throw new Error('Download failed');
  saveBlob(await r.blob(), name);
};

export function saveBlob(blob, name) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

// Last known data, shown instantly on open while fresh data loads.
export const remember = (k, v) => { try { localStorage.setItem(`sd.c.${k}`, JSON.stringify(v)); } catch { /* full or private */ } };
export const recall = (k) => { try { return JSON.parse(localStorage.getItem(`sd.c.${k}`)); } catch { return null; } };
