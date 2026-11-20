import { lazy, StrictMode, Suspense, useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AuthError, enableLocal, get, getLocalIp, isLocalMode, recall, remember, signedIn, signIn, signOut } from './api.js';
import Live from './Live.jsx';
import './style.css';
import { Skeleton } from './ui.jsx';

const Tests      = lazy(() => import('./Tests.jsx'));
const TestDetail = lazy(() => import('./TestDetail.jsx'));
const Events     = lazy(() => import('./Events.jsx'));

const useHash = () => {
  const [h, setH] = useState(location.hash.slice(1) || '/');
  useEffect(() => {
    const f = () => { setH(location.hash.slice(1) || '/'); window.scrollTo(0, 0); };
    addEventListener('hashchange', f);
    return () => removeEventListener('hashchange', f);
  }, []);
  return h;
};

// ---- Local device connect screen ------------------------------------------------
function ConnectLocal({ done }) {
  const [ip, setIp] = useState(getLocalIp() || '192.168.1.');
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const isGithub = typeof location !== 'undefined' && location.hostname.endsWith('github.io');

  const submit = async (e) => {
    e.preventDefault();
    const trimmed = ip.trim().replace(/\/$/, '');
    if (!trimmed) { setErr('Enter the IP address shown on the display'); return; }
    setBusy(true);
    setErr('');
    try {
      const r = await fetch(`http://${trimmed}/api/status`, { signal: AbortSignal.timeout(4000) });
      if (!r.ok) throw new Error(`Device responded with ${r.status}`);
      enableLocal(trimmed);
      done();
    } catch (x) {
      if (isGithub) {
        setErr(`Browser blocked direct fetch to http://${trimmed} due to HTTPS Mixed-Content policy. Please open http://${trimmed}/app/ directly in your browser address bar.`);
      } else {
        setErr(x.name === 'TimeoutError' ? `No response from ${trimmed} — check IP and Wi-Fi` : x.message);
      }
    }
    setBusy(false);
  };

  return (
    <form className="login" onSubmit={submit}>
      <img src="./icon.svg" width="64" height="64" alt="" />
      <h1>Seed Drill Monitor</h1>
      <p className="note" style={{ textAlign: 'center' }}>Enter the IP address shown on the controller display</p>
      {isGithub && (
        <div className="warnbar" style={{ textAlign: 'left', fontSize: '13px' }}>
          <b>Notice: Running on GitHub Pages (HTTPS)</b>
          <p style={{ margin: '6px 0 0' }}>
            Browsers block HTTPS websites from accessing plain HTTP local IPs.
            To connect directly without cloud, open <b>http://&lt;controller-ip&gt;/app/</b> directly in your browser.
          </p>
        </div>
      )}
      <label>Device IP address
        <input
          type="text"
          inputMode="numeric"
          placeholder="192.168.1.42"
          value={ip}
          onChange={(e) => setIp(e.target.value)}
          autoFocus
        />
      </label>
      {err && <p className="err" role="alert">{err}</p>}
      <button className="btn pri" disabled={busy}>{busy ? 'Connecting…' : 'Connect to device'}</button>
      <button type="button" className="btn ghost" onClick={() => done('cloud')}>Use cloud login instead</button>
    </form>
  );
}

// ---- Cloud login screen ---------------------------------------------------------
function Login({ done }) {
  const [email, setEmail]       = useState('');
  const [password, setPassword] = useState('');
  const [err, setErr]           = useState('');
  const [busy, setBusy]         = useState(false);

  const submit = async (e) => {
    e.preventDefault();
    setBusy(true);
    setErr('');
    try { await signIn(email.trim(), password); done(); }
    catch (x) { setErr(x.message); setBusy(false); }
  };

  return (
    <form className="login" onSubmit={submit}>
      <img src="./icon.svg" width="64" height="64" alt="" />
      <h1>Seed Drill Monitor</h1>
      <label>Email<input type="email" autoComplete="username" required value={email} onChange={(e) => setEmail(e.target.value)} /></label>
      <label>Password<input type="password" autoComplete="current-password" required value={password} onChange={(e) => setPassword(e.target.value)} /></label>
      {err && <p className="err" role="alert">{err}</p>}
      <button className="btn pri" disabled={busy}>{busy ? 'Signing in…' : 'Sign in'}</button>
      <button type="button" className="btn ghost" onClick={() => done('local')}>Connect directly to device</button>
    </form>
  );
}

// ---- Entry: choose local or cloud -----------------------------------------------
function Entry({ done }) {
  // If a local IP is already stored, skip straight to ConnectLocal
  const [mode, setMode] = useState(() => {
    if (isLocalMode() && getLocalIp()) return 'local';
    return getLocalIp() ? 'local' : 'choose';
  });

  if (mode === 'local') return <ConnectLocal done={(m) => m === 'cloud' ? setMode('cloud') : done()} />;
  if (mode === 'cloud') return <Login done={(m) => m === 'local' ? setMode('local') : done()} />;

  // First visit: offer both options
  return (
    <div className="login">
      <img src="./icon.svg" width="64" height="64" alt="" />
      <h1>Seed Drill Monitor</h1>
      <p className="note" style={{ textAlign: 'center' }}>How do you want to connect?</p>
      <button className="btn pri" onClick={() => setMode('local')}>Connect to device directly<br /><small>Same Wi-Fi, no internet needed</small></button>
      <button className="btn" onClick={() => setMode('cloud')}>Cloud login<br /><small>Access from anywhere</small></button>
    </div>
  );
}

// ---- Main app -------------------------------------------------------------------
function App() {
  const [auth, setAuth]       = useState(signedIn());
  const [machine, setMachine] = useState(recall('machine'));
  const [none, setNone]       = useState(false);
  const route = useHash();

  useEffect(() => {
    if (!auth) return;
    get('/machines').then((r) => {
      const ids = r.machines.map((m) => m.id);
      if (!ids.length) { setNone(true); return; }
      const pick = ids.includes(machine) ? machine : ids[0];
      setMachine(pick);
      remember('machine', pick);
    }).catch((e) => { if (e instanceof AuthError) setAuth(false); });
  }, [auth]);

  if (!auth) return <Entry done={() => setAuth(true)} />;

  const detail = route.match(/^\/tests\/(\d+)$/);
  const tab = route.startsWith('/tests') ? 'tests' : route.startsWith('/events') ? 'events' : 'live';
  const out = () => { signOut(); setAuth(false); setMachine(null); };

  return (
    <>
      <main>
        {none && <p className="empty">No machines have reported yet. Power on a controller connected to Wi-Fi.</p>}
        {machine && (
          <Suspense fallback={<div className="stack"><Skeleton h={44} /><Skeleton h={220} /><Skeleton /></div>}>
            {detail
              ? <TestDetail machine={machine} id={Number(detail[1])} />
              : tab === 'tests'  ? <Tests machine={machine} />
              : tab === 'events' ? <Events machine={machine} />
              : <Live machine={machine} onSignOut={out} />}
          </Suspense>
        )}
      </main>
      <nav>
        <a href="#/"       className={tab === 'live'   ? 'on' : ''}>Live</a>
        <a href="#/tests"  className={tab === 'tests'  ? 'on' : ''}>Reports</a>
        <a href="#/events" className={tab === 'events' ? 'on' : ''}>Faults</a>
      </nav>
    </>
  );
}

createRoot(document.getElementById('root')).render(<StrictMode><App /></StrictMode>);

if ('serviceWorker' in navigator && import.meta.env.PROD) {
  addEventListener('load', () => navigator.serviceWorker.register('./sw.js'));
}
