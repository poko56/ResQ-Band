"use client";

import { useEffect, useMemo, useState } from "react";
import dynamic from "next/dynamic";
import { TopBar } from "@/components/ui/TopBar";
import { HubStatusBanner } from "@/components/Hub/HubStatusBanner";
import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";
import type { Sighting, Wristband } from "@/lib/types";

const LiveMap = dynamic(() => import("@/components/Map/LiveMap"), {
  ssr: false,
  loading: () => <div className="grid h-full place-items-center text-xs text-app-muted">loading map…</div>,
});

const REASON_LABEL: Record<string, string> = {
  vitals_critical: "vitals critical",
  fall_detected:   "fall detected",
  tap_sos:         "3-tap SOS",
  silent_too_long: "silent too long",
  manual_override: "operator boost",
  battery_low:     "battery low",
};

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

function reasonString(s: Sighting): string {
  const parts: string[] = [];
  if (s.lastReason && REASON_LABEL[s.lastReason]) parts.push(REASON_LABEL[s.lastReason]);
  if (s.heartRate > 0 && (s.heartRate < 40 || s.heartRate > 150)) parts.push(`HR ${s.heartRate}`);
  if (s.spo2 > 0 && s.spo2 < 85) parts.push(`SpO₂ ${s.spo2}%`);
  if (Math.abs(s.lastGForce) > 4) parts.push(`g ${s.lastGForce.toFixed(1)}`);
  const silentSec = Math.floor((Date.now() - s.lastSeen) / 1000);
  if (silentSec > 60) parts.push(`silent ${Math.floor(silentSec / 60)}m`);
  if (s.batteryPct > 0 && s.batteryPct < 10) parts.push(`bat ${s.batteryPct}%`);
  if (s.manualBoost) parts.push(`boost +${s.manualBoost}`);
  return parts.join(" · ");
}

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m`;
  return `${Math.floor(s / 3600)}h`;
}

export default function SearchPage() {
  const sightings     = useResQ((s) => s.sightings);
  const wristbands    = useResQ((s) => s.wristbands);
  const searchMode    = useResQ((s) => s.searchMode);
  const ringWristband = useResQ((s) => s.ringWristband);
  const manualBoost   = useResQ((s) => s.manualBoost);
  const markRescued   = useResQ((s) => s.markRescued);

  // tick so silent counters stay fresh
  const [, force] = useState(0);
  useEffect(() => {
    const t = setInterval(() => force((n) => n + 1), 2000);
    return () => clearInterval(t);
  }, []);

  const queue = useMemo(() => {
    const now = Date.now();
    return Object.values(sightings)
      .map((s) => ({ s, wb: wristbands[s.wristbandId], score: computeScore(s, now), reason: reasonString(s) }))
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
    <div className="flex h-screen flex-col bg-app-bg">
      <TopBar />
      <HubStatusBanner />

      {/* Mode strip */}
      <div className="flex items-center h-7 bg-app-surface border-b border-app-divider px-3 text-xs">
        <span className={`inline-block h-1.5 w-1.5 rounded-full mr-2 ${searchMode ? "bg-status-warn animate-pulse" : "bg-app-muted"}`} />
        <span className={`text-2xs uppercase tracking-wider font-semibold ${searchMode ? "text-status-warn" : "text-app-dim"}`}>
          {searchMode ? "Search mode" : "Standby"}
        </span>
        <div className="ml-6 flex items-center gap-4 font-mono text-2xs">
          <span className="text-app-muted">pending<span className="ml-1 text-triage-red">{counts.pending}</span></span>
          <span className="text-app-muted">assigned<span className="ml-1 text-status-warn">{counts.assigned}</span></span>
          <span className="text-app-muted">rescued<span className="ml-1 text-status-info">{counts.rescued}</span></span>
          {counts.sos > 0 && <span className="pill bg-triage-red">SOS · {counts.sos}</span>}
        </div>
        <button
          onClick={() => useResQ.setState({ searchMode: !searchMode })}
          className={`ml-auto px-3 h-full text-2xs uppercase tracking-wider font-semibold ${
            searchMode ? "bg-app-raised hover:bg-app-active text-app-text" : "bg-status-warn hover:brightness-110 text-app-bg"
          }`}
        >
          {searchMode ? "Exit search mode" : "Enter search mode"}
        </button>
      </div>

      {/* Body */}
      <div className="grid flex-1 grid-cols-[400px_1fr] overflow-hidden divide-x divide-app-divider">
        {/* Queue */}
        <aside className="flex flex-col bg-app-panel min-h-0">
          <div className="panel-header justify-between">
            <span>Priority queue</span>
            <span className="font-mono text-app-dim normal-case">{queue.length}</span>
          </div>
          {queue.length === 0 ? (
            <div className="grid flex-1 place-items-center text-xs text-app-muted text-center p-6">
              queue empty<br/>
              <span className="text-2xs">SOS / fall / critical vitals will appear here</span>
            </div>
          ) : (
            <ol className="flex-1 overflow-y-auto divide-y divide-app-divider">
              {queue.map((row, idx) => (
                <QueueRow
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

        {/* Map */}
        <main className="relative">
          <LiveMap />
        </main>
      </div>
    </div>
  );
}

function QueueRow(props: {
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
    <li className={`row-hover p-2 ${isSOS ? "bg-[#3a2020]" : ""}`}>
      <div className="flex gap-2">
        <div className="flex flex-col items-center gap-0.5 shrink-0 w-10">
          <span className="font-mono text-2xs text-app-muted">#{rank}</span>
          {wb?.photoDataUrl ? (
            /* eslint-disable-next-line @next/next/no-img-element */
            <img src={wb.photoDataUrl} alt={name}
                 className={`h-10 w-10 object-cover ${isSOS ? "ring-2 ring-triage-red animate-pulse" : "ring-1 ring-app-border"}`} />
          ) : (
            <div
              className={`grid h-10 w-10 place-items-center font-mono text-xs font-bold text-white ${isSOS ? "ring-2 ring-triage-red animate-pulse" : "ring-1 ring-app-border"}`}
              style={{ background: triageColor }}
            >
              {name.slice(0, 2).toUpperCase()}
            </div>
          )}
        </div>

        <div className="min-w-0 flex-1">
          <div className="flex items-center gap-2">
            <span className="truncate text-xs font-semibold text-app-text">{name}</span>
            <span className="font-mono text-2xs text-app-muted">{s.wristbandId}</span>
            <span className="ml-auto pill text-app-bg" style={{ background: triageColor }}>
              {TRIAGE_LABELS_TH[s.triage]}
            </span>
          </div>

          <div className="mt-1 flex flex-wrap gap-x-3 font-mono text-2xs text-app-dim">
            <span>HR <span className="text-app-text">{s.heartRate || "—"}</span></span>
            <span>SpO₂ <span className="text-app-text">{s.spo2 || "—"}</span></span>
            <span>BAT <span className="text-app-text">{s.batteryPct}%</span></span>
            <span className="ml-auto text-app-muted">{timeAgo(s.lastSeen)} ago</span>
          </div>

          <div className="mt-1 flex items-center gap-2 text-2xs">
            <span className="font-mono px-1.5 bg-app-input text-status-ok">score {score}</span>
            {isAssigned && <span className="pill bg-status-warn text-app-bg">assigned</span>}
          </div>

          {reason && (
            <div className="mt-1 text-2xs text-app-dim">
              <span className="text-app-muted">reason · </span>
              <span className="text-app-text">{reason}</span>
            </div>
          )}

          {wb?.medical && (wb.medical.bloodType || wb.medical.allergies?.length || wb.medical.conditions?.length) && (
            <div className="mt-1 flex flex-wrap gap-1 text-2xs">
              {wb.medical.bloodType && (
                <span className="font-mono px-1 bg-triage-red/30 text-triage-red">{wb.medical.bloodType}</span>
              )}
              {wb.medical.allergies?.map((a) => (
                <span key={`a-${a}`} className="px-1 bg-status-warn/20 text-status-warn">allergy · {a}</span>
              ))}
              {wb.medical.conditions?.map((c) => (
                <span key={`c-${c}`} className="px-1 bg-triage-red/20 text-triage-red">{c}</span>
              ))}
            </div>
          )}

          <div className="mt-2 flex gap-1">
            <button onClick={props.onRing} className="btn btn-sm flex-1" title="Sound buzzer on the Band">
              Ring
            </button>
            <button onClick={props.onBoost} className="btn btn-sm flex-1" title="Push +500 priority">
              Boost
            </button>
            <button onClick={props.onRescued} className="btn btn-sm btn-accent flex-1" title="Confirm extraction">
              Rescued
            </button>
          </div>
        </div>
      </div>
    </li>
  );
}
