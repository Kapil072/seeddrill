import { useEffect, useState } from 'react';
import { get } from './api.js';
import { Skeleton, when } from './ui.jsx';

export default function Events({ machine }) {
  const [rows, setRows] = useState(null);
  const [err, setErr] = useState('');
  useEffect(() => { get(`/machines/${machine}/events`).then((r) => setRows(r.events)).catch((e) => setErr(e.message)); }, [machine]);
  if (err) return <p className="warnbar">{err}</p>;
  if (!rows) return <div className="stack"><Skeleton h={44} /><Skeleton /><Skeleton /></div>;
  return (
    <div className="stack">
      <header className="top"><h1>Fault log</h1></header>
      {!rows.length && <p className="empty">No faults recorded.</p>}
      <ul className="list">
        {rows.map((e) => (
          <li key={`${e.ts}-${e.fault}`}>
            <div className="pad">
              <div className="row between"><b>{e.text}</b></div>
              <div className="row between small"><span>{e.test ? `Test #${e.test}` : ''}{e.vfd ? ` · VFD code ${e.vfd}` : ''}</span><span>{when(e.ts)}</span></div>
            </div>
          </li>
        ))}
      </ul>
    </div>
  );
}
