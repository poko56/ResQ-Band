"use client";

import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";
import type { TriageLevel } from "@/lib/types";

const LEVELS: TriageLevel[] = ["red", "yellow", "green", "black", "nodata"];

export function TriageStats() {
  const sightings = useResQ((s) => s.sightings);

  const counts = LEVELS.reduce<Record<TriageLevel, number>>(
    (acc, lvl) => { acc[lvl] = 0; return acc; },
    { green: 0, yellow: 0, red: 0, black: 0, nodata: 0 },
  );
  for (const s of Object.values(sightings)) {
    if (s.status === "rescued") continue;
    // A band we can't hear has no current vitals, whatever it last reported.
    counts[s.status === "silent" ? "nodata" : s.triage]++;
  }
  const rescued = Object.values(sightings).filter((s) => s.status === "rescued").length;

  return (
    <div className="grid grid-cols-6 gap-px bg-app-divider border-b border-app-divider">
      {LEVELS.map((lvl) => (
        <div key={lvl} className="stat">
          {/* #2a2a2a is invisible on the dark panel, so the black count uses light ink */}
          <div className="stat-value" style={{ color: lvl === "black" ? "#bdbdbd" : TRIAGE_COLORS[lvl] }}>{counts[lvl]}</div>
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
