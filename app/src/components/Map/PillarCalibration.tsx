"use client";

// ============================================================================
// Multi-point pillar calibration on the live map. The operator leaves a band at
// a spot they can find on the map, waits for a few reports, clicks that spot,
// and repeats at several spots. The panel shows how far off the position
// estimate is at each spot before and after the fitted correction, and
// "ใช้ค่านี้" saves the per-pillar offsets (and n, when the points pin it down).
// ============================================================================

import { Fragment, useEffect, useMemo, useState } from "react";
import { CircleMarker, Polyline, Tooltip, useMapEvents } from "react-leaflet";
import { useResQ } from "@/lib/store";
import { estimatePoint, fitPillarCal } from "@/lib/pillarCal";
import type { LatLng } from "@/lib/types";

export function PillarCalClickHandler({ active, onPick }: { active: boolean; onPick: (p: LatLng) => void }) {
  useMapEvents({
    click(e) {
      if (active) onPick({ lat: e.latlng.lat, lng: e.latlng.lng });
    },
  });
  return null;
}

function usePreview() {
  const points = useResQ((s) => s.pillarCalPoints);
  const anchors = useResQ((s) => s.anchors);
  const model = useResQ((s) => s.pathLoss);
  return useMemo(() => {
    const fit = fitPillarCal(points, anchors, model);
    return { points, anchors, model, fit };
  }, [points, anchors, model]);
}

// True spots (numbered) and, once a fit exists, a dashed line to where the
// corrected estimate would put each one.
export function PillarCalLayer() {
  const { points, anchors, model, fit } = usePreview();
  return (
    <>
      {points.map((p, i) => {
        const est = fit.ok ? estimatePoint(p, anchors, fit.offsets, { ...model, pathLossN: fit.n }) : undefined;
        return (
          <Fragment key={p.id}>
            <CircleMarker center={[p.truePos.lat, p.truePos.lng]} radius={7}
              pathOptions={{ color: "#5dade2", weight: 2, fillOpacity: 0.6 }}>
              <Tooltip permanent direction="top" offset={[0, -6]}>{i + 1}</Tooltip>
            </CircleMarker>
            {est && (
              <>
                <Polyline positions={[[p.truePos.lat, p.truePos.lng], [est.lat, est.lng]]}
                  pathOptions={{ color: "#d4a235", weight: 1.5, dashArray: "4 4" }} />
                <CircleMarker center={[est.lat, est.lng]} radius={4}
                  pathOptions={{ color: "#d4a235", weight: 1, fillOpacity: 0.9 }} />
              </>
            )}
          </Fragment>
        );
      })}
    </>
  );
}

