"use client";

import dynamic from "next/dynamic";
import { useState } from "react";
import { TopBar } from "@/components/ui/TopBar";
import { useResQ } from "@/lib/store";

const LiveMap = dynamic(() => import("@/components/Map/LiveMap"), {
  ssr: false,
  loading: () => <div className="grid h-full place-items-center text-xs text-app-muted">loading map…</div>,
});

const MAX_PINS = 4;

function timeAgo(ts?: number) {
  if (!ts) return "never";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  return `${Math.floor(s / 3600)}h ago`;
}

const HubBridgeClient = dynamic(() => import("@/components/Hub/HubBridgeClient"), {
  ssr: false,
});

const HubStatusBanner = dynamic(() => import("@/components/Hub/HubStatusBanner").then(mod => mod.HubStatusBanner), {
  ssr: false,
});

export default function PinSetupPage() {
  const anchors          = useResQ((s) => s.anchors);
  const unassignedAnchors = useResQ((s) => s.unassignedAnchors);
  const removeAnchor     = useResQ((s) => s.removeAnchor);
  const renameAnchor     = useResQ((s) => s.renameAnchor);
  const placementMode    = useResQ((s) => s.placementMode);
  const setPlacementMode = useResQ((s) => s.setPlacementMode);

  const [editingId, setEditingId] = useState<string | null>(null);
  const [draftName, setDraftName] = useState("");

  const slots = Array.from({ length: MAX_PINS }, (_, i) => ({
    index: i,
    anchor: Object.values(anchors).find((a) => a.pinIndex === i),
  }));

  function commitEdit() {
    if (editingId && draftName.trim()) renameAnchor(editingId, draftName.trim());
    setEditingId(null);
    setDraftName("");
  }

  const [selectedUnassigned, setSelectedUnassigned] = useState<Record<number, string>>({});

  return (
    <div className="flex h-screen flex-col bg-app-bg">
      <HubBridgeClient />
      <TopBar />
      <HubStatusBanner />

      <div className="grid flex-1 grid-cols-[360px_1fr] overflow-hidden divide-x divide-app-divider">
        {/* Left: pin admin */}
        <aside className="flex flex-col bg-app-panel">
          <div className="panel-header">Anchors · 4 TDMA slots</div>

          <div className="p-2 border-b border-app-divider">
            <p className="mt-1.5 text-2xs text-app-muted">
              Select an anchor from the list and click on map to set its position
            </p>
          </div>

          <ul className="flex-1 overflow-y-auto divide-y divide-app-divider">
            {slots.map(({ index, anchor }) => (
              <li key={index} className="row-hover px-3 py-2">
                <div className="flex items-start gap-3">
                  <div
                    className={`grid h-8 w-8 shrink-0 place-items-center font-mono text-sm font-bold ${
                      anchor
                        ? anchor.online ? "bg-status-ok text-app-bg" : "bg-status-warn text-app-bg"
                        : "bg-app-input text-app-muted border border-dashed border-app-border"
                    }`}
                  >
                    {index}
                  </div>

                  <div className="min-w-0 flex-1">
                    {!anchor ? (
                      <div className="flex flex-col gap-2">
                        <div>
                          <div className="text-xs text-app-muted">Empty slot</div>
                          <div className="text-2xs text-app-muted">Select an unassigned anchor</div>
                        </div>
                        <div className="flex items-center gap-2">
                          <select
                            className="field h-7 text-xs bg-app-input border border-app-border text-app-text"
                            value={selectedUnassigned[index] || ""}
                            onChange={(e) => setSelectedUnassigned({ ...selectedUnassigned, [index]: e.target.value })}
                          >
                            <option value="">-- select anchor --</option>
                            {Object.values(unassignedAnchors).map((ua) => (
                              <option key={ua.id} value={ua.id}>{ua.id} ({timeAgo(ua.lastSeen)})</option>
                            ))}
                          </select>
                          <button
                            className="btn btn-sm btn-accent"
                            disabled={!selectedUnassigned[index]}
                            onClick={() => {
                              import("@/lib/hubBridge").then(mod => {
                                mod.sendCommand({ c: "set_pin_slot", pin_id: selectedUnassigned[index], slot: index });
                              });
                            }}
                          >
                            Assign
                          </button>
                        </div>
                      </div>
                    ) : editingId === anchor.id ? (
                      <div className="flex items-center gap-1">
                        <input
                          autoFocus
                          value={draftName}
                          onChange={(e) => setDraftName(e.target.value)}
                          onKeyDown={(e) => {
                            if (e.key === "Enter") commitEdit();
                            if (e.key === "Escape") { setEditingId(null); setDraftName(""); }
                          }}
                          className="field h-6"
                        />
                        <button onClick={commitEdit} className="btn btn-sm btn-accent">Save</button>
                      </div>
                    ) : (
                      <>
                        <div className="flex items-center gap-2">
                          <span className="text-xs font-medium text-app-text">{anchor.name}</span>
                          <span className="font-mono text-2xs text-app-muted">{anchor.id}</span>
                          {anchor.online
                            ? <span className="pill bg-status-ok text-app-bg">online</span>
                            : <span className="pill bg-status-warn text-app-bg">offline</span>}
                        </div>
                        <div className="font-mono text-2xs text-app-dim mt-0.5">
                          {anchor.position ? `${anchor.position.lat.toFixed(5)}, ${anchor.position.lng.toFixed(5)}` : "No position"}
                        </div>
                        <div className="text-2xs text-app-muted mt-0.5">
                          last sighting · {timeAgo(anchor.lastSeen)}
                        </div>
                        <div className="mt-1.5 flex gap-1">
                          <button
                            onClick={() => { setEditingId(anchor.id); setDraftName(anchor.name); }}
                            className="btn btn-sm"
                          >
                            Rename
                          </button>
                          <button
                            onClick={() => {
                              import("@/lib/hubBridge").then(mod => {
                                mod.sendCommand({ c: "identify_pin", pin_id: anchor.id });
                              });
                            }}
                            disabled={!anchor.online}
                            className="btn btn-sm btn-info text-white"
                          >
                            ระบุตัว
                          </button>
                          <button
                            onClick={() => setPlacementMode(placementMode === anchor.id ? "none" : anchor.id)}
                            className={`btn btn-sm ${placementMode === anchor.id ? "btn-accent" : ""}`}
                          >
                            {placementMode === anchor.id ? "Cancel Place" : "Place"}
                          </button>
                          <button
                            onClick={() => {
                              import("@/lib/hubBridge").then(mod => {
                                mod.sendCommand({ c: "set_pin_slot", pin_id: anchor.id, slot: 255 }); // Unassign
                              });
                              removeAnchor(anchor.id);
                            }}
                            className="btn btn-sm hover:bg-status-err hover:text-white"
                          >
                            Remove
                          </button>
                        </div>
                      </>
                    )}
                  </div>
                </div>
              </li>
            ))}
          </ul>

          <div className="border-t border-app-divider bg-app-surface px-3 py-1.5 text-2xs text-app-muted">
            Position is used to estimate Band location from strongest pin RSSI
          </div>
        </aside>

        {/* Right: map */}
        <main className="relative">
          <LiveMap />
          {placementMode !== "none" && (
            <div className="pointer-events-none absolute left-1/2 top-3 z-[1000] -translate-x-1/2 px-3 py-1 bg-status-warn text-app-bg text-2xs font-bold uppercase tracking-wider">
              Click on map to drop anchor
            </div>
          )}
        </main>
      </div>
    </div>
  );
}
