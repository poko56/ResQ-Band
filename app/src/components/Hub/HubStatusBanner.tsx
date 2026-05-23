"use client";

import { useState } from "react";
import { useResQ } from "@/lib/store";

const STATE_LABEL: Record<string, { th: string; color: string; bg: string }> = {
  disconnected: { th: "ไม่ได้เชื่อมต่อ Main Hub", color: "text-red-300", bg: "bg-red-950 border-red-900" },
  connecting:   { th: "กำลังเชื่อมต่อ Main Hub...", color: "text-amber-300", bg: "bg-amber-950 border-amber-900" },
  connected:    { th: "เชื่อมต่อ Main Hub แล้ว", color: "text-emerald-300", bg: "bg-emerald-950 border-emerald-900" },
  error:        { th: "Main Hub มีปัญหา", color: "text-red-300", bg: "bg-red-950 border-red-900" },
};

function timeAgo(ts?: number) {
  if (!ts) return "—";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60) return `${s}s ที่แล้ว`;
  if (s < 3600) return `${Math.floor(s / 60)}m ที่แล้ว`;
  return `${Math.floor(s / 3600)}h ที่แล้ว`;
}

export function HubStatusBanner() {
  const hub = useResQ((s) => s.hub);
  const setHubUrl = useResQ((s) => s.setHubUrl);
  const setDemoMode = useResQ((s) => s.setDemoMode);
  const [editing, setEditing] = useState(false);
  const [draftUrl, setDraftUrl] = useState(hub.url);

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
              className={`inline-block h-2 w-2 rounded-full ${hub.state === "connected" ? "bg-emerald-400" : hub.state === "connecting" ? "bg-amber-400 animate-pulse" : "bg-red-400"}`}
            />
            <strong>{meta.th}</strong>
          </span>
          {hub.state === "connected" && (
            <span className="text-slate-400">
              {hub.hubId && <span className="font-mono">{hub.hubId}</span>}
              <span className="ml-2">• packets: <span className="font-mono text-emerald-300">{hub.loraPacketsReceived}</span></span>
              <span className="ml-2">• heartbeat: {timeAgo(hub.lastHeartbeat)}</span>
            </span>
          )}
          {hub.lastError && hub.state !== "connected" && (
            <span className="text-red-400">— {hub.lastError}</span>
          )}
        </div>

        <div className="flex items-center gap-2">
          {!editing ? (
            <>
              <span className="font-mono text-slate-500">{hub.url}</span>
              <button
                onClick={() => {
                  setDraftUrl(hub.url);
                  setEditing(true);
                }}
                className="rounded bg-panel px-2 py-0.5 text-slate-300 hover:bg-slate-700"
              >
                ตั้งค่า
              </button>
              {hub.state !== "connected" && (
                <button
                  onClick={() => setDemoMode(true)}
                  className="rounded bg-fuchsia-700 px-2 py-0.5 font-semibold text-white hover:bg-fuchsia-600"
                  title="ใช้ข้อมูลจำลองสำหรับทดสอบ UI โดยไม่ต้องใช้ฮาร์ดแวร์"
                >
                  เปิด Demo
                </button>
              )}
            </>
          ) : (
            <>
              <input
                value={draftUrl}
                onChange={(e) => setDraftUrl(e.target.value)}
                placeholder="ws://192.168.x.x:81/ws"
                className="w-72 rounded border border-panel-border bg-panel px-2 py-0.5 font-mono text-xs text-slate-200 outline-none focus:border-emerald-500"
                autoFocus
              />
              <button
                onClick={() => {
                  setHubUrl(draftUrl.trim());
                  setEditing(false);
                }}
                className="rounded bg-emerald-700 px-2 py-0.5 font-semibold text-white hover:bg-emerald-600"
              >
                บันทึก
              </button>
              <button
                onClick={() => setEditing(false)}
                className="rounded bg-panel px-2 py-0.5 text-slate-400 hover:bg-slate-700"
              >
                ยกเลิก
              </button>
            </>
          )}
        </div>
      </div>
    </div>
  );
}
