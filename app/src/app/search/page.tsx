"use client";

// ============================================================================
// /search - rescue dispatcher view.
//
// Live priority queue (sorted by triage score) on the left, map on the right.
// Each row exposes the three things a dispatcher actually does:
//   1. RING  - tell the Band's buzzer to fire so the rescuer can locate it
//   2. BOOST - manually escalate priority (operator knows context the sensors don't)
//   3. RESCUED - close out the case once the field team confirms extraction
//
// Search mode auto-activates when an SOS_TAP or SOS_FALL arrives; the operator
// can also toggle it manually.
// ============================================================================

import { useEffect, useMemo, useState } from "react";
import dynamic from "next/dynamic";
import { TopBar } from "@/components/ui/TopBar";
import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";
import type { Sighting, Wristband } from "@/lib/types";

const LiveMap = dynamic(() => import("@/components/Map/LiveMap"), {
  ssr: false,
  loading: () => <div className="grid h-full place-items-center text-sm text-slate-500">กำลังโหลดแผนที่...</div>,
});

const REASON_LABEL: Record<string, string> = {
  vitals_critical: "ชีพจรวิกฤต",
  fall_detected:   "ตรวจพบการล้ม",
  tap_sos:         "เคาะขอความช่วยเหลือ",
  silent_too_long: "ขาดสัญญาณนานเกินไป",
  manual_override: "Operator บูสต์ลำดับ",
  battery_low:     "แบตเตอรี่ต่ำ",
};

// Mirror MainNode/main.cpp triage score so the dispatcher list matches what
// the firmware decides. Slight differences in 'silent_ms' window are OK -
// the dispatcher view is always informational, the radio is the source of truth.
function computeScore(s: Sighting, nowMs: number): number {
  let score = 0;
  if (s.heartRate > 0) {
    if (s.heartRate < 40 || s.heartRate > 150) score += 100;
    else if (s.heartRate < 55 || s.heartRate > 110) score += 30;
  }
  if (s.spo2 > 0) {
    if (s.spo2 < 85) score += 80;
    else if (s.spo2 < 94) score += 20;
  }
  if (Math.abs(s.lastGForce) > 4) score += 150;
  const silentSec = Math.max(0, Math.floor((nowMs - s.lastSeen) / 1000));
  score += Math.min(200, Math.floor(silentSec / 60) * 20);
  if (s.batteryPct > 0 && s.batteryPct < 10) score += 30;
  if (s.manualBoost) score += s.manualBoost;
  if (s.status === "assigned") score -= 500;
  if (s.status === "rescued" || s.status === "deceased") score -= 100000;
  return score;
}