export function PillarCalPanel({ open, setOpen, picking, setPicking, picked, clearPicked }: {
  open: boolean;
  setOpen: (v: boolean) => void;
  picking: boolean;
  setPicking: (v: boolean) => void;
  picked: LatLng | null;
  clearPicked: () => void;
}) {
  const sightings = useResQ((s) => s.sightings);
  const wristbands = useResQ((s) => s.wristbands);
  const addPoint = useResQ((s) => s.addPillarCalPoint);
  const removePoint = useResQ((s) => s.removePillarCalPoint);
  const clearPoints = useResQ((s) => s.clearPillarCalPoints);
  const apply = useResQ((s) => s.applyPillarCal);
  const clearOffsets = useResQ((s) => s.clearPillarOffsets);
  const { points, anchors, model, fit } = usePreview();
  const [bandId, setBandId] = useState("");
  const [msg, setMsg] = useState("");

  const now = Date.now();
  const bands = Object.values(sightings)
    .map((s) => ({ id: s.wristbandId, pins: Object.values(s.rssiPerPin).filter((p) => now - p.timestamp < 60000).length }))
    .filter((b) => b.pins > 0)
    .sort((a, b) => b.pins - a.pins);
  const selected = bandId && bands.some((b) => b.id === bandId) ? bandId : bands[0]?.id ?? "";
  const hasOffsets = Object.values(anchors).some((a) => a.rssiOffsetDb !== undefined);
  const nameOf = (id: string) => anchors[id]?.name ?? id;

  useEffect(() => {
    if (!picked) return;
    clearPicked();
    setPicking(false);
    const r = selected ? addPoint(selected, picked) : { ok: false, error: "ยังไม่มีเสาได้ยินกำไล" };
    setMsg(r.ok ? "" : r.error ?? "");
  }, [picked, selected, addPoint, clearPicked, setPicking]);

  if (!open) {
    return (
      <div className="absolute left-3 top-[88px] z-[1000]">
        <button className="btn btn-ghost h-7 border border-app-border bg-app-panel/95 px-2 text-2xs" onClick={() => setOpen(true)}>
          ปรับเทียบเสา{points.length ? ` (${points.length} จุด)` : ""}{hasOffsets ? " ✓" : ""}
        </button>
      </div>
    );
  }

  const fmt = (m?: number) => (m === undefined ? "--" : `${m.toFixed(1)}m`);
  const perPoint = new Map(fit.perPoint.map((p) => [p.id, p]));

  return (
    <div className="absolute left-3 top-[88px] z-[1000] max-h-[calc(100%-100px)] w-80 overflow-y-auto rounded border border-app-border bg-app-panel/95 p-2 text-2xs text-app-text shadow-lg">
      <div className="mb-1.5 flex items-center justify-between">
        <span className="font-semibold">ปรับเทียบเสาหลายจุด</span>
        <button className="text-app-muted hover:text-app-text" onClick={() => { setOpen(false); setPicking(false); }}>✕</button>
      </div>

      {picking ? (
        <div className="space-y-1.5">
          <div>วางกำไลนิ่ง ๆ ไว้ 30 วิขึ้นไป แล้ว<b>คลิกบนแผนที่ตรงจุดที่กำไลอยู่จริง</b></div>
          <select className="w-full rounded border border-app-border bg-app-input p-1" value={selected} onChange={(e) => setBandId(e.target.value)}>
            {bands.length === 0 && <option value="">ยังไม่มีเสาได้ยินกำไล</option>}
            {bands.map((b) => (
              <option key={b.id} value={b.id}>{wristbands[b.id]?.name ?? b.id} · เสาได้ยิน {b.pins}</option>
            ))}
          </select>
          <button className="btn btn-ghost h-7 w-full border border-app-border" onClick={() => setPicking(false)}>ยกเลิก</button>
        </div>
      ) : (
        <div className="text-app-muted">
          ย้ายกำไลไปหลาย ๆ จุด (ใกล้เสาต่าง ๆ, กลางวง, ขอบ) แล้วเพิ่มทีละจุด ยิ่งหลายจุดยิ่งแม่น แนะนำ 4–8 จุด
          <button className="btn btn-ghost mt-1.5 h-7 w-full border border-app-border text-app-text" onClick={() => { setMsg(""); setPicking(true); }}>
            + เพิ่มจุด
          </button>
        </div>
      )}
      {msg && <div className="mt-1.5 text-triage-yellow">{msg}</div>}

      {points.length > 0 && (
        <table className="mt-2 w-full font-mono">
          <thead>
            <tr className="text-app-muted">
              <th className="text-left">#</th><th className="text-left">กำไล</th><th>เสา</th><th>คลาด·เดิม</th><th>คลาด·ใหม่</th><th />
            </tr>
          </thead>
          <tbody>
            {points.map((p, i) => {
              const pp = perPoint.get(p.id);
              return (
                <tr key={p.id}>
                  <td>{i + 1}</td>
                  <td className="truncate">{wristbands[p.bandId]?.name ?? p.bandId.slice(0, 4)}</td>
                  <td className="text-right">{p.readings.length}</td>
                  <td className="text-right">{fmt(pp?.beforeM)}</td>
                  <td className="text-right text-triage-green">{fmt(pp?.afterM)}</td>
                  <td className="text-right">
                    <button className="text-app-muted hover:text-triage-red" onClick={() => removePoint(p.id)} title="ลบจุดนี้">×</button>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      )}

      {fit.ok && (
        <div className="mt-2 space-y-1 border-t border-app-divider pt-1.5">
          <div>
            คลาดเฉลี่ย <b>{fmt(fit.meanBeforeM)}</b> → <b className="text-triage-green">{fmt(fit.meanAfterM)}</b>
          </div>
          <div className="text-app-muted">
            n = {fit.n.toFixed(2)} {fit.nFitted ? "(คำนวณจากจุดที่วัด)" : `(ใช้ค่าเดิม — ต้องมี ≥3 จุดที่ระยะต่างกันพอ)`}
          </div>
          <div className="font-mono text-app-muted">
            {Object.entries(fit.offsets).map(([id, off]) => (
              <span key={id} className="mr-2">{nameOf(id)} {off > 0 ? "+" : ""}{off.toFixed(1)}dB</span>
            ))}
          </div>
          {points.length < 3 && <div className="text-triage-yellow">จุดยังน้อย ผลอาจดีเฉพาะจุดที่วัด เพิ่มจุดอีกจะเชื่อถือได้กว่า</div>}
        </div>
      )}

      <div className="mt-2 grid grid-cols-2 gap-1">
        <button className="btn btn-ghost col-span-2 h-7 border border-app-border" disabled={!fit.ok}
          onClick={() => { const r = apply(); setMsg(r.ok ? "✅ ใช้ค่าชดเชยแล้ว ตำแหน่งจะอัปเดตในรอบรายงานถัดไป" : r.error ?? ""); }}>
          ใช้ค่านี้
        </button>
        <button className="btn btn-ghost h-7 border border-app-border" disabled={points.length === 0}
          onClick={() => { if (confirm("ล้างจุดที่วัดทั้งหมด?")) clearPoints(); }}>ล้างจุด</button>
        <button className="btn btn-ghost h-7 border border-app-border" disabled={!hasOffsets}
          onClick={() => { clearOffsets(); setMsg("ล้างค่าชดเชยแล้ว"); }}>ล้างค่าชดเชย</button>
      </div>
      <div className="mt-1 text-app-muted">ใช้อยู่: RSSI@1m {model.rssiAt1m.toFixed(1)} · n {model.pathLossN.toFixed(2)}</div>
    </div>
  );
}
