"use client";

import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";
import type { TriageLevel } from "@/lib/types";

const LEVELS: TriageLevel[] = ["red", "yellow", "green", "black"];

export function TriageStats() {
  const sightings = useResQ((s) => s.sightings);

  const counts = LEVELS.reduce<Record<TriageLevel, number>>(
    (acc, lvl) => { acc[lvl] = 0; return acc; },
    { green: 0, yellow: 0, red: 0, black: 0 },
  );
  for (const s of Object.values(sightings)) {
    if (s.status === "rescued") continue;
    counts[s.triage]++;
  }
  const rescued = Object.values(sightings).filter((s) => s.status === "rescued").length;

  return (
    <div className="grid grid-cols-5 gap-px bg-app-divider border-b border-app-divider">
      {LEVELS.map((lvl) => (
        <div key={lvl} className="stat">
          <div className="stat-value" style={{ color: TRIAGE_COLORS[lvl] }}>{counts[lvl]}</div>
          <div className="stat-label">{TRIAGE_LABELS_TH[lvl]}</div>
        </div>
      ))}
      <div className="stat">
        <div className="stat-value text-accent">{rescued}</div>
        <div className="stat-label">กู้ออกแล้ว</div>
      </div>
    </div>
  );
}
