import { useRef, useState } from 'react';
import { AuthError, get, post, recall, remember } from './api.js';
import { clock, fmt, Gauge, LineChart, Metric, Skeleton, StatePill, usePoll } from './ui.jsx';

const POLL_MS = 2000;
const TREND_POINTS = 120;
const HOLD_MS = 1200;

// A press-and-hold start: a stray tap in a pocket must not start a machine.
function HoldButton({ onDone, children, disabled }) {
  const [p, setP] = useState(0);
  const t = useRef(null);
  const begin = () => {
    if (disabled) return;
    const t0 = performance.now();
    t.current = setInterval(() => {
      const f = (performance.now() - t0) / HOLD_MS;
      if (f >= 1) { clearInterval(t.current); setP(0); onDone(); } else setP(f);
    }, 30);
  };
  const end = () => { clearInterval(t.current); setP(0); };
  return (
    <button className="btn pri hold" disabled={disabled} onPointerDown={begin} onPointerUp={end} onPointerLeave={end} onPointerCancel={end} onContextMenu={(e) => e.preventDefault()}>
      <span style={{ width: `${p * 100}%` }} />
      <em>{children}</em>
    </button>
  );
}

function Check({ ok, label }) {
  return <li className={ok ? 'ok' : 'bad'}><span aria-hidden>{ok ? '✓' : '✕'}</span>{label}</li>;
}

function Control({ machine, t, operator, online }) {
  const [msg, setMsg] = useState('');
  const [speed, setSpeed] = useState('');
  if (!operator) return <p className="note">You have view-only access.</p>;
  const send = async (body) => {
    setMsg('Sending…');
    try { await post(`/machines/${machine}/cmd`, body); setMsg('Sent to machine'); } catch (e) { setMsg(e.message); }
    setTimeout(() => setMsg(''), 4000);
  };
  const running = t.st === 'running' || t.st === 'stopping';
  const ready = online && t.rem && t.est && t.grd && t.en && t.drv;
  return (
    <section className="card stack">
      <h2>Remote control</h2>
      {!t.rem && <p className="note">Remote control is off. Turn it on at the panel: Settings, Remote control, Allowed.</p>}
      {running
        ? <button className="btn dan" onClick={() => send({ cmd: 'stop' })} disabled={!online}>Stop test</button>
        : <HoldButton disabled={!ready} onDone={() => send({ cmd: 'start' })}>{ready ? 'Hold to start test' : 'Start not available'}</HoldButton>}
      <form className="row" onSubmit={(e) => { e.preventDefault(); const v = Math.round(Number(speed) * 10); if (v > 0) send({ cmd: 'setpoint', value: v }); }}>
        <label className="grow">Set speed (km/h or RPM, per panel mode)
          <input inputMode="decimal" value={speed} onChange={(e) => setSpeed(e.target.value)} placeholder={fmt(t.kmh)} disabled={!t.rem} />
        </label>
        <button className="btn" disabled={!t.rem || !speed || !online}>Apply</button>
      </form>
      {msg && <p className="note" role="status">{msg}</p>}
      <p className="fine">Stops and interlocks are enforced by the machine. Physical emergency stop always takes priority.</p>
    </section>
  );
}

export default function Live({ machine, onSignOut }) {
  const [d, setD] = useState(() => recall(`live.${machine}`));
  const [err, setErr] = useState('');
  const trend = useRef({ xs: [], rpm: [], tgt: [], amps: [] });

  usePoll(async () => {
    try {
      const r = await get(`/machines/${machine}/live`);
      setD(r);
      setErr('');
      remember(`live.${machine}`, r);
      const t = r.telemetry, h = trend.current;
      if (t && h.xs[h.xs.length - 1] !== t.ts) {
        h.xs.push(t.ts); h.rpm.push(t.rpm / 10); h.tgt.push(t.rpmT / 10); h.amps.push(t.a / 100);
        if (h.xs.length > TREND_POINTS) { h.xs.shift(); h.rpm.shift(); h.tgt.shift(); h.amps.shift(); }
      }
    } catch (e) {
      if (e instanceof AuthError) { onSignOut(); return; }
      setErr('Cannot reach the server. Showing last known data.');
    }
  }, POLL_MS);

  if (!d) return <div className="stack"><Skeleton h={44} /><Skeleton h={260} /><Skeleton /><Skeleton /></div>;
  const t = d.telemetry ?? {};
  const online = d.online;
  const st = online ? t.st : 'offline';
  const active = st === 'running' || st === 'stopping';
  const h = trend.current;
  const t0 = h.xs[0] ?? 0;
  const xs = h.xs.map((x) => x - t0);

  return (
    <div className="stack">
      <header className="top">
        <div><h1>{d.id}</h1><small>{t.sim ? 'Simulated drive' : `Firmware ${d.fw ?? '-'}`}</small></div>
        <StatePill st={st} />
      </header>
      {err && <p className="warnbar" role="status">{err}</p>}
      {!online && <p className="warnbar">Machine offline{d.age != null ? `, last seen ${Math.round(d.age / 60)} min ago` : ''}. Values below are the last received.</p>}
      {t.fault > 0 && <p className="faultbar" role="alert">Machine stopped on a fault. Check the panel, then reset it there.</p>}

      <section className="card">
        <Gauge rpm={t.rpm} target={t.rpmT} label={t.meas ? 'Output RPM (measured)' : 'Output RPM (estimated)'} />
        <div className="progress" role="progressbar" aria-valuenow={t.prog ?? 0} aria-valuemin="0" aria-valuemax="100"><i style={{ width: `${t.prog ?? 0}%` }} /></div>
        <div className="row between small"><span>Elapsed {clock(t.el)}</span><span>Remaining {clock(t.left)}</span></div>
      </section>

      <section className="grid2">
        <Metric label="Simulated speed" value={fmt(t.kmh)} unit="km/h" />
        <Metric label="Distance" value={fmt(t.dist)} unit="m" />
        <Metric label="Motor current" value={fmt(t.a, 1, 2)} unit="A" />
        <Metric label="Drive frequency" value={fmt(t.hz, 2)} unit="Hz" />
      </section>

      {xs.length > 2 && (
        <section className="card">
          <h2>Output speed</h2>
          <LineChart xs={xs} unit="RPM" series={[{ name: 'Actual', color: 'var(--c1)', values: h.rpm }, { name: 'Target', color: 'var(--c2)', dashed: true, values: h.tgt }]} />
        </section>
      )}
      {xs.length > 2 && active && (
        <section className="card">
          <h2>Motor current (A)</h2>
          <LineChart xs={xs} unit="A" height={120} digits={2} series={[{ name: 'Current', color: 'var(--c1)', values: h.amps }]} />
        </section>
      )}

      <section className="card">
        <h2>Interlocks</h2>
        <ul className="checks">
          <Check ok={t.est} label="E-stop released" />
          <Check ok={t.grd} label="Guards closed" />
          <Check ok={t.en} label="Local enable on" />
          <Check ok={t.drv} label="Drive online" />
        </ul>
      </section>

      <Control machine={machine} t={t} operator={d.operator} online={online} />
      <button className="btn ghost" onClick={onSignOut}>Sign out</button>
    </div>
  );
}
