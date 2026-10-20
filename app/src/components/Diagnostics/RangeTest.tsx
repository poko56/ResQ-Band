"use client";

// ============================================================================
// Range test: carry a band to known distances from a receiver (MainNode or a
// pillar), average its RSSI at each, and fit the log-distance path-loss model.
// The result can drive the dashboard's position estimate and gives the
// constants for the handheld's RSSI -> distance readout.
// ============================================================================

import React, { useMemo, useState } from "react";
import { useSessionState } from "@/lib/useSessionState";
import { DEFAULT_PATH_LOSS, useResQ } from "@/lib/store";
import { collectRssi, fitPathLoss, meanSd, predictRssi } from "@/lib/pathloss";

const BAND_TX_DBM = 10;   // LORA_BAND_TX_POWER_DBM in include/ResQConfig.h
const SAMPLE_TARGETS = [3, 5, 10];

function receiverLabel(r: string) {
  return r === "direct" ? "MainNode (ฟังตรง)" : `เสา ${Number(r.slice(4)) + 1}`;
}

export function RangeTest() {
  const log = useResQ((s) => s.packetLog);
  const wristbands = useResQ((s) => s.wristbands);
  const bandDiag = useResQ((s) => s.diagnostics.bands);
  const anchors = useResQ((s) => s.anchors);
  const points = useResQ((s) => s.rangePoints);
  const addPoint = useResQ((s) => s.addRangePoint);
  const removePoint = useResQ((s) => s.removeRangePoint);
  const clearPoints = useResQ((s) => s.clearRangePoints);
  const pathLoss = useResQ((s) => s.pathLoss);
  const setPathLoss = useResQ((s) => s.setPathLoss);

  const bandIds = useMemo(
    () => [...new Set([...Object.keys(bandDiag), ...Object.keys(wristbands)])].sort(),
    [bandDiag, wristbands],
  );
  const receivers = useMemo(
    () => ["direct", ...Object.values(anchors).map((a) => `pin:${a.pinIndex}`).sort()],
    [anchors],
  );

  const [band, setBand] = useSessionState("range:band", "");
  const [receiver, setReceiver] = useSessionState("range:receiver", "direct");
  const [distance, setDistance] = useSessionState("range:distance", "1");
  const [target, setTarget] = useSessionState("range:target", 5);
  // Kept across page changes so a collection keeps counting while you look elsewhere.
  const [since, setSince] = useSessionState<number | null>("range:since", null);

  const activeBand = band || bandIds[0] || "";
  const samples = since ? collectRssi(log, activeBand, receiver, since) : [];
  const stats = meanSd(samples);
  const distM = Number(distance);

  const fit = useMemo(() => fitPathLoss(points), [points]);
  const mixedReceivers = new Set(points.map((p) => p.receiver)).size > 1;
  const nameOf = (id: string) => wristbands[id]?.name ? `${wristbands[id].name} (${id})` : id;

  function save() {
    if (!samples.length || !(distM > 0)) return;
    addPoint({ bandId: activeBand, receiver, distanceM: distM, rssiAvg: stats.mean, rssiSd: stats.sd, samples: samples.length });
    setSince(null);
  }

  return (
    <div className="space-y-3">
      <div className="grid gap-3 lg:grid-cols-[minmax(0,380px)_minmax(0,1fr)]">
        {/* Step 1: measure */}
        <div className="panel">
          <div className="panel-header"><span>1 · วัดค่าที่ระยะจริง</span></div>
          <div className="space-y-2.5 p-3 text-xs">
            <ol className="list-decimal space-y-0.5 pl-4 text-2xs text-app-muted">
              <li>วางกำไลห่างจากเสาอากาศของตัวรับตามระยะที่วัดด้วยตลับเมตร</li>
              <li>กด “เริ่มเก็บ” แล้วรอให้ได้ค่าครบ (กำไลส่งทุก ~10 วินาที)</li>
              <li>กด “บันทึกจุดนี้” แล้วย้ายไประยะถัดไป เช่น 1, 2, 5, 10, 20 ม.</li>
            </ol>
            <label className="block">
              <span className="text-2xs text-app-muted">กำไลที่ใช้ทดสอบ</span>
              <select value={activeBand} onChange={(e) => { setBand(e.target.value); setSince(null); }}
                className="mt-0.5 h-7 w-full rounded-sm border border-app-border bg-app-input px-2 text-xs text-app-text">
                {bandIds.length === 0 && <option value="">ยังไม่ได้ยินกำไลเลย</option>}
                {bandIds.map((id) => <option key={id} value={id}>{nameOf(id)}</option>)}
              </select>
            </label>
            <label className="block">
              <span className="text-2xs text-app-muted">ตัวรับ (วัด RSSI ที่)</span>
              <select value={receiver} onChange={(e) => { setReceiver(e.target.value); setSince(null); }}
                className="mt-0.5 h-7 w-full rounded-sm border border-app-border bg-app-input px-2 text-xs text-app-text">
                {receivers.map((r) => <option key={r} value={r}>{receiverLabel(r)}</option>)}
              </select>
            </label>
            <div className="flex gap-2">
              <label className="block flex-1">
                <span className="text-2xs text-app-muted">ระยะจริง (เมตร)</span>
                <input type="number" min={0.1} step={0.1} value={distance} onChange={(e) => setDistance(e.target.value)}
                  className="mt-0.5 h-7 w-full rounded-sm border border-app-border bg-app-input px-2 font-mono text-xs text-app-text" />
              </label>
              <label className="block">
                <span className="text-2xs text-app-muted">จำนวนค่า</span>
                <select value={target} onChange={(e) => setTarget(Number(e.target.value))}
                  className="mt-0.5 h-7 rounded-sm border border-app-border bg-app-input px-2 text-xs text-app-text">
                  {SAMPLE_TARGETS.map((t) => <option key={t} value={t}>{t}</option>)}
                </select>
              </label>
            </div>

            {since === null ? (
              <button onClick={() => setSince(Date.now())} disabled={!activeBand || !(distM > 0)}
                className="btn btn-accent h-8 w-full disabled:opacity-40">เริ่มเก็บที่ {distM > 0 ? distM : "?"} ม.</button>
            ) : (
              <div className="space-y-2 rounded border border-app-border bg-app-surface p-2">
                <div className="flex items-baseline justify-between">
                  <span className="text-2xs text-app-muted">ได้ {samples.length}/{target} ค่า</span>
                  <span className="font-mono text-sm text-app-text">
                    {samples.length ? `${stats.mean.toFixed(1)} dBm ±${stats.sd.toFixed(1)}` : "รอ packet…"}
                  </span>
                </div>
                <div className="h-1.5 overflow-hidden rounded bg-app-input">
                  <div className="h-full bg-accent transition-all" style={{ width: `${Math.min(100, (samples.length / target) * 100)}%` }} />
                </div>
                <div className="font-mono text-3xs text-app-muted">{samples.slice(-10).join("  ")}</div>
                <div className="flex gap-2">
                  <button onClick={save} disabled={!samples.length}
                    className={`btn h-7 flex-1 disabled:opacity-40 ${samples.length >= target ? "btn-accent" : "btn-ghost border border-app-border"}`}>
                    บันทึกจุดนี้
                  </button>
                  <button onClick={() => setSince(null)} className="btn btn-ghost h-7 border border-app-border">ยกเลิก</button>
                </div>
              </div>
            )}
          </div>
        </div>

        {/* Step 2: fit */}
        <div className="panel">
          <div className="panel-header justify-between">
            <span>2 · เส้นโค้ง path-loss ที่ได้</span>
            {points.length > 0 && (
              <button onClick={() => confirm("ลบจุดวัดทั้งหมด?") && clearPoints()} className="btn btn-sm btn-ghost border border-app-border hover:text-status-err">ล้างจุด</button>
            )}
          </div>
          <div className="p-3">
            <FitChart points={points} fit={fit} current={pathLoss} />
            {mixedReceivers && (
              <p className="mt-2 rounded border border-amber-800 bg-amber-950/30 px-2 py-1 text-2xs text-amber-300">
                ⚠️ จุดวัดมาจากตัวรับหลายตัว ซึ่งเสาอากาศอาจต่างกัน — ควรวัดกับตัวรับเดียวเพื่อความแม่น
              </p>
            )}
          </div>
        </div>
      </div>

      <div className="grid gap-3 lg:grid-cols-2">
        {/* Points table */}
        <div className="panel">
          <div className="panel-header"><span>จุดที่วัดแล้ว ({points.length})</span></div>
          <table className="w-full text-left text-2xs">
            <thead className="text-app-muted">
              <tr className="border-b border-app-divider">
                <th className="px-3 py-1.5 font-normal">ระยะ</th>
                <th className="px-3 py-1.5 font-normal">RSSI เฉลี่ย</th>
                <th className="px-3 py-1.5 font-normal">ค่าที่โมเดลทำนาย</th>
                <th className="px-3 py-1.5 font-normal">ตัวรับ / กำไล</th>
                <th />
              </tr>
            </thead>
            <tbody className="divide-y divide-app-divider font-mono">
              {points.length === 0 && (
                <tr><td colSpan={5} className="px-3 py-4 text-center font-sans text-app-muted">ยังไม่มีจุดวัด — ต้องมีอย่างน้อย 2 ระยะ</td></tr>
              )}
              {[...points].sort((a, b) => a.distanceM - b.distanceM).map((p) => (
                <tr key={p.id}>
                  <td className="px-3 py-1.5 text-app-text">{p.distanceM} ม.</td>
                  <td className="px-3 py-1.5 text-app-text">{p.rssiAvg.toFixed(1)} ±{p.rssiSd.toFixed(1)} <span className="text-app-muted">(n={p.samples})</span></td>
                  <td className="px-3 py-1.5 text-app-dim">{fit ? `${predictRssi(fit, p.distanceM).toFixed(1)}` : "-"}</td>
                  <td className="px-3 py-1.5 font-sans text-app-dim">{receiverLabel(p.receiver)} · {p.bandId}</td>
                  <td className="px-3 py-1.5 text-right">
                    <button onClick={() => removePoint(p.id)} className="text-app-muted hover:text-status-err" title="ลบจุดนี้">✕</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        {/* Results */}
        <div className="panel">
          <div className="panel-header"><span>3 · นำค่าไปใช้</span></div>
          <div className="space-y-3 p-3 text-xs">
            {!fit ? (
              <p className="text-app-muted">วัดอย่างน้อย 2 ระยะที่ต่างกันเพื่อคำนวณ</p>
            ) : (
              <>
                <div className="grid grid-cols-3 gap-2">
                  <Stat label="RSSI ที่ 1 ม." value={`${fit.rssiAt1m.toFixed(1)} dBm`} />
                  <Stat label="Path-loss n" value={fit.pathLossN.toFixed(2)} hint={fit.pathLossN < 2 ? "ต่ำกว่าอากาศโล่ง: วัดใกล้เกินไปหรือสะท้อน" : fit.pathLossN > 4 ? "สูง: มีสิ่งกีดขวางมาก" : undefined} />
                  <Stat label="ความเข้ากันได้ R²" value={fit.r2.toFixed(3)} hint={fit.r2 < 0.8 ? "ต่ำ: ลองวัดเพิ่ม" : undefined} />
                </div>
                <div>
                  <div className="mb-1 text-2xs text-app-muted">ค่าคงที่สำหรับเครื่องเดินค้นหา (src/resq_node/main.cpp)</div>
                  <pre className="select-text overflow-x-auto rounded bg-app-input p-2 font-mono text-2xs text-app-text">{`static const float RSSI_1M_LOSS = ${(BAND_TX_DBM - fit.rssiAt1m).toFixed(1)}f;   // = ${BAND_TX_DBM} dBm TX - (${fit.rssiAt1m.toFixed(1)} dBm @1m)
static const float PATH_LOSS_N  = ${fit.pathLossN.toFixed(2)}f;`}</pre>
                </div>
                <div className="flex flex-wrap gap-2">
                  <button
                    onClick={() => setPathLoss({ rssiAt1m: fit.rssiAt1m, pathLossN: fit.pathLossN, source: `Range test ${fit.points} จุด, R² ${fit.r2.toFixed(2)}` })}
                    className="btn btn-accent h-7">ใช้กับตำแหน่ง/แผนที่บนเว็บ</button>
                  <button onClick={() => setPathLoss(DEFAULT_PATH_LOSS)} className="btn btn-ghost h-7 border border-app-border">กลับเป็นค่าเริ่มต้น</button>
                </div>
              </>
            )}
            <p className="text-2xs text-app-muted">
              เว็บใช้อยู่ตอนนี้: RSSI@1ม. {pathLoss.rssiAt1m.toFixed(1)} dBm · n {pathLoss.pathLossN.toFixed(2)} · {pathLoss.source ?? "-"}
            </p>
          </div>
        </div>
      </div>
    </div>
  );
}

function Stat({ label, value, hint }: { label: string; value: string; hint?: string }) {
  return (
    <div className="rounded border border-app-border bg-app-surface p-2">
      <div className="text-3xs uppercase tracking-wider text-app-muted">{label}</div>
      <div className="mt-0.5 font-mono text-base font-semibold text-app-text">{value}</div>
      {hint && <div className="mt-0.5 text-3xs text-amber-400">{hint}</div>}
    </div>
  );
}

// Scatter of measured points (log distance) with the fitted and active curves.
function FitChart({ points, fit, current }: {
  points: { distanceM: number; rssiAvg: number; rssiSd: number }[];
  fit: ReturnType<typeof fitPathLoss>;
  current: { rssiAt1m: number; pathLossN: number };
}) {
  const W = 560, H = 220, P = { l: 44, r: 12, t: 10, b: 26 };
  const maxD = Math.max(50, ...points.map((p) => p.distanceM * 1.5));
  const minD = Math.min(0.5, ...points.map((p) => p.distanceM / 1.5));
  const ys = points.map((p) => p.rssiAvg);
  const yMax = Math.ceil(Math.max(-30, ...ys.map((y) => y + 6)) / 10) * 10;
  const yMin = Math.floor(Math.min(-110, ...ys.map((y) => y - 6)) / 10) * 10;
  const x = (d: number) => P.l + ((Math.log10(d) - Math.log10(minD)) / (Math.log10(maxD) - Math.log10(minD))) * (W - P.l - P.r);
  const y = (v: number) => P.t + ((yMax - v) / (yMax - yMin)) * (H - P.t - P.b);
  const curve = (m: { rssiAt1m: number; pathLossN: number }) => {
    let d = "";
    for (let i = 0; i <= 40; i++) {
      const dist = Math.pow(10, Math.log10(minD) + (i / 40) * (Math.log10(maxD) - Math.log10(minD)));
      d += `${i ? "L" : "M"}${x(dist).toFixed(1)},${y(predictRssi(m, dist)).toFixed(1)}`;
    }
    return d;
  };
  const xTicks = [0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500].filter((t) => t >= minD && t <= maxD);
  const yTicks: number[] = [];
  for (let v = yMax; v >= yMin; v -= 20) yTicks.push(v);

  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="mx-auto h-auto w-full max-w-[680px]" role="img" aria-label="RSSI เทียบระยะทาง">
      <rect x={P.l} y={P.t} width={W - P.l - P.r} height={H - P.t - P.b} fill="#181818" />
      {yTicks.map((v) => (
        <g key={v}>
          <line x1={P.l} x2={W - P.r} y1={y(v)} y2={y(v)} stroke="#2d2d2d" />
          <text x={P.l - 6} y={y(v)} dy="0.32em" textAnchor="end" fontSize={10} className="fill-app-muted font-mono">{v}</text>
        </g>
      ))}
      {xTicks.map((t) => (
        <g key={t}>
          <line x1={x(t)} x2={x(t)} y1={P.t} y2={H - P.b} stroke="#2d2d2d" />
          <text x={x(t)} y={H - 8} textAnchor="middle" fontSize={10} className="fill-app-muted font-mono">{t}m</text>
        </g>
      ))}
      <path d={curve(current)} fill="none" stroke="#6a6a6a" strokeWidth={1.5} strokeDasharray="4 4" />
      {fit && <path d={curve(fit)} fill="none" stroke="#3987e5" strokeWidth={2} />}
      {points.map((p, i) => (
        <g key={i}>
          <line x1={x(p.distanceM)} x2={x(p.distanceM)} y1={y(p.rssiAvg + p.rssiSd)} y2={y(p.rssiAvg - p.rssiSd)} stroke="#d95926" strokeWidth={1.5} />
          <circle cx={x(p.distanceM)} cy={y(p.rssiAvg)} r={4.5} fill="#d95926" stroke="#252525" strokeWidth={2}>
            <title>{`${p.distanceM} ม.: ${p.rssiAvg.toFixed(1)} ±${p.rssiSd.toFixed(1)} dBm`}</title>
          </circle>
        </g>
      ))}
      <g fontSize={10} className="fill-app-dim">
        <line x1={W - 210} x2={W - 196} y1={P.t + 10} y2={P.t + 10} stroke="#3987e5" strokeWidth={2} />
        <text x={W - 192} y={P.t + 10} dy="0.32em">เส้นที่คำนวณได้</text>
        <line x1={W - 110} x2={W - 96} y1={P.t + 10} y2={P.t + 10} stroke="#6a6a6a" strokeDasharray="4 4" strokeWidth={1.5} />
        <text x={W - 92} y={P.t + 10} dy="0.32em">ค่าที่เว็บใช้อยู่</text>
      </g>
    </svg>
  );
}
