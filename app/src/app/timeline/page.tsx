"use client";

import { TopBar } from "@/components/ui/TopBar";
import { useResQ } from "@/lib/store";

const TYPE_LABELS: Record<string, { th: string; color: string }> = {
  anchor_placed: { th: "วางเสา", color: "bg-orange-500" },
  wristband_registered: { th: "ลงทะเบียน", color: "bg-sky-500" },
  sos_received: { th: "SOS", color: "bg-triage-red" },
  lost_contact: { th: "ขาดการติดต่อ", color: "bg-amber-500" },
  vitals_critical: { th: "ชีพจรวิกฤต", color: "bg-triage-red" },
  found_by_handheld: { th: "เจอตัว", color: "bg-emerald-500" },
  marked_rescued: { th: "กู้สำเร็จ", color: "bg-sky-500" },
};

export default function TimelinePage() {
  const timeline = useResQ((s) => s.timeline);

  return (
    <div className="flex h-screen flex-col">
      <TopBar />
      <main className="flex-1 overflow-y-auto p-5">
        <h2 className="mb-4 text-lg font-bold">Timeline เหตุการณ์ทั้งหมด ({timeline.length})</h2>

        {timeline.length === 0 ? (
          <div className="rounded border border-dashed border-panel-border p-6 text-center text-sm text-slate-500">
            ยังไม่มีเหตุการณ์
          </div>
        ) : (
          <ol className="space-y-2">
            {timeline.map((ev) => {
              const meta = TYPE_LABELS[ev.type] ?? { th: ev.type, color: "bg-slate-500" };
              return (
                <li key={ev.id} className="flex items-start gap-3 rounded border border-panel-border bg-panel-soft p-3">
                  <span className={`shrink-0 rounded px-2 py-0.5 text-[10px] font-bold uppercase text-white ${meta.color}`}>
                    {meta.th}
                  </span>
                  <div className="min-w-0 flex-1">
                    <div className="text-sm">{ev.message}</div>
                    <div className="text-xs text-slate-500">
                      {new Date(ev.ts).toLocaleString("th-TH")}
                      {ev.wristbandId && <span className="ml-2 font-mono">· wb={ev.wristbandId}</span>}
                      {ev.anchorId && <span className="ml-2 font-mono">· anchor={ev.anchorId}</span>}
                    </div>
                  </div>
                </li>
              );
            })}
          </ol>
        )}
      </main>
    </div>
  );
}
