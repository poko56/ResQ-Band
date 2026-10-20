"use client";

// ============================================================================
// Coverage map (client-only, Leaflet). Draws each pillar -> band link the
// pillars reported recently, coloured by RSSI, plus estimated coverage rings
// around every placed pillar from the active path-loss model. Helps decide
// where to put pillars on site and spot weak links.
// ============================================================================

import React, { useEffect, useMemo, useState } from "react";
import { useSessionState } from "@/lib/useSessionState";
import { Circle, CircleMarker, MapContainer, Marker, Polyline, TileLayer, Tooltip, useMap } from "react-leaflet";
import L from "leaflet";
import { rssiToDistanceM, useResQ } from "@/lib/store";
import { DEFAULT_CENTER } from "@/lib/mock";
import { LORA } from "@/lib/lora";
import type { LatLng } from "@/lib/types";

// RSSI bands (dBm), strongest first.
const RSSI_STEPS = [
  { min: -80,  color: "#10b981", label: "ดีมาก ≥ -80" },
  { min: -95,  color: "#84cc16", label: "ดี -80…-95" },
  { min: -110, color: "#f59e0b", label: "พอใช้ -95…-110" },
  { min: -999, color: "#f43f5e", label: `อ่อน < -110 (หลุดที่ ~${LORA.sensitivityDbm})` },
];
const rssiColor = (rssi: number) => RSSI_STEPS.find((s) => rssi >= s.min)!.color;

// Coverage rings: where the predicted RSSI reaches these levels.
const RING_GOOD_DBM = -100;
const RING_EDGE_DBM = LORA.sensitivityDbm + 10;   // 10 dB fade margin

function metersBetween(a: LatLng, b: LatLng): number {
  return L.latLng(a.lat, a.lng).distanceTo(L.latLng(b.lat, b.lng));
}

function pinIcon(label: string) {
  return L.divIcon({ className: "", html: `<div class="resq-anchor">${label}</div>`, iconSize: [26, 26], iconAnchor: [13, 13] });
}

