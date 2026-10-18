"use client";

// ============================================================================
// Line chart of per-minute traffic or loss for Dev Tools. One line per device
// in the chosen group (pillars, wristbands, MainNode) or system-wide totals,
// with a crosshair tooltip and a table view.
// ============================================================================

import React, { useEffect, useMemo, useRef, useState } from "react";
import { formatBytes } from "@/lib/activity";
import type { NodeActivity, NodeKind } from "@/lib/types";

// Categorical palette stepped for the dark panel (#252525); passes the
// lightness, chroma, CVD and contrast checks. Assigned in fixed order.
const SERIES_COLORS = ["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#008300", "#9085e9", "#e66767"];
const MAX_SERIES = SERIES_COLORS.length;

type Group = "all" | NodeKind;
type Metric = "packets" | "loss";

const GROUPS: { id: Group; label: string }[] = [
  { id: "all", label: "รวมทั้งระบบ" },
  { id: "pin", label: "เสา" },
  { id: "band", label: "กำไล" },
  { id: "hub", label: "MainNode" },
];
const METRICS: { id: Metric; label: string; unit: string }[] = [
  { id: "packets", label: "แพ็กเก็ต/นาที", unit: "" },
  { id: "loss", label: "หลุด %", unit: "%" },
];

interface Series {
  key: string;
  label: string;
  color: string;
  values: (number | null)[];   // null = no data for that minute (line breaks)
  bytes: number[];
}

const H = 220;
const PAD = { top: 12, right: 12, bottom: 22, left: 40 };

function niceMax(v: number): number {
  if (v <= 0) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  for (const m of [1, 2, 2.5, 5, 10]) if (m * p >= v) return m * p;
  return 10 * p;
}

function perMinute(n: NodeActivity, first: number, count: number) {
  const rx = new Array<number>(count).fill(0);
  const lost = new Array<number>(count).fill(0);
  const bytes = new Array<number>(count).fill(0);
  for (const b of n.buckets) {
    const i = Math.floor((b.t - first) / 60_000);
    if (i < 0 || i >= count) continue;
    rx[i] += b.rx;
    lost[i] += b.lost;
    bytes[i] += b.bytes;
  }
  return { rx, lost, bytes };
}

function buildSeries(nodes: NodeActivity[], group: Group, metric: Metric, minutes: number[]): { series: Series[]; hidden: number } {
  const first = minutes[0];
  const count = minutes.length;
  const lossOf = (rx: number, lost: number) => (rx + lost > 0 ? (lost / (rx + lost)) * 100 : null);

  if (group === "all") {
    const rx = new Array<number>(count).fill(0);
    const lost = new Array<number>(count).fill(0);
    const bytes = new Array<number>(count).fill(0);
    for (const n of nodes) {
      const m = perMinute(n, first, count);
      for (let i = 0; i < count; i++) { rx[i] += m.rx[i]; lost[i] += m.lost[i]; bytes[i] += m.bytes[i]; }
    }
    // Before the first device appeared there is no data, not zero traffic.
    const firstSeen = Math.min(...nodes.map((n) => n.firstSeen));
    const known = (i: number) => minutes[i] + 60_000 > firstSeen;
    const series: Series[] = metric === "packets"
      ? [
          { key: "rx", label: "ได้รับ", color: SERIES_COLORS[0], values: rx.map((v, i) => (known(i) ? v : null)), bytes },
          { key: "lost", label: "หลุด", color: SERIES_COLORS[1], values: lost.map((v, i) => (known(i) ? v : null)), bytes },
        ]
      : [{ key: "loss", label: "หลุด % ทั้งระบบ", color: SERIES_COLORS[0], values: rx.map((r, i) => lossOf(r, lost[i])), bytes }];
    return { series, hidden: 0 };
  }

  const members = nodes
    .filter((n) => n.kind === group)
    .sort((a, b) => a.key.localeCompare(b.key, undefined, { numeric: true }));
  const shown = members.slice(0, MAX_SERIES);
  const series = shown.map((n, idx) => {
    const m = perMinute(n, first, count);
    // Pillars keep the colour of their slot so a pillar is the same colour
    // whichever pillars happen to be online.
    const slot = n.kind === "pin" ? Number(n.key.slice(4)) % MAX_SERIES : idx;
    const values = m.rx.map((r, i) => {
      if (minutes[i] + 60_000 <= n.firstSeen) return null;   // device not known yet
      return metric === "packets" ? r : lossOf(r, m.lost[i]);
    });
    return { key: n.key, label: n.label, color: SERIES_COLORS[slot], values, bytes: m.bytes };
  });
  return { series, hidden: members.length - shown.length };
}

function linePath(values: (number | null)[], x: (i: number) => number, y: (v: number) => number): string {
  let d = "";
  let pen = false;
  values.forEach((v, i) => {
    if (v === null) { pen = false; return; }
    d += `${pen ? "L" : "M"}${x(i).toFixed(1)},${y(v).toFixed(1)}`;
    pen = true;
  });
  return d;
}

