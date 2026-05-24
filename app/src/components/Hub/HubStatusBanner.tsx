"use client";

import { useEffect, useState } from "react";
import { useResQ } from "@/lib/store";
import { connectHub, disconnectHub, isWebSerialSupported } from "@/lib/hubBridge";

const BOOT_WAIT_MS = 20_000;

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  return `${Math.floor(s / 3600)}h ago`;
}

const DOT: Record<string, string> = {
  disconnected: "bg-app-muted",
  connecting:   "bg-status-warn animate-pulse",
  connected:    "bg-status-ok",
  error:        "bg-status-err",
};
const LABEL_TH: Record<string, string> = {
  disconnected: "OFFLINE",
  connecting:   "CONNECTING",
  connected:    "ONLINE",
  error:        "ERROR",
};

export function HubStatusBanner() {
  const hub         = useResQ((s) => s.hub);
  const setDemoMode = useResQ((s) => s.setDemoMode);
  const supported   = isWebSerialSupported();

  const [, force] = useState(0);
  useEffect(() => {
    if (hub.state !== "connecting") return;
    const t = setInterval(() => force((n) => n + 1), 500);
    return () => clearInterval(t);
  }, [hub.state]);

  const bootElapsed = hub.connectStartedAt ? Date.now() - hub.connectStartedAt : 0;
  const bootRemaining = Math.max(0, Math.ceil((BOOT_WAIT_MS - bootElapsed) / 1000));

  if (hub.demoMode) {
    return (
      <div className="flex items-center h-6 bg-[#5d3a8a] text-white px-3 text-2xs tracking-wider uppercase border-b border-app-divider">
        <span className="flex items-center gap-2">
          <span className="inline-block h-1.5 w-1.5 rounded-full bg-white animate-pulse" />
          DEMO MODE — simulated stream, no hardware required
        </span>
        <button onClick={() => setDemoMode(false)} className="ml-auto btn btn-sm">
          Exit demo
        </button>
      </div>
    );
  }

  return (
    <div className="flex items-stretch h-6 bg-app-surface border-b border-app-divider text-xs">
      {/* Left: state */}
      <div className="flex items-center gap-2 px-3 border-r border-app-divider min-w-[180px]">
        <span className={`inline-block h-1.5 w-1.5 rounded-full ${DOT[hub.state]}`} />
        <span className="text-2xs font-semibold tracking-wider uppercase text-app-text">{LABEL_TH[hub.state]}</span>
      </div>

      {/* Middle: details */}
      <div className="flex items-center gap-4 px-3 text-app-dim flex-1 min-w-0 overflow-hidden">
        {hub.state === "connecting" && (
          <span className="truncate">
            <span className="text-status-warn">waiting boot</span>
            <span className="font-mono ml-2">{Math.ceil(bootElapsed / 1000)}s / {BOOT_WAIT_MS / 1000}s</span>
            {hub.connectAttempts ? <span className="ml-2 text-app-muted">ping × {hub.connectAttempts}</span> : null}
            {bootRemaining > 0 && bootRemaining <= 5 && (
              <span className="ml-2 text-triage-yellow">timeout in {bootRemaining}s</span>
            )}
          </span>
        )}

        {hub.state === "connected" && (
          <>
            {hub.mainId && <span className="font-mono text-app-text">{hub.mainId}</span>}
            {hub.fw     && <span className="text-app-muted">fw {hub.fw}</span>}
            {hub.cycleId !== undefined && (
              <span>cycle <span className="font-mono text-status-ok">#{hub.cycleId}</span></span>
            )}
            <span>rx <span className="font-mono text-app-text">{hub.loraPacketsReceived}</span></span>
            <span className="text-app-muted">last {timeAgo(hub.lastHeartbeat)}</span>
            {hub.loraReady === false && (
              <span className="pill bg-triage-yellow text-app-bg" title={hub.loraLastError ?? "SX1278 not detected on SPI"}>
                LoRa offline
              </span>
            )}
            {hub.emergencyMode && (
              <span className="pill bg-triage-red">Emergency</span>
            )}
          </>
        )}

        {hub.state === "error" && hub.lastError && (
          <span className="text-status-err truncate">{hub.lastError}</span>
        )}

        {!supported && hub.state !== "connected" && (
          <span className="text-status-warn">WebSerial requires Chrome / Edge</span>
        )}
      </div>

      {/* Right: controls */}
      <div className="flex items-stretch border-l border-app-divider">
        {hub.state === "connected" ? (
          <button onClick={() => void disconnectHub()} className="px-3 hover:bg-app-raised text-2xs tracking-wider uppercase text-app-dim hover:text-app-text">
            Disconnect
          </button>
        ) : (
          <>
            {hub.state === "connecting" && (
              <button onClick={() => void disconnectHub()} className="px-3 hover:bg-app-raised text-2xs tracking-wider uppercase text-app-dim hover:text-app-text">
                Cancel
              </button>
            )}
            <button
              onClick={() => void connectHub()}
              disabled={!supported || hub.state === "connecting"}
              className="px-3 bg-accent-pressed hover:bg-accent disabled:opacity-40 text-2xs tracking-wider uppercase text-white font-semibold"
            >
              Connect USB
            </button>
            <button onClick={() => setDemoMode(true)} className="px-3 hover:bg-app-raised text-2xs tracking-wider uppercase text-app-dim hover:text-app-text border-l border-app-divider">
              Demo
            </button>
          </>
        )}
      </div>
    </div>
  );
}
