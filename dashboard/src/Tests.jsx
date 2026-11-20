import { useEffect, useState } from 'react';
import { get, isLocalMode, recall, remember } from './api.js';
import { clock, fmt, Skeleton, when } from './ui.jsx';

const tone = (e) => (e === 'Completed' ? 'ok' : /fault/i.test(e) ? 'bad' : 'warn');

export default function Tests({ machine }) {
  const [rows, setRows] = useState(() => recall(`tests.${machine}`));
  const [cursor, setCursor] = useState(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');

  useEffect(() => {
    get(`/machines/${machine}/tests`).then((r) => {
      setRows(r.tests);
      setCursor(r.cursor);
      remember(`tests.${machine}`, r.tests);
    }).catch((e) => setErr(e.message));
  }, [machine]);

  const more = async () => {
    setBusy(true);
    try {
      const r = await get(`/machines/${machine}/tests?cursor=${encodeURIComponent(cursor)}`);
      setRows((x) => [...x, ...r.tests]);
      setCursor(r.cursor);
    } catch (e) { setErr(e.message); }
    setBusy(false);
  };

  if (!rows) return <div className="stack"><Skeleton h={44} /><Skeleton /><Skeleton /><Skeleton /></div>;
  return (
    <div className="stack">
      <header className="top"><h1>Test reports{isLocalMode() ? ' · Local' : ''}</h1></header>
      {err && <p className="warnbar">{err}</p>}
      {!rows.length && <p className="empty">No tests yet. Finished tests appear here automatically.</p>}
      <ul className="list">
        {rows.map((t) => (
          <li key={t.id}>
            <a href={`#/tests/${t.id}`}>
              <div className="row between"><b>Test #{t.id}</b><span className={`tag ${tone(t.end)}`}>{t.end}</span></div>
              <div className="row between small">
                <span>{t.profile?.name}</span><span>{when(t.start)}</span>
              </div>
              <div className="row between small">
                <span>{fmt(t.rpmAvg)} RPM avg</span><span>{clock(t.dur)}{t.sim ? ' · simulated' : ''}</span>
              </div>
            </a>
          </li>
        ))}
      </ul>
      {cursor && <button className="btn" onClick={more} disabled={busy}>{busy ? 'Loading…' : 'Load more'}</button>}
    </div>
  );
}
