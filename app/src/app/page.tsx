"use client";

import { TriageStats } from "@/components/Triage/TriageStats";
import { WristbandList } from "@/components/Roster/WristbandList";
import { MapPanel } from "@/components/Map/MapPanel";
import dynamic from "next/dynamic";

export default function DashboardPage() {
  return (
    <div className="flex flex-1 overflow-hidden">
      <main className="relative flex-1 border-r border-app-divider">
          <MapPanel />
        </main>

        <aside className="flex w-[340px] flex-col bg-app-panel">
          <div className="panel-header">Triage summary</div>
          <TriageStats />
          <div className="panel-header">Roster</div>
          <div className="flex-1 overflow-y-auto">
            <WristbandList />
          </div>
        </aside>
    </div>
  );
}
