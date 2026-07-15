"use client";

import { TopBar } from "@/components/ui/TopBar";
import { TriageStats } from "@/components/Triage/TriageStats";
import { WristbandList } from "@/components/Roster/WristbandList";
import { MapPanel } from "@/components/Map/MapPanel";
import dynamic from "next/dynamic";

const HubBridgeClient = dynamic(() => import("@/components/Hub/HubBridgeClient"), {
  ssr: false,
});

const HubStatusBanner = dynamic(() => import("@/components/Hub/HubStatusBanner").then(mod => mod.HubStatusBanner), {
  ssr: false,
});

export default function DashboardPage() {

  return (
    <div className="flex h-screen flex-col bg-app-bg">
      <HubBridgeClient />
      <TopBar />
      <HubStatusBanner />

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
    </div>
  );
}
