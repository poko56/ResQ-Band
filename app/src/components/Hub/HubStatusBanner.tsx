"use client";

import { useEffect, useState } from "react";
import { useResQ } from "@/lib/store";
import { connectHub, disconnectHub, isWebSerialSupported } from "@/lib/hubBridge";

const BOOT_WAIT_MS = 20_000;

const STATE_LABEL: Record<string, { th: string; color: string; bg: string }> = {
  disconnected: { th: "ยังไม่ได้ต่อ MainNode (USB)",  color: "text-red-300",     bg: "bg-red-950 border-red-900" },
  connecting:   { th: "กำลังเปิด USB Serial...",      color: "text-amber-300",   bg: "bg-amber-950 border-amber-900" },
  connected:    { th: "ต่อ MainNode แล้ว",           color: "text-emerald-300", bg: "bg-emerald-950 border-emerald-900" },
  error:        { th: "MainNode มีปัญหา",            color: "text-red-300",     bg: "bg-red-950 border-red-900" },
};

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s}s ที่แล้ว`;
  if (s < 3600) return `${Math.floor(s / 60)}m ที่แล้ว`;
  return `${Math.floor(s / 3600)}h ที่แล้ว`;
}

export function HubStatusBanner() {
  const hub = useResQ((s) => s.hub);
  const setDemoMode = useResQ((s) => s.setDemoMode);
  const supported = isWebSerialSupported();

  // Tick once a second so the boot-wait countdown stays current.
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
      <div className="flex items-center justify-between border-b border-fuchsia-900 bg-fuchsia-950 px-4 py-1.5 text-xs">
        <span className="text-fuchsia-300">
          🧪 <strong>โหมดทดสอบ (Demo)</strong> — กำลังจำลองข้อมูลโดยไม่ต้องใช้ฮาร์ดแวร์จริง
        </span>
        <button
          onClick={() => setDemoMode(false)}
          className="rounded bg-fuchsia-900 px-3 py-1 font-semibold uppercase text-white hover:bg-fuchsia-800"
        >
          ปิด Demo
        </button>
      </div>
    );
  }

  const meta = STATE_LABEL[hub.state] ?? STATE_LABEL.disconnected!;

  return (
    <div className={`border-b ${meta.bg} px-4 py-1.5`}>
      <div className="flex items-center justify-between gap-3 text-xs">
        <div className="flex items-center gap-3">
          <span className={`flex items-center gap-2 ${meta.color}`}>
            <span
              className={`inline-block h-2 w-2 rounded-full ${
                hub.state === "connected" ? "bg-emerald-400" :
                hub.state === "connecting" ? "bg-amber-400 animate-pulse" : "bg-red-400"
              }`}
            />
            <strong>{meta.th}</strong>
            {hub.state === "connecting" && (
              <span className="text-amber-200/80">
                — รอ MainNode boot ({Math.ceil(bootElapsed / 1000)}s / {BOOT_WAIT_MS / 1000}s)
                {hub.connectAttempts ? <span className="ml-1 text-slate-400">· ping × {hub.connectAttempts}</span> : null}
                {bootRemaining <= 5 && bootRemaining > 0 && (
                  <span className="ml-2 text-orange-300">⏱ จะหมดเวลาใน {bootRemaining}s</span>
                )}
              </span>
            )}
          </span>
          {hub.state === "connected" && (
            <span className="text-slate-400">
              {hub.mainId  && <span className="font-mono">{hub.mainId}</span>}
              {hub.fw      && <span className="ml-2 text-slate-500">fw {hub.fw}</span>}
              {hub.cycleId !== undefined && <span className="ml-2">cycle <span className="font-mono text-emerald-300">#{hub.cycleId}</span></span>}
              <span className="ml-2">• packets: <span className="font-mono text-emerald-300">{hub.loraPacketsReceived}</span></span>
              <span className="ml-2">• ล่าสุด: {timeAgo(hub.lastHeartbeat)}</span>
              {hub.emergencyMode && <span className="ml-2 rounded bg-red-700 px-1.5 font-bold text-white">EMERGENCY</span>}
              {hub.loraReady === false && (
                <span className="ml-2 rounded bg-orange-700 px-1.5 font-bold text-white" title={hub.loraLastError ?? "ตรวจไม่พบ SX1278 บน SPI"}>
                  LoRa OFFLINE
                </span>
              )}
            </span>
          )}
          {hub.lastError && hub.state !== "connected" && (
            <span className="text-red-400">— {hub.lastError}</span>
          )}
          {!supported && (
            <span className="text-amber-400">— ต้องใช้ Chrome หรือ Edge สำหรับ WebSerial</span>
          )}
        </div>

        <div className="flex items-center gap-2">
          {hub.state !== "connected" ? (
            <>
              {hub.state === "connecting" ? (
                <button
                  onClick={() => void disconnectHub()}
                  className="rounded bg-slate-700 px-3 py-0.5 font-semibold text-slate-100 hover:bg-slate-600"
                >
                  ยกเลิก
                </button>
              ) : null}
              <button
                onClick={() => void connectHub()}
                disabled={!supported || hub.state === "connecting"}
                className="rounded bg-emerald-700 px-3 py-0.5 font-semibold text-white hover:bg-emerald-600 disabled:opacity-40"
              >
                เชื่อมต่อ USB
              </button>
              <button
                onClick={() => setDemoMode(true)}
                className="rounded bg-fuchsia-700 px-2 py-0.5 font-semibold text-white hover:bg-fuchsia-600"
                title="ใช้ข้อมูลจำลองสำหรับทดสอบ UI โดยไม่ต้องใช้ฮาร์ดแวร์"
              >
                เปิด Demo
              </button>
            </>
          ) : (
            <button
              onClick={() => void disconnectHub()}
              className="rounded bg-slate-700 px-3 py-0.5 font-semibold text-slate-100 hover:bg-slate-600"
            >
              ตัดการเชื่อมต่อ
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
