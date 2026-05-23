"use client";

import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";
import type { TriageLevel } from "@/lib/types";

const LEVELS: TriageLevel[] = ["red", "yellow", "green", "black"];

export function TriageStats() {
  const sightings = useResQ((s) => s.sightings);

  const counts = LEVELS.reduce<Record<TriageLevel, number>>(
    (acc, level) => {
      acc[level] = 0;
      return acc;
    },
    { green: 0, yellow: 0, red: 0, black: 0 },
  );

  for (const s of Object.values(sightings)) {
    if (s.status === "rescued") continue;
    counts[s.triage]++;
  }

  const rescued = Object.values(sightings).filter((s) => s.status === "rescued").length;

  return (
    <div className="grid grid-cols-5 gap-2 p-3">
      {LEVELS.map((level) => (
        <div
          key={level}
          className="rounded-md border border-panel-border bg-panel-soft p-2 text-center"
          style={{ borderTopColor: TRIAGE_COLORS[level], borderTopWidth: 3 }}
        >
          <div className="font-mono text-2xl font-bold" style={{ color: TRIAGE_COLORS[level] }}>
            {counts[level]}
          </div>
          <div className="text-[10px] uppercase tracking-wide text-slate-400">{TRIAGE_LABELS_TH[level]}</div>
        </div>
      ))}
      <div className="rounded-md border border-panel-border bg-panel-soft p-2 text-center" style={{ borderTopColor: "#0ea5e9", borderTopWidth: 3 }}>
        <div className="font-mono text-2xl font-bold text-sky-400">{rescued}</div>
        <div className="text-[10px] uppercase tracking-wide text-slate-400">กู้ออกแล้ว</div>
      </div>
    </div>
  );
}