function buildReasonText(s: Sighting): string {
  const reasons: string[] = [];
  if (s.lastReason && REASON_LABEL[s.lastReason]) {
    reasons.push(REASON_LABEL[s.lastReason]);
  }
  if (s.heartRate > 0 && (s.heartRate < 40 || s.heartRate > 150)) reasons.push(`HR ${s.heartRate}`);
  if (s.spo2 > 0 && s.spo2 < 85) reasons.push(`SpO₂ ${s.spo2}%`);
  if (Math.abs(s.lastGForce) > 4) reasons.push(`g ${s.lastGForce.toFixed(1)}`);
  const silentSec = Math.floor((Date.now() - s.lastSeen) / 1000);
  if (silentSec > 60) reasons.push(`เงียบ ${Math.floor(silentSec / 60)}m`);
  if (s.batteryPct > 0 && s.batteryPct < 10) reasons.push(`batt ${s.batteryPct}%`);
  if (s.manualBoost) reasons.push(`boost +${s.manualBoost}`);
  return reasons.join(" · ");
}

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m`;
  return `${Math.floor(s / 3600)}h`;
}

export default function SearchPage() {
  const sightings = useResQ((s) => s.sightings);
  const wristbands = useResQ((s) => s.wristbands);
  const searchMode = useResQ((s) => s.searchMode);
  const ringWristband = useResQ((s) => s.ringWristband);
  const manualBoost   = useResQ((s) => s.manualBoost);
  const markRescued   = useResQ((s) => s.markRescued);

  // Tick every 2 s so silent-time scoring stays fresh
  const [, force] = useState(0);
  useEffect(() => {
    const t = setInterval(() => force((n) => n + 1), 2000);
    return () => clearInterval(t);
  }, []);

  const queue = useMemo(() => {
    const now = Date.now();
    return Object.values(sightings)
      .map((s) => ({ s, wb: wristbands[s.wristbandId], score: computeScore(s, now), reason: buildReasonText(s) }))
      .filter((r) => r.s.status !== "rescued" && r.s.status !== "deceased")
      .sort((a, b) => b.score - a.score);
  }, [sightings, wristbands]);

  const counts = useMemo(() => {
    const c = { pending: 0, assigned: 0, rescued: 0, sos: 0 };
    for (const s of Object.values(sightings)) {
      if      (s.status === "rescued" || s.status === "deceased") c.rescued++;
      else if (s.status === "assigned")                            c.assigned++;
      else if (s.status === "sos")                                 { c.sos++; c.pending++; }
      else                                                          c.pending++;
    }
    return c;
  }, [sightings]);

  return (
    <div className="flex h-screen flex-col">
      <TopBar />

      {/* Mode banner */}
      <div className={`flex items-center justify-between border-b px-4 py-2 text-xs ${
        searchMode ? "border-amber-900 bg-amber-950" : "border-panel-border bg-panel-soft"
      }`}>
        <div className="flex items-center gap-3">
          <span className={`inline-block h-2 w-2 rounded-full ${searchMode ? "animate-pulse bg-amber-400" : "bg-slate-500"}`} />
          <span className={searchMode ? "font-bold text-amber-200" : "text-slate-400"}>
            {searchMode ? "🔴 SEARCH MODE — มีเหตุฉุกเฉินรอช่วย" : "ระบบเฝ้าระวัง — รอเหตุการณ์"}
          </span>
          <span className="text-slate-500">
            รอช่วย <span className="font-mono text-red-300">{counts.pending}</span>{" "}
            · กำลังช่วย <span className="font-mono text-amber-300">{counts.assigned}</span>{" "}
            · กู้แล้ว <span className="font-mono text-sky-300">{counts.rescued}</span>
            {counts.sos > 0 && <span className="ml-2 rounded bg-red-700 px-1.5 py-0.5 font-bold text-white">SOS {counts.sos}</span>}
          </span>
        </div>
        <button
          onClick={() => useResQ.setState({ searchMode: !searchMode })}
          className={`rounded px-3 py-1 font-semibold ${searchMode ? "bg-slate-700 text-slate-200 hover:bg-slate-600" : "bg-amber-700 text-white hover:bg-amber-600"}`}
        >
          {searchMode ? "ออกจาก Search Mode" : "เข้า Search Mode"}
        </button>
      </div>

      {/* Body */}
      <div className="grid flex-1 grid-cols-[420px_1fr] overflow-hidden">
        {/* ---- QUEUE ----------------------------------------------------- */}
        <aside className="overflow-y-auto border-r border-panel-border bg-panel-soft">
          {queue.length === 0 ? (
            <div className="grid h-full place-items-center p-6 text-center text-sm text-slate-500">
              <div>
                ✅ ไม่มีคิวรอช่วยตอนนี้
                <br />
                <span className="text-xs">SOS / Fall / vitals วิกฤต จะปรากฏที่นี่อัตโนมัติ</span>
              </div>
            </div>
          ) : (
            <ol>
              {queue.map((row, idx) => (
                <QueueCard
                  key={row.s.wristbandId}
                  rank={idx + 1}
                  sighting={row.s}
                  wristband={row.wb}
                  score={row.score}
                  reason={row.reason}
                  onRing={() => ringWristband(row.s.wristbandId)}
                  onBoost={() => manualBoost(row.s.wristbandId, (row.s.manualBoost ?? 0) + 500)}
                  onRescued={() => markRescued(row.s.wristbandId)}
                />
              ))}
            </ol>
          )}
        </aside>

        {/* ---- MAP ------------------------------------------------------- */}
        <main className="relative">
          <LiveMap />
        </main>
      </div>
    </div>
  );
}

// ----------------------------------------------------------------------------
// Queue card
// ----------------------------------------------------------------------------
function QueueCard(props: {
  rank: number;
  sighting: Sighting;
  wristband?: Wristband;
  score: number;
  reason: string;
  onRing: () => void;
  onBoost: () => void;
  onRescued: () => void;
}) {
  const { rank, sighting: s, wristband: wb, score, reason } = props;
  const name = wb?.name ?? s.wristbandId;
  const triageColor = TRIAGE_COLORS[s.triage];
  const isAssigned = s.status === "assigned";
  const isSOS = s.status === "sos";

  return (
    <li className={`border-b border-panel-border p-3 ${isSOS ? "bg-red-950/30" : ""}`}>
      <div className="flex gap-3">
        {/* Rank + avatar */}
        <div className="flex flex-col items-center gap-1">
          <span className="text-xs font-bold text-slate-500">#{rank}</span>
          {wb?.photoDataUrl ? (
            /* eslint-disable-next-line @next/next/no-img-element */
            <img src={wb.photoDataUrl} alt={name}
                 className={`h-12 w-12 rounded-full object-cover ring-2 ${isSOS ? "ring-red-500 animate-pulse" : "ring-slate-700"}`} />
          ) : (
            <div
              className={`grid h-12 w-12 place-items-center rounded-full font-bold text-white ring-2 ${isSOS ? "ring-red-500 animate-pulse" : "ring-slate-700"}`}
              style={{ background: triageColor }}
            >
              {name.slice(0, 2).toUpperCase()}
            </div>
          )}
        </div>

        {/* Info */}
        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            <span className="truncate text-sm font-bold">{name}</span>
            <span className="font-mono text-[10px] text-slate-500">{s.wristbandId}</span>
            <span
              className="ml-auto rounded px-1.5 py-0.5 text-[10px] font-bold uppercase text-white"
              style={{ background: triageColor }}
            >
              {TRIAGE_LABELS_TH[s.triage]}
            </span>
          </div>

          <div className="mt-1 flex flex-wrap items-center gap-x-3 gap-y-0.5 text-xs text-slate-300">
            <span>♥ {s.heartRate || "—"}</span>
            <span>O₂ {s.spo2 || "—"}%</span>
            <span>🔋 {s.batteryPct}%</span>
            <span className="text-slate-500">เห็นล่าสุด {timeAgo(s.lastSeen)} ที่แล้ว</span>
          </div>

          <div className="mt-1.5 flex items-center gap-2 text-[11px]">
            <span className="rounded bg-panel px-1.5 py-0.5 font-mono text-emerald-300">score {score}</span>
            {isAssigned && <span className="rounded bg-amber-700 px-1.5 py-0.5 font-bold text-white">ASSIGNED</span>}
          </div>

          {reason && (
            <div className="mt-1 text-[11px] text-slate-400">
              เหตุผล: <span className="text-slate-200">{reason}</span>
            </div>
          )}

          {/* Medical hint */}
          {wb?.medical && (wb.medical.allergies?.length || wb.medical.conditions?.length || wb.medical.bloodType) && (
            <div className="mt-1.5 flex flex-wrap gap-1 text-[10px]">
              {wb.medical.bloodType && <span className="rounded bg-rose-700/40 px-1 text-rose-200">เลือด {wb.medical.bloodType}</span>}
              {wb.medical.allergies?.map((a) => (
                <span key={`a-${a}`} className="rounded bg-amber-700/40 px-1 text-amber-200">แพ้ {a}</span>
              ))}
              {wb.medical.conditions?.map((c) => (
                <span key={`c-${c}`} className="rounded bg-rose-700/40 px-1 text-rose-200">{c}</span>
              ))}
            </div>
          )}

          {/* Controls */}
          <div className="mt-2 flex gap-1.5">
            <button
              onClick={props.onRing}
              className="flex-1 rounded bg-sky-700 px-2 py-1 text-[11px] font-bold uppercase text-white hover:bg-sky-600"
              title="สั่งให้กำไลร้อง Buzzer 3 วินาที"
            >
              🔔 Ring
            </button>
            <button
              onClick={props.onBoost}
              className="flex-1 rounded bg-amber-700 px-2 py-1 text-[11px] font-bold uppercase text-white hover:bg-amber-600"
              title="เพิ่มความสำคัญ +500 — ดันคนนี้ขึ้นต้นคิว"
            >
              ⬆ Boost
            </button>
            <button
              onClick={props.onRescued}
              className="flex-1 rounded bg-emerald-700 px-2 py-1 text-[11px] font-bold uppercase text-white hover:bg-emerald-600"
              title="ยืนยันว่ากู้ภัยพบและพาออกจากที่เกิดเหตุแล้ว"
            >
              ✓ Rescued
            </button>
          </div>
        </div>
      </div>
    </li>
  );
}
