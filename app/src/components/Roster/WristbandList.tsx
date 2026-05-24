"use client";

import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const sec = Math.floor((Date.now() - ts) / 1000);
  if (sec < 60)   return `${sec}s`;
  if (sec < 3600) return `${Math.floor(sec / 60)}m`;
  return `${Math.floor(sec / 3600)}h`;
}

export function WristbandList() {
  const wristbands = useResQ((s) => s.wristbands);
  const sightings  = useResQ((s) => s.sightings);
  const markRescued = useResQ((s) => s.markRescued);

  const list = Object.values(wristbands).sort((a, b) => {
    const sa = sightings[a.id];
    const sb = sightings[b.id];
    const rank = { red: 0, yellow: 1, green: 2, black: 3 } as const;
    const ra = sa ? rank[sa.triage] : 99;
    const rb = sb ? rank[sb.triage] : 99;
    if (ra !== rb) return ra - rb;
    return a.name.localeCompare(b.name);
  });

  if (list.length === 0) {
    return (
      <div className="grid place-items-center p-6 text-center text-xs text-app-muted">
        no wristbands registered yet
        <br />
        <span className="text-2xs">go to <span className="text-accent">Roster</span> to enroll</span>
      </div>
    );
  }

  return (
    <ul className="divide-y divide-app-divider">
      {list.map((wb) => {
        const s = sightings[wb.id];
        const triage = s?.triage ?? "green";
        const isSOS = s?.status === "sos";
        const isRescued = s?.status === "rescued";

        return (
          <li key={wb.id} className={`row-hover flex items-center gap-2 px-2 py-1.5 ${isRescued ? "opacity-40" : ""}`}>
            <span
              className={`resq-pin shrink-0 ${isSOS ? "pulse-sos" : ""}`}
              style={{ background: TRIAGE_COLORS[triage] }}
              title={TRIAGE_LABELS_TH[triage]}
            >
              {wb.name.slice(0, 2)}
            </span>

            <div className="min-w-0 flex-1">
              <div className="flex items-center gap-2">
                <span className="truncate text-xs font-medium text-app-text">{wb.name}</span>
                <span className="font-mono text-2xs text-app-muted">{wb.id}</span>
              </div>
              <div className="flex items-center gap-3 text-2xs font-mono text-app-dim">
                {s ? (
                  <>
                    <span>HR <span className="text-app-text">{s.heartRate || "—"}</span></span>
                    <span>SpO₂ <span className="text-app-text">{s.spo2 || "—"}</span></span>
                    <span>BAT <span className="text-app-text">{s.batteryPct}%</span></span>
                    <span className="ml-auto text-app-muted">{timeAgo(s.lastSeen)}</span>
                  </>
                ) : (
                  <span className="italic text-app-muted">no signal yet</span>
                )}
              </div>
            </div>

            {!isRescued && s && (
              <button onClick={() => markRescued(wb.id)} className="btn btn-sm btn-accent">
                Rescued
              </button>
            )}
          </li>
        );
      })}
    </ul>
  );
}