export function TrendChart({ nodes, now, windowMin }: { nodes: NodeActivity[]; now: number; windowMin: number }) {
  const [group, setGroup] = useState<Group>("all");
  const [metric, setMetric] = useState<Metric>("packets");
  const [hover, setHover] = useState<number | null>(null);
  const boxRef = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(800);

  useEffect(() => {
    const el = boxRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([e]) => setWidth(Math.max(320, Math.round(e.contentRect.width))));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const minutes = useMemo(() => {
    const currentMin = now - (now % 60_000);
    return Array.from({ length: windowMin }, (_, i) => currentMin - (windowMin - 1 - i) * 60_000);
  }, [now, windowMin]);

  const { series, hidden } = useMemo(() => buildSeries(nodes, group, metric, minutes), [nodes, group, metric, minutes]);
  const unit = METRICS.find((m) => m.id === metric)!.unit;

  const yMax = useMemo(() => {
    let m = 0;
    for (const s of series) for (const v of s.values) if (v !== null && v > m) m = v;
    return metric === "loss" ? Math.min(100, niceMax(Math.max(m, 5))) : niceMax(Math.max(m, 4));
  }, [series, metric]);

  const plotW = width - PAD.left - PAD.right;
  const plotH = H - PAD.top - PAD.bottom;
  const step = minutes.length > 1 ? plotW / (minutes.length - 1) : plotW;
  const x = (i: number) => PAD.left + i * step;
  const y = (v: number) => PAD.top + plotH - (v / yMax) * plotH;
  const ticks = [0, yMax / 2, yMax];
  const fmt = (v: number) => (metric === "loss" ? `${v.toFixed(1)}%` : `${Math.round(v)}`);
  const timeLabel = (t: number) => new Date(t).toLocaleTimeString("th-TH", { hour: "2-digit", minute: "2-digit" });

  function onPointerMove(e: React.PointerEvent<SVGRectElement>) {
    const rect = e.currentTarget.getBoundingClientRect();
    const px = e.clientX - rect.left;
    setHover(Math.max(0, Math.min(minutes.length - 1, Math.round(px / step))));
  }

  const hasData = series.length > 0;

  return (
    <div>
      {/* Controls: one row above the chart */}
      <div className="mb-2 flex flex-wrap items-center gap-3 font-sans">
        <div className="flex items-center gap-1">
          {GROUPS.map((g) => (
            <button key={g.id} onClick={() => setGroup(g.id)}
              className={`btn btn-sm ${group === g.id ? "btn-accent" : "btn-ghost border border-app-border"}`}>
              {g.label}
            </button>
          ))}
        </div>
        <div className="flex items-center gap-1">
          {METRICS.map((m) => (
            <button key={m.id} onClick={() => setMetric(m.id)}
              className={`btn btn-sm ${metric === m.id ? "btn-accent" : "btn-ghost border border-app-border"}`}>
              {m.label}
            </button>
          ))}
        </div>
      </div>

      {/* Legend: line keys, always present for 2+ series */}
      {series.length > 1 && (
        <div className="mb-1.5 flex flex-wrap gap-x-4 gap-y-1 text-2xs text-app-dim">
          {series.map((s) => (
            <span key={s.key} className="flex items-center gap-1.5">
              <span className="inline-block h-0.5 w-4 rounded-full" style={{ backgroundColor: s.color }} />
              {s.label}
            </span>
          ))}
          {hidden > 0 && <span className="text-app-muted">+{hidden} เครื่องไม่แสดง</span>}
        </div>
      )}

      <div ref={boxRef} className="relative">
        {!hasData ? (
          <div className="py-10 text-center text-xs text-app-muted">ยังไม่มีอุปกรณ์ในกลุ่มนี้</div>
        ) : (
          <svg width={width} height={H} role="img"
               aria-label={`${METRICS.find((m) => m.id === metric)!.label} ${windowMin} นาทีล่าสุด`}>
            {/* Recessive grid + y axis labels */}
            {ticks.map((t) => (
              <g key={t}>
                <line x1={PAD.left} x2={width - PAD.right} y1={y(t)} y2={y(t)}
                      stroke="#3a3a3a" strokeWidth={1} strokeDasharray={t === 0 ? undefined : "2 3"} />
                <text x={PAD.left - 6} y={y(t)} dy="0.32em" textAnchor="end" className="fill-app-muted font-mono" fontSize={10}>
                  {fmt(t)}
                </text>
              </g>
            ))}
            {/* X axis: start, middle, now */}
            {[0, Math.floor((minutes.length - 1) / 2), minutes.length - 1].map((i, k) => (
              <text key={k} x={x(i)} y={H - 6} textAnchor={k === 0 ? "start" : k === 2 ? "end" : "middle"}
                    className="fill-app-muted font-mono" fontSize={10}>
                {k === 2 ? "ตอนนี้" : timeLabel(minutes[i])}
              </text>
            ))}

            {/* The current minute is still filling up, so its segment is dashed */}
            {series.map((s) => {
              const last = s.values.length - 1;
              const prev = s.values[last - 1];
              const cur = s.values[last];
              return (
                <g key={s.key}>
                  <path d={linePath(s.values.slice(0, last), x, y)} fill="none" stroke={s.color}
                        strokeWidth={2} strokeLinejoin="round" strokeLinecap="round" />
                  {prev != null && cur != null && (
                    <path d={`M${x(last - 1)},${y(prev)}L${x(last)},${y(cur)}`} fill="none" stroke={s.color}
                          strokeWidth={2} strokeDasharray="3 4" strokeLinecap="round" opacity={0.7} />
                  )}
                </g>
              );
            })}

            {/* Single-point minutes would be invisible as a path - draw a dot */}
            {series.map((s) => s.values.map((v, i) => {
              const isolated = v !== null && (s.values[i - 1] ?? null) === null && (s.values[i + 1] ?? null) === null;
              return isolated ? <circle key={`${s.key}-${i}`} cx={x(i)} cy={y(v)} r={2.5} fill={s.color} /> : null;
            }))}

            {/* Crosshair */}
            {hover !== null && (
              <g pointerEvents="none">
                <line x1={x(hover)} x2={x(hover)} y1={PAD.top} y2={PAD.top + plotH} stroke="#8a8a8a" strokeWidth={1} />
                {series.map((s) => {
                  const v = s.values[hover];
                  return v === null ? null : (
                    <circle key={s.key} cx={x(hover)} cy={y(v)} r={4} fill={s.color} stroke="#252525" strokeWidth={2} />
                  );
                })}
              </g>
            )}

            {/* Hit area: the whole plot, snapping to the nearest minute */}
            <rect x={PAD.left} y={PAD.top} width={plotW} height={plotH} fill="transparent"
                  onPointerMove={onPointerMove} onPointerLeave={() => setHover(null)} />
          </svg>
        )}

        {hasData && hover !== null && (
          <div className="pointer-events-none absolute top-2 z-10 min-w-[11rem] rounded border border-app-border bg-app-surface/95 p-2 text-2xs shadow-lg"
               style={x(hover) > width / 2 ? { right: width - x(hover) + 12 } : { left: x(hover) + 12 }}>
            <div className="mb-1 font-mono text-app-muted">
              {timeLabel(minutes[hover])}{hover === minutes.length - 1 ? " · นาทีปัจจุบัน (ยังไม่ครบ)" : ""}
            </div>
            {[...series]
              .sort((a, b) => (b.values[hover] ?? -1) - (a.values[hover] ?? -1))
              .map((s) => (
                <div key={s.key} className="flex items-center justify-between gap-3">
                  <span className="flex items-center gap-1.5 text-app-dim">
                    <span className="inline-block h-0.5 w-3 rounded-full" style={{ backgroundColor: s.color }} />
                    {s.label}
                  </span>
                  <span className="font-mono font-semibold text-app-text">
                    {s.values[hover] === null ? "--" : fmt(s.values[hover]!)}
                  </span>
                </div>
              ))}
            {metric === "packets" && (
              <div className="mt-1 border-t border-app-divider pt-1 font-mono text-app-muted">
                {formatBytes(series.reduce((sum, s) => sum + (group === "all" && s.key === "lost" ? 0 : s.bytes[hover]), 0))}
              </div>
            )}
          </div>
        )}
      </div>

      {/* Table view: every value reachable without hovering */}
      {hasData && (
        <details className="mt-2 border-t border-app-divider pt-2 text-2xs">
          <summary className="cursor-pointer text-app-muted hover:text-app-text">ดูเป็นตาราง</summary>
          <div className="mt-2 max-h-56 overflow-auto">
            <table className="w-full border-collapse text-left font-mono">
              <thead>
                <tr className="border-b border-app-divider text-app-muted">
                  <th className="px-2 py-1 font-sans">เวลา</th>
                  {series.map((s) => <th key={s.key} className="px-2 py-1 text-right font-sans">{s.label}</th>)}
                </tr>
              </thead>
              <tbody className="divide-y divide-app-divider">
                {minutes.map((t, i) => (
                  <tr key={t}>
                    <td className="px-2 py-0.5 text-app-dim">{timeLabel(t)}</td>
                    {series.map((s) => (
                      <td key={s.key} className="px-2 py-0.5 text-right text-app-text">
                        {s.values[i] === null ? "--" : `${fmt(s.values[i]!)}`}
                      </td>
                    ))}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </details>
      )}
      {unit === "%" && (
        <div className="mt-1 text-3xs text-app-muted">
          หลุด % = แพ็กเก็ตที่หลุด ÷ (ได้รับ + หลุด) ในนาทีนั้น · ช่องว่างในเส้น = นาทีที่ไม่มีข้อมูล
        </div>
      )}
    </div>
  );
}
