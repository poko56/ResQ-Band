"use client";

import Link from "next/link";
import dynamic from "next/dynamic";
import { useResQ } from "@/lib/store";

const LiveMap = dynamic(() => import("./LiveMap"), {
  ssr: false,
  loading: () => (
    <div className="grid h-full place-items-center text-sm text-app-muted">loading map…</div>
  ),
});

const MAX_PINS = 4;

export function MapPanel() {
  const placementMode    = useResQ((s) => s.placementMode);
  const setPlacementMode = useResQ((s) => s.setPlacementMode);
  const anchors          = useResQ((s) => s.anchors);

  const slots = Array.from({ length: MAX_PINS }, (_, i) => {
    return Object.values(anchors).find((a) => a.pinIndex === i);
  });
  const placedCount = slots.filter(Boolean).length;
  const onlineCount = slots.filter((a) => a?.online).length;

  return (
    <div className="relative h-full w-full">
      <LiveMap />

      {/* Floating control panel - Premiere overlay */}
      <div className="absolute right-3 top-3 z-[1000] w-[240px] panel shadow-panel">
        <div className="panel-header justify-between">
          <span>Anchors</span>
          <span className="font-mono text-app-dim normal-case">
            <span className={onlineCount === placedCount && placedCount > 0 ? "text-status-ok" : "text-status-warn"}>{onlineCount}</span>
            <span className="text-app-muted">/{placedCount}/{MAX_PINS}</span>
          </span>
        </div>
        <div className="p-2">
          <button
            onClick={() => setPlacementMode(placementMode === "anchor" ? "none" : "anchor")}
            className={`w-full h-7 text-xs font-semibold uppercase tracking-wider rounded-sm ${
              placementMode === "anchor"
                ? "bg-status-warn text-app-bg"
                : "bg-accent-pressed text-white hover:bg-accent"
            }`}
          >
            {placementMode === "anchor" ? "✕  Cancel placement" : "+  Place anchor"}
          </button>

          <div className="mt-2 grid grid-cols-4 gap-1">
            {slots.map((a, i) => (
              <div
                key={i}
                className={`grid h-7 place-items-center text-xs font-mono ${
                  !a
                    ? "border border-dashed border-app-border text-app-muted"
                    : a.online
                      ? "bg-status-ok text-app-bg font-bold"
                      : "bg-status-warn text-app-bg font-bold"
                }`}
                title={a ? `${a.name} - ${a.online ? "online" : "offline"}` : `slot ${i} empty`}
              >
                {i}
              </div>
            ))}
          </div>

          <Link
            href="/setup"
            className="mt-2 block text-center text-2xs uppercase tracking-wider text-app-dim hover:text-accent border-t border-app-border pt-2"
          >
            Manage anchors →
          </Link>
        </div>
      </div>
    </div>
  );
}
