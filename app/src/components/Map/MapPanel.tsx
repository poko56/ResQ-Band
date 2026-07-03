"use client";

import { useEffect, useState } from "react";
import Link from "next/link";
import dynamic from "next/dynamic";
import { useResQ } from "@/lib/store";

const LiveMap = dynamic(() => import("./LiveMap"), {
  ssr: false,
  loading: () => (
    <div className="grid h-full place-items-center text-sm text-app-muted">loading map…</div>
  ),
});

function lastSeenLabel(ts?: number): string {
  if (!ts) return "never";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  return `${Math.floor(s / 3600)}h ago`;
}

export function MapPanel() {
  const placementMode    = useResQ((s) => s.placementMode);
  const setPlacementMode = useResQ((s) => s.setPlacementMode);
  const anchors          = useResQ((s) => s.anchors);

  const [, force] = useState(0);
  useEffect(() => {
    const t = setInterval(() => force((n) => n + 1), 1000);
    return () => clearInterval(t);
  }, []);

  const anchorList = Object.values(anchors);
  const placedCount = anchorList.filter(a => a.position).length;
  const onlineCount = anchorList.filter(a => a.online).length;

  return (
    <div className="relative h-full w-full">
      <LiveMap />

      <div className="absolute right-3 top-3 z-[1000] w-[260px] panel shadow-panel">
        <div className="panel-header justify-between">
          <span>Detected Anchors</span>
          <span className="font-mono text-app-dim normal-case">
            <span className={onlineCount > 0 ? "text-status-ok" : "text-status-warn"}>{onlineCount}</span>
            <span className="text-app-muted">/{anchorList.length}</span>
          </span>
        </div>
        <div className="p-2 space-y-2 max-h-[60vh] overflow-y-auto">
          {anchorList.length === 0 && (
            <div className="text-center text-xs text-app-muted py-4">
              ยังไม่พบเสาสัญญาณในระบบ
            </div>
          )}
          {anchorList.map((a) => {
            const isPlacing = placementMode === a.id;
            return (
              <div key={a.id} className="border border-app-border rounded-sm p-2 text-xs flex justify-between items-center bg-black/20">
                <div>
                  <div className="font-bold flex items-center gap-1">
                    <span className={`w-2 h-2 rounded-full ${a.online ? "bg-status-ok" : "bg-status-warn"}`} />
                    {a.name}
                  </div>
                  <div className="text-app-dim font-mono text-[10px]">{a.id}</div>
                  <div className="text-[10px] text-app-muted mt-0.5">
                    {a.position ? `Placed · ${lastSeenLabel(a.lastSeen)}` : "No position"}
                  </div>
                </div>
                <div className="flex flex-col gap-1 items-end">
                  <button
                    onClick={() => {
                      import("@/lib/hubBridge").then(mod => {
                        mod.sendCommand({ c: "identify_pin", pin_id: a.id });
                      });
                    }}
                    disabled={!a.online}
                    className="px-2 py-0.5 rounded-sm font-semibold uppercase tracking-wider text-[9px] bg-status-info text-app-bg hover:opacity-90 disabled:opacity-40"
                  >
                    ระบุตัว
                  </button>
                  <button
                    onClick={() => setPlacementMode(isPlacing ? "none" : a.id)}
                    className={`px-2 py-0.5 rounded-sm font-semibold uppercase tracking-wider text-[9px] ${
                      isPlacing
                        ? "bg-status-warn text-app-bg"
                        : a.position
                          ? "bg-accent-pressed text-white hover:bg-accent"
                          : "bg-status-ok text-app-bg hover:opacity-90"
                    }`}
                  >
                    {isPlacing ? "Cancel" : a.position ? "Move" : "Place"}
                  </button>
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
