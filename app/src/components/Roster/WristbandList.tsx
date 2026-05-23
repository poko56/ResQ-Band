"use client";

import { useResQ } from "@/lib/store";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const sec = Math.floor((Date.now() - ts) / 1000);
  if (sec < 60) return `${sec}s`;
  if (sec < 3600) return `${Math.floor(sec / 60)}m`;
  return `${Math.floor(sec / 3600)}h`;
}

export function WristbandList() {
  const wristbands = useResQ((s) => s.wristbands);
  const sightings = useResQ((s) => s.sightings);
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
      <div className="grid place-items-center p-8 text-center text-sm text-slate-500">
        <div>
          ยังไม่มีกำไลในระบบ
          <br />
          <span className="text-xs">ไปที่หน้า &quot;ตั้งค่ากำไล&quot; เพื่อลงทะเบียน</span>
        </div>
      </div>
    );
  }

  return (
    <div className="divide-y divide-panel-border overflow-y-auto">
      {list.map((wb) => {
        const s = sightings[wb.id];
        const triage = s?.triage ?? "green";
        const isSOS = s?.status === "sos";
        const isRescued = s?.status === "rescued";

        return (
          <div key={wb.id} className={`flex items-center gap-3 px-3 py-2 ${isRescued ? "opacity-50" : ""}`}>
            <div
              className={`resq-pin shrink-0 ${isSOS ? "pulse-sos" : ""}`}
              style={{ background: TRIAGE_COLORS[triage] }}
              title={TRIAGE_LABELS_TH[triage]}
            >
              {wb.name.slice(0, 2).toUpperCase()}
            </div>

            <div className="min-w-0 flex-1">
              <div className="flex items-center gap-2">
                <span className="truncate text-sm font-semibold">{wb.name}</span>
                <span className="font-mono text-[10px] text-slate-500">{wb.id}</span>
              </div>
              <div className="flex items-center gap-3 text-xs text-slate-400">
                {s ? (
                  <>
                    <span>♥ {s.heartRate || "—"} BPM</span>
                    <span>O₂ {s.spo2 || "—"}%</span>
                    <span>🔋 {s.batteryPct}%</span>
                    <span className="ml-auto text-slate-500">{timeAgo(s.lastSeen)}</span>
                  </>
                ) : (
                  <span className="italic text-slate-600">ยังไม่ได้รับสัญญาณ</span>
                )}
              </div>
            </div>

            {!isRescued && s && (
              <button
                onClick={() => markRescued(wb.id)}
                className="rounded bg-sky-600 px-2 py-1 text-[10px] font-semibold uppercase text-white hover:bg-sky-500"
              >
                กู้แล้ว
              </button>
            )}
          </div>
        );
      })}
    </div>
  );
}
