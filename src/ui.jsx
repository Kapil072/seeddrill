// Shared pieces. Charts are hand-rolled SVG: a chart library would outweigh the rest
// of the app, and only one form (time series) is needed.
import { useEffect, useRef, useState } from 'react';

export const fmt = (x, d = 1, scale = d) => (x == null ? '-' : (x / 10 ** scale).toFixed(d));
export const clock = (s) => (s == null ? '--:--' : `${String(Math.floor(s / 60)).padStart(2, '0')}:${String(Math.floor(s % 60)).padStart(2, '0')}`);
export const when = (u) => (u ? new Date(u * 1000).toLocaleString('en-IN', { day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit', hour12: false }) : '-');

const PILL = {
  ready: ['idle', 'Ready'], checking: ['info', 'Pre-check'], running: ['ok', 'Running'], stopping: ['warn', 'Stopping'],
  complete: ['ok', 'Complete'], fault: ['bad', 'Fault'], offline: ['idle', 'Offline'],
};
export const StatePill = ({ st }) => {
  const [k, t] = PILL[st] ?? PILL.offline;
  return <span className={`pill ${k}`}><i />{t}</span>;
};

export function Gauge({ rpm, target, max = 120, label }) {
  const r = 86, c = 2 * Math.PI * r, span = 0.75;
  const f = Math.min(1, (rpm ?? 0) / 10 / max);
  return (
    <div className="gauge" role="img" aria-label={`${label}: ${fmt(rpm)} RPM, target ${fmt(target)}`}>
      <svg viewBox="0 0 200 200">
        <circle cx="100" cy="100" r={r} className="track" strokeDasharray={`${c * span} ${c}`} transform="rotate(135 100 100)" />
        <circle cx="100" cy="100" r={r} className="fill" strokeDasharray={`${c * span * f} ${c}`} transform="rotate(135 100 100)" />
      </svg>
      <div className="gauge-in">
        <small>{label}</small>
        <b>{fmt(rpm)}</b>
        <small>Target {fmt(target)} RPM</small>
      </div>
    </div>
  );
}

const useWidth = () => {
  const ref = useRef(null);
  const [w, setW] = useState(320);
  useEffect(() => {
    if (!ref.current) return undefined;
    const o = new ResizeObserver(([e]) => setW(Math.round(e.contentRect.width)));
    o.observe(ref.current);
    return () => o.disconnect();
  }, []);
  return [ref, w];
};

const nice = (m) => {
  const p = 10 ** Math.floor(Math.log10(m || 1));
  return [1, 2, 2.5, 5, 10].map((k) => k * p).find((s) => s * 4 >= m) ?? p * 10;
};

// One y axis, always. series: [{ name, color, dashed, values }], xs: seconds.
export function LineChart({ xs, series, unit, height = 180, digits = 1 }) {
  const [ref, w] = useWidth();
  const [hover, setHover] = useState(null);
  const L = 34, R = 10, T = 8, B = 22;
  const iw = Math.max(40, w - L - R), ih = height - T - B;
  const top = Math.max(1, ...series.flatMap((s) => s.values));
  const step = nice(top * 1.1);
  const ymax = step * 4;
  const x1 = xs[xs.length - 1] || 1, x0 = xs[0] || 0;
  const px = (t) => L + (iw * (t - x0)) / ((x1 - x0) || 1);
  const py = (v) => T + ih - (ih * v) / ymax;
  const path = (vs) => vs.map((v, i) => `${i ? 'L' : 'M'}${px(xs[i]).toFixed(1)} ${py(v).toFixed(1)}`).join('');

  const move = (e) => {
    const b = e.currentTarget.getBoundingClientRect();
    const t = x0 + ((e.clientX - b.left - L) / iw) * (x1 - x0);
    let lo = 0, hi = xs.length - 1;
    while (lo < hi) { const m = (lo + hi) >> 1; if (xs[m] < t) lo = m + 1; else hi = m; }
    setHover(lo);
  };
  const tipLeft = hover != null ? Math.min(Math.max(px(xs[hover]) - 60, 0), Math.max(0, w - 130)) : 0;

  return (
    <div className="chart" ref={ref}>
      {series.length > 1 && (
        <div className="legend">
          {series.map((s) => <span key={s.name}><svg width="18" height="6"><line x1="0" y1="3" x2="18" y2="3" stroke={s.color} strokeWidth="2" strokeDasharray={s.dashed ? '4 3' : undefined} /></svg>{s.name}</span>)}
        </div>
      )}
      <svg width={w} height={height} onPointerMove={move} onPointerDown={move} onPointerLeave={() => setHover(null)} style={{ touchAction: 'pan-y' }}>
        {[0, 1, 2, 3, 4].map((i) => (
          <g key={i}>
            <line x1={L} x2={w - R} y1={py(step * i)} y2={py(step * i)} className="grid" />
            <text x={L - 6} y={py(step * i) + 4} textAnchor="end" className="tick">{step * i}</text>
          </g>
        ))}
        <text x={L} y={height - 5} className="tick">{clock(x0)}</text>
        <text x={w - R} y={height - 5} textAnchor="end" className="tick">{clock(x1)}</text>
        {series.map((s) => <path key={s.name} d={path(s.values)} fill="none" stroke={s.color} strokeWidth="2" strokeLinejoin="round" strokeLinecap="round" strokeDasharray={s.dashed ? '5 4' : undefined} />)}
        {hover != null && <line x1={px(xs[hover])} x2={px(xs[hover])} y1={T} y2={T + ih} className="cross" />}
        {hover != null && series.map((s) => <circle key={s.name} cx={px(xs[hover])} cy={py(s.values[hover])} r="4" fill={s.color} stroke="var(--card)" strokeWidth="2" />)}
      </svg>
      {hover != null && (
        <div className="tip" style={{ left: tipLeft }}>
          <b>{clock(xs[hover])}</b>
          {series.map((s) => <div key={s.name}>{s.name} <b>{s.values[hover].toFixed(digits)}</b> {unit}</div>)}
        </div>
      )}
    </div>
  );
}

export const Metric = ({ label, value, unit }) => (
  <div className="metric"><small>{label}</small><div><b>{value}</b>{unit && <span> {unit}</span>}</div></div>
);

export const Skeleton = ({ h = 72 }) => <div className="skeleton" style={{ height: h }} />;

// Interval polling that pauses while the page is hidden, to save the phone's battery.
export function usePoll(fn, ms) {
  const f = useRef(fn);
  f.current = fn;
  useEffect(() => {
    let t, stop = false;
    const tick = async () => {
      if (!document.hidden) await f.current().catch(() => {});
      if (!stop) t = setTimeout(tick, ms);
    };
    tick();
    const wake = () => { if (!document.hidden) { clearTimeout(t); tick(); } };
    document.addEventListener('visibilitychange', wake);
    return () => { stop = true; clearTimeout(t); document.removeEventListener('visibilitychange', wake); };
  }, [ms]);
}
