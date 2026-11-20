import { lazy, StrictMode, Suspense, useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AuthError, get, recall, remember, signedIn, signIn, signOut } from './api.js';
import Live from './Live.jsx';
import './style.css';
import { Skeleton } from './ui.jsx';

// Only the live screen is in the first download; history loads when it is opened.
const Tests = lazy(() => import('./Tests.jsx'));
const TestDetail = lazy(() => import('./TestDetail.jsx'));
const Events = lazy(() => import('./Events.jsx'));

const useHash = () => {
  const [h, setH] = useState(location.hash.slice(1) || '/');
  useEffect(() => {
    const f = () => { setH(location.hash.slice(1) || '/'); window.scrollTo(0, 0); };
    addEventListener('hashchange', f);
    return () => removeEventListener('hashchange', f);
  }, []);
  return h;
};

function Login({ done }) {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const submit = async (e) => {
    e.preventDefault();
    setBusy(true);
    setErr('');
    try { await signIn(email.trim(), password); done(); } catch (x) { setErr(x.message); setBusy(false); }
  };
  return (
    <form className="login" onSubmit={submit}>
      <img src="./icon.svg" width="64" height="64" alt="" />
      <h1>Seed Drill Monitor</h1>
      <label>Email<input type="email" autoComplete="username" required value={email} onChange={(e) => setEmail(e.target.value)} /></label>
      <label>Password<input type="password" autoComplete="current-password" required value={password} onChange={(e) => setPassword(e.target.value)} /></label>
      {err && <p className="err" role="alert">{err}</p>}
      <button className="btn pri" disabled={busy}>{busy ? 'Signing in…' : 'Sign in'}</button>
    </form>
  );
}

function App() {
  const [auth, setAuth] = useState(signedIn());
  const [machine, setMachine] = useState(recall('machine'));
  const [none, setNone] = useState(false);
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

  if (!auth) return <Login done={() => setAuth(true)} />;

  const detail = route.match(/^\/tests\/(\d+)$/);
  const tab = route.startsWith('/tests') ? 'tests' : route.startsWith('/events') ? 'events' : 'live';
  const out = () => { signOut(); setAuth(false); };

  return (
    <>
      <main>
        {none && <p className="empty">No machines have reported yet. Power on a controller that is connected to the internet.</p>}
        {machine && (
          <Suspense fallback={<div className="stack"><Skeleton h={44} /><Skeleton h={220} /><Skeleton /></div>}>
            {detail ? <TestDetail machine={machine} id={Number(detail[1])} />
              : tab === 'tests' ? <Tests machine={machine} />
                : tab === 'events' ? <Events machine={machine} />
                  : <Live machine={machine} onSignOut={out} />}
          </Suspense>
        )}
      </main>
      <nav>
        <a href="#/" className={tab === 'live' ? 'on' : ''}>Live</a>
        <a href="#/tests" className={tab === 'tests' ? 'on' : ''}>Reports</a>
        <a href="#/events" className={tab === 'events' ? 'on' : ''}>Faults</a>
      </nav>
    </>
  );
}

createRoot(document.getElementById('root')).render(<StrictMode><App /></StrictMode>);

if ('serviceWorker' in navigator && import.meta.env.PROD) {
  addEventListener('load', () => navigator.serviceWorker.register('./sw.js'));
}
