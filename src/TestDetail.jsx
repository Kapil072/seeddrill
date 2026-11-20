import { useEffect, useMemo, useState } from 'react';
import { download, get, isLocalMode, localPdfUrl, saveBlob } from './api.js';
import { clock, fmt, LineChart, Metric, Skeleton, when } from './ui.jsx';

// Keeps the SVG light on long tests: at most 300 points, evenly spaced.
const thin = (rows, max = 300) => {
  const step = Math.ceil(rows.length / max) || 1;
  return rows.filter((_, i) => i % step === 0);
};

export const rate = (grams, distDm, p) => {
  const area = (distDm / 10) * p.rows * (p.rowSp / 1000);
  return area > 0 ? (grams / 1000) / (area / 10000) : 0;
};

export default function TestDetail({ machine, id }) {
  const [d, setD] = useState(null);
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState('');

  useEffect(() => {
    get(`/machines/${machine}/tests/${id}`).then(setD).catch((e) => setErr(e.message));
  }, [machine, id]);

  const chart = useMemo(() => (d ? thin(d.samples) : []), [d]);

  const pdf = async () => {
    setBusy('pdf');
    if (isLocalMode()) {
      // In local mode, firmware generates the PDF — just open it.
      window.open(localPdfUrl(id), '_blank');
      setBusy('');
      return;
    }
    const { reportPdf } = await import('./reportPdf.js');
    saveBlob(new Blob([reportPdf(d.test, d.samples, machine)], { type: 'application/pdf' }), `seeddrill-test-${id}.pdf`);
    setBusy('');
  };
  const csv = async () => {
    setBusy('csv');
    await download(`/machines/${machine}/tests/${id}/csv`, `seeddrill-test-${id}.csv`).catch((e) => setErr(e.message));
    setBusy('');
  };

  if (err) return <p className="warnbar">{err}</p>;
  if (!d) return <div className="stack"><Skeleton h={44} /><Skeleton h={200} /><Skeleton /></div>;
  const t = d.test, p = t.profile ?? {};
  const xs = chart.map((r) => r[0]);

  return (
    <div className="stack">
      <header className="top">
        <div><a className="back" href="#/tests">‹ Reports</a><h1>Test #{t.id}</h1><small>{p.name} · {when(t.start)}</small></div>
      </header>
      {!!t.sim && <p className="warnbar">Simulated drive. Not a machine calibration record.</p>}
      <p className={/Completed/.test(t.end) ? 'okbar' : 'faultbar'}>{t.end}</p>

      <section className="grid2">
        <Metric label="Average output" value={fmt(t.rpmAvg)} unit="RPM" />
        <Metric label="Speed accuracy" value={fmt(t.acc)} unit="%" />
        <Metric label="Duration" value={clock(t.dur)} />
        <Metric label="Distance (calculated)" value={fmt(t.dist)} unit="m" />
        <Metric label="Peak current" value={fmt(t.aPeak, 2)} unit="A" />
        <Metric label="Target" value={fmt(t.rpmT)} unit="RPM" />
      </section>

      {xs.length > 2 && (
        <section className="card">
          <h2>Output speed</h2>
          <LineChart xs={xs} unit="RPM" series={[
            { name: 'Actual', color: 'var(--c1)', values: chart.map((r) => r[2] / 10) },
            { name: 'Target', color: 'var(--c2)', dashed: true, values: chart.map((r) => r[1] / 10) }]} />
        </section>
      )}
      {xs.length > 2 && (
        <section className="card">
          <h2>Motor current (A)</h2>
          <LineChart xs={xs} unit="A" height={130} digits={2} series={[{ name: 'Current', color: 'var(--c1)', values: chart.map((r) => r[4] / 100) }]} />
        </section>
      )}

      {(t.seed > 0 || t.fert > 0) && (
        <section className="grid2">
          <Metric label="Seed rate" value={rate(t.seed / 10, t.dist, p).toFixed(1)} unit="kg/ha" />
          <Metric label="Fertilizer rate" value={rate(t.fert / 10, t.dist, p).toFixed(1)} unit="kg/ha" />
        </section>
      )}

      <div className="row">
        <button className="btn pri grow" onClick={pdf} disabled={!!busy}>{busy === 'pdf' ? 'Preparing…' : 'Download PDF'}</button>
        <button className="btn grow" onClick={csv} disabled={!!busy}>{busy === 'csv' ? 'Preparing…' : 'Download CSV'}</button>
      </div>
    </div>
  );
}
