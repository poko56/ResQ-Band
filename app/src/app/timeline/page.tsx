"use client";

import { TopBar } from "@/components/ui/TopBar";
import { HubStatusBanner } from "@/components/Hub/HubStatusBanner";
import { useResQ } from "@/lib/store";

const TYPE_META: Record<string, { th: string; bg: string }> = {
  hub_connected:         { th: "hub up",        bg: "bg-status-info" },
  hub_disconnected:      { th: "hub down",      bg: "bg-app-muted" },
  anchor_placed:         { th: "anchor",        bg: "bg-status-warn" },
  wristband_registered:  { th: "register",      bg: "bg-accent" },
  sos_received:          { th: "sos",           bg: "bg-triage-red" },
  fall_received:         { th: "fall",          bg: "bg-triage-red" },
  lost_contact:          { th: "lost",          bg: "bg-status-warn" },
  vitals_critical:       { th: "critical",      bg: "bg-triage-red" },
  assignment_dispatched: { th: "dispatch",      bg: "bg-accent" },
  found_by_handheld:     { th: "found",         bg: "bg-status-ok" },
  marked_rescued:        { th: "rescued",       bg: "bg-status-ok" },
  manual_priority:       { th: "boost",         bg: "bg-status-warn" },
};

export default function TimelinePage() {
  const timeline = useResQ((s) => s.timeline);

  return (
    <div className="flex h-screen flex-col bg-app-bg">
      <TopBar />
      <HubStatusBanner />

      <div className="panel-header justify-between">
        <span>Event log</span>
        <span className="font-mono text-app-dim normal-case">{timeline.length} events</span>
      </div>

      <main className="flex-1 overflow-y-auto">
        {timeline.length === 0 ? (
          <div className="grid h-full place-items-center text-xs text-app-muted">
            no events yet
          </div>
        ) : (
          <ol className="divide-y divide-app-divider">
            {timeline.map((ev) => {
              const meta = TYPE_META[ev.type] ?? { th: ev.type, bg: "bg-app-muted" };
              return (
                <li key={ev.id} className="row-hover grid grid-cols-[80px_140px_1fr_auto] items-center gap-3 px-3 py-1.5">
                  <span className={`pill ${meta.bg}`}>{meta.th}</span>
                  <span className="font-mono text-2xs text-app-muted">
                    {new Date(ev.ts).toLocaleString("th-TH", { hour: "2-digit", minute: "2-digit", second: "2-digit" })}
                  </span>
                  <span className="text-xs text-app-text truncate">{ev.message}</span>
                  <span className="font-mono text-2xs text-app-muted">
                    {ev.wristbandId && <span>wb={ev.wristbandId}</span>}
                    {ev.anchorId    && <span>· anc={ev.anchorId}</span>}
                  </span>
                </li>
              );
            })}
          </ol>
        )}
      </main>
    </div>
  );
}
