"use client";

import { TriageStats } from "@/components/Triage/TriageStats";
import { WristbandList } from "@/components/Roster/WristbandList";
import { MapPanel } from "@/components/Map/MapPanel";
import dynamic from "next/dynamic";
import { useState } from "react";
import { useResQ } from "@/lib/store";
import { buildRosterPayload } from "@/lib/roster";

export default function DashboardPage() {
  const wristbands = useResQ((s) => s.wristbands);
  const [syncStatus, setSyncStatus] = useState<string | null>(null);

  // Export the roster as a roster.json file to hand off to the ResQ-Node: on the
  // handheld's page (http://192.168.4.1) pick this file under "Upload roster.json".
  // This file route avoids the https->http mixed-content block the live Sync hits.
  async function exportRosterFile() {
    const payload = await buildRosterPayload(wristbands);
    const count = Object.keys(payload).length;
    const json = JSON.stringify(payload);
    const url = URL.createObjectURL(new Blob([json], { type: "application/json" }));
    const a = document.createElement("a");
    a.href = url;
    a.download = "roster.json";
    a.click();
    URL.revokeObjectURL(url);
    setSyncStatus(`Export แล้ว: ${count} คน, ${Math.round(json.length / 1024)} KB → อัปโหลดที่ 192.168.4.1`);
    setTimeout(() => setSyncStatus(null), 6000);
  }

  async function syncToLocator() {
    setSyncStatus("กำลังซิงค์...");
    try {
      // Build the exact keyed shape the handheld reads (name/medical/photo), the
      // same as the file export - sending the raw store object saved nothing.
      const payload = await buildRosterPayload(wristbands);
      const count = Object.keys(payload).length;
      const json = JSON.stringify(payload);
      // The ResQ-Node's POST /api/roster is a streamed multipart upload handler,
      // so send multipart/form-data with a "file" part (NOT a raw JSON body, and
      // let the browser set the multipart boundary - don't set Content-Type).
      const fd = new FormData();
      fd.append("file", new Blob([json], { type: "application/json" }), "roster.json");
      const res = await fetch("http://192.168.4.1/api/roster", { method: "POST", body: fd });
      if (!res.ok) throw new Error("Status " + res.status);
      const info = await res.json().catch(() => ({} as { bytes?: number }));
      setSyncStatus(`ซิงค์สำเร็จ: ${count} คน (${info.bytes ?? json.length} ไบต์)`);
      setTimeout(() => setSyncStatus(null), 4000);
    } catch (err: any) {
      console.error(err);
      setSyncStatus(`ล้มเหลว: ${err.message || 'network'} — ต่อ WiFi "ResQ-Locator" อยู่ไหม?`);
      setTimeout(() => setSyncStatus(null), 8000);
    }
  }

  return (
    <div className="flex flex-1 overflow-hidden">
      <main className="relative flex-1 border-r border-app-divider">
          <MapPanel />
        </main>

        <aside className="flex w-[340px] flex-col bg-app-panel">
          <div className="panel-header">Triage summary</div>
          <TriageStats />
          <div className="panel-header justify-between">
            <span>Roster</span>
          </div>
          <div className="p-3 shrink-0 border-b border-app-divider space-y-2">
            <button
              type="button"
              onClick={exportRosterFile}
              className="btn w-full h-8 text-xs uppercase tracking-wider font-semibold flex items-center justify-center"
            >
              Export roster.json
            </button>
            <button
              type="button"
              onClick={syncToLocator}
              className="btn outline w-full h-8 text-xs uppercase tracking-wider font-semibold flex items-center justify-center"
            >
              Sync to Locator (WiFi)
            </button>
            {syncStatus && (
              <div className={`mt-2 text-center text-xs font-semibold ${syncStatus.includes('Failed') ? 'text-status-warn' : 'text-status-info'}`}>
                {syncStatus}
              </div>
            )}
          </div>
          <div className="flex-1 overflow-y-auto">
            <WristbandList />
          </div>
        </aside>
    </div>
  );
}