function FitToContent({ points }: { points: LatLng[] }) {
  const map = useMap();
  const key = points.map((p) => `${p.lat.toFixed(5)},${p.lng.toFixed(5)}`).join("|");
  useEffect(() => {
    if (points.length === 0) return;
    if (points.length === 1) { map.setView([points[0].lat, points[0].lng], 18); return; }
    map.fitBounds(L.latLngBounds(points.map((p) => [p.lat, p.lng])), { padding: [40, 40], maxZoom: 19 });
    // Only refit when the set of anchors changes, not on every band move.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, map]);
  return null;
}

export default function CoverageMap() {
  const anchors = useResQ((s) => s.anchors);
  const sightings = useResQ((s) => s.sightings);
  const wristbands = useResQ((s) => s.wristbands);
  const bandDiag = useResQ((s) => s.diagnostics.bands);
  const model = useResQ((s) => s.pathLoss);

  const [showRings, setShowRings] = useSessionState("cov:rings", true);
  const [maxAgeS, setMaxAgeS] = useSessionState("cov:age", 60);
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => { const t = setInterval(() => setNow(Date.now()), 2000); return () => clearInterval(t); }, []);

  const placed = useMemo(() => Object.values(anchors).filter((a) => a.position).sort((a, b) => a.pinIndex - b.pinIndex), [anchors]);

  const links = useMemo(() => {
    const out: { band: string; name: string; pin: number; from: LatLng; to: LatLng; rssi: number; snr: number; ageS: number; estM: number; mapM: number }[] = [];
    for (const [id, d] of Object.entries(bandDiag)) {
      const pos = sightings[id]?.position;
      if (!pos) continue;
      for (const [pinStr, st] of Object.entries(d.pinStats)) {
        const anchor = placed.find((a) => a.pinIndex === Number(pinStr));
        const ageS = (now - st.lastSeen) / 1000;
        if (!anchor || ageS > maxAgeS) continue;
        out.push({
          band: id, name: wristbands[id]?.name ?? id, pin: Number(pinStr),
          from: anchor.position!, to: pos, rssi: st.lastRssi, snr: st.lastSnr, ageS,
          estM: rssiToDistanceM(st.lastRssi, model), mapM: metersBetween(anchor.position!, pos),
        });
      }
    }
    return out.sort((a, b) => a.rssi - b.rssi);
  }, [bandDiag, sightings, placed, wristbands, now, maxAgeS, model]);

  const ringGoodM = rssiToDistanceM(RING_GOOD_DBM, model);
  const ringEdgeM = rssiToDistanceM(RING_EDGE_DBM, model);
  const bandsOnMap = Object.values(sightings).filter((s) => s.position);
  const fitPoints = placed.map((a) => a.position!);
  const center = fitPoints[0] ?? DEFAULT_CENTER;

  if (placed.length === 0) {
    return (
      <div className="panel p-8 text-center text-xs text-app-muted">
        ยังไม่มีเสาที่วางตำแหน่งบนแผนที่ — ไปที่หน้า <span className="text-accent">Anchors</span> หรือแผนที่หน้า Live แล้ววางเสาก่อน
      </div>
    );
  }

  return (
    <div className="grid gap-3 lg:grid-cols-[minmax(0,1fr)_320px]">
      <div className="panel relative h-[62vh] min-h-[380px] overflow-hidden">
        <MapContainer center={[center.lat, center.lng]} zoom={18} className="h-full w-full">
          <TileLayer attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
            url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png" maxZoom={19} />
          <FitToContent points={fitPoints} />

          {showRings && placed.map((a) => (
            <React.Fragment key={`rings-${a.id}`}>
              <Circle center={[a.position!.lat, a.position!.lng]} radius={ringEdgeM}
                pathOptions={{ color: "#f59e0b", weight: 1, dashArray: "4 6", fillOpacity: 0.03 }} />
              <Circle center={[a.position!.lat, a.position!.lng]} radius={ringGoodM}
                pathOptions={{ color: "#10b981", weight: 1, fillOpacity: 0.06 }} />
            </React.Fragment>
          ))}

          {links.map((l) => (
            <Polyline key={`${l.band}-${l.pin}`} positions={[[l.from.lat, l.from.lng], [l.to.lat, l.to.lng]]}
              pathOptions={{ color: rssiColor(l.rssi), weight: 3, opacity: Math.max(0.35, 1 - l.ageS / maxAgeS) }}>
              <Tooltip sticky>
                <div className="text-xs">
                  <b>เสา {l.pin + 1} → {l.name}</b><br />
                  {l.rssi} dBm · SNR {l.snr} dB · {Math.round(l.ageS)} วิที่แล้ว<br />
                  ระยะจาก RSSI ≈ {l.estM.toFixed(0)} ม. · บนแผนที่ {l.mapM.toFixed(0)} ม.
                </div>
              </Tooltip>
            </Polyline>
          ))}

          {placed.map((a) => (
            <Marker key={a.id} position={[a.position!.lat, a.position!.lng]} icon={pinIcon(`P${a.pinIndex + 1}`)}>
              <Tooltip direction="top" offset={[0, -12]}>{a.name} {a.online ? "" : "(offline)"}</Tooltip>
            </Marker>
          ))}

          {bandsOnMap.map((s) => (
            <CircleMarker key={s.wristbandId} center={[s.position!.lat, s.position!.lng]} radius={7}
              pathOptions={{ color: "#252525", weight: 2, fillColor: "#3987e5", fillOpacity: 1 }}>
              <Tooltip direction="top" offset={[0, -6]}>{wristbands[s.wristbandId]?.name ?? s.wristbandId} (ตำแหน่งประมาณ)</Tooltip>
            </CircleMarker>
          ))}
        </MapContainer>
      </div>

      <div className="space-y-3">
        <div className="panel">
          <div className="panel-header"><span>การแสดงผล</span></div>
          <div className="space-y-2 p-3 text-xs">
            <label className="flex items-center gap-2">
              <input type="checkbox" checked={showRings} onChange={(e) => setShowRings(e.target.checked)} />
              วงครอบคลุมโดยประมาณของเสา
            </label>
            <label className="flex items-center justify-between gap-2">
              <span>แสดงลิงก์ที่รายงานภายใน</span>
              <select value={maxAgeS} onChange={(e) => setMaxAgeS(Number(e.target.value))}
                className="h-6 rounded-sm border border-app-border bg-app-input px-1 text-xs text-app-text">
                {[30, 60, 300, 900].map((s) => <option key={s} value={s}>{s < 60 ? `${s} วิ` : `${s / 60} นาที`}</option>)}
              </select>
            </label>
            <div className="space-y-1 border-t border-app-divider pt-2 text-2xs text-app-dim">
              {RSSI_STEPS.map((s) => (
                <div key={s.min} className="flex items-center gap-2">
                  <span className="h-0.5 w-5 rounded-full" style={{ backgroundColor: s.color }} />{s.label}
                </div>
              ))}
              {showRings && (
                <>
                  <div className="flex items-center gap-2"><span className="h-3 w-3 rounded-full border border-emerald-500" />สัญญาณดี ({RING_GOOD_DBM} dBm) ≈ {ringGoodM.toFixed(0)} ม.</div>
                  <div className="flex items-center gap-2"><span className="h-3 w-3 rounded-full border border-dashed border-amber-500" />ขอบระยะ ({RING_EDGE_DBM} dBm) ≈ {ringEdgeM.toFixed(0)} ม.</div>
                </>
              )}
            </div>
            <p className="text-3xs text-app-muted">
              วงคำนวณจาก path-loss (RSSI@1ม. {model.rssiAt1m.toFixed(1)}, n {model.pathLossN.toFixed(2)} · {model.source ?? ""}) เป็นค่าโดยประมาณในที่โล่ง
              — ใต้ซากอาคารจะสั้นกว่านี้ ใช้แท็บ Range Test ปรับให้ตรงหน้างาน
            </p>
          </div>
        </div>

        <div className="panel">
          <div className="panel-header justify-between"><span>ลิงก์ (อ่อนสุดก่อน)</span><span className="font-mono text-2xs text-app-dim">{links.length}</span></div>
          <div className="max-h-[38vh] overflow-y-auto">
            {links.length === 0 ? (
              <p className="p-3 text-2xs text-app-muted">ยังไม่มีเสาไหนรายงานว่าได้ยินกำไลที่มีตำแหน่งบนแผนที่ในช่วงนี้</p>
            ) : (
              <table className="w-full text-left text-2xs">
                <tbody className="divide-y divide-app-divider font-mono">
                  {links.map((l) => (
                    <tr key={`${l.band}-${l.pin}`}>
                      <td className="px-3 py-1.5 font-sans text-app-text">เสา {l.pin + 1} → {l.name}</td>
                      <td className="px-3 py-1.5 text-right" style={{ color: rssiColor(l.rssi) }}>{l.rssi} dBm</td>
                      <td className="px-3 py-1.5 text-right text-app-dim" title="ระยะที่ประมาณจาก RSSI">≈{l.estM.toFixed(0)} ม.</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
