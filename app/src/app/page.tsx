"use client";

import { useEffect } from "react";
import { TopBar } from "@/components/ui/TopBar";
import { TriageStats } from "@/components/Triage/TriageStats";
import { WristbandList } from "@/components/Roster/WristbandList";
import { MapPanel } from "@/components/Map/MapPanel";
import { HubStatusBanner } from "@/components/Hub/HubStatusBanner";
import { useHubBridge } from "@/lib/hubBridge";

export default function DashboardPage() {
  useHubBridge();

  return (
    <div className="flex h-screen flex-col">
      <TopBar />
      <HubStatusBanner />

      <div className="flex flex-1 overflow-hidden">
        <main className="relative flex-1">
          <MapPanel />
        </main>

        <aside className="flex w-[340px] flex-col border-l border-panel-border bg-panel">
          <TriageStats />
          <div className="border-y border-panel-border bg-panel-soft px-3 py-2 text-xs font-semibold uppercase tracking-wide text-slate-400">
            กำไลทั้งหมด
          </div>
          <div className="flex-1 overflow-y-auto">
            <WristbandList />
          </div>
        </aside>
      </div>
    </div>
  );
}
