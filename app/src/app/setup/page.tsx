"use client";

// ============================================================================
// /setup - ResQ-Pin (tower) administration.
//
// Place / rename / delete the four LoRa anchor towers and bind each to a
// physical pinIndex (0..3) that maps to its TDMA slot. The map on the right
// is the same LiveMap component the dashboard uses, but with anchor click
// placement front-and-center.
// ============================================================================

import dynamic from "next/dynamic";
import { useState } from "react";
import { TopBar } from "@/components/ui/TopBar";
import { useResQ } from "@/lib/store";

const LiveMap = dynamic(() => import("@/components/Map/LiveMap"), {
  ssr: false,
  loading: () => <div className="grid h-full place-items-center text-sm text-slate-500">กำลังโหลดแผนที่...</div>,
});

const MAX_PINS = 4;

function timeAgo(ts?: number) {
  if (!ts) return "ยังไม่เคยส่งสัญญาณ";
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60)   return `${s} วินาทีที่แล้ว`;
  if (s < 3600) return `${Math.floor(s / 60)} นาทีที่แล้ว`;
  return `${Math.floor(s / 3600)} ชั่วโมงที่แล้ว`;
}

export default function PinSetupPage() {
  const anchors = useResQ((s) => s.anchors);
  const removeAnchor = useResQ((s) => s.removeAnchor);
  const renameAnchor = useResQ((s) => s.renameAnchor);
  const placementMode = useResQ((s) => s.placementMode);
  const setPlacementMode = useResQ((s) => s.setPlacementMode);

  const [editingId, setEditingId] = useState<string | null>(null);
  const [draftName, setDraftName] = useState("");

  // List of slots 0..MAX_PINS-1 with assigned anchor (if any)
  const slots = Array.from({ length: MAX_PINS }, (_, i) => {
    const anchor = Object.values(anchors).find((a) => a.pinIndex === i);
    return { index: i, anchor };
  });

  function startEdit(id: string, current: string) {
    setEditingId(id);
    setDraftName(current);
  }

  function commitEdit() {
    if (editingId && draftName.trim()) {
      renameAnchor(editingId, draftName.trim());
    }
    setEditingId(null);
    setDraftName("");
  }

  return (
    <div className="flex h-screen flex-col">
      <TopBar />

      <div className="grid flex-1 grid-cols-[420px_1fr] overflow-hidden">
        {/* ---- LIST ----------------------------------------------------- */}
        <aside className="flex flex-col border-r border-panel-border bg-panel-soft">
          <header className="border-b border-panel-border p-4">
            <h2 className="text-lg font-bold">เสา ResQ-Pin (Anchor)</h2>
            <p className="text-xs text-slate-400">
              วาง 4 ตัวรอบพื้นที่เสี่ยง — TDMA slot 0..3 — ตำแหน่งบนแผนที่ใช้สำหรับประมาณตำแหน่ง Band
            </p>
            <button
              onClick={() => setPlacementMode(placementMode === "anchor" ? "none" : "anchor")}
              className={`mt-3 w-full rounded px-3 py-2 text-sm font-bold ${
                placementMode === "anchor"
                  ? "bg-orange-500 text-white"
                  : "bg-emerald-700 text-white hover:bg-emerald-600"
              }`}
            >
              {placementMode === "anchor" ? "✕ ยกเลิกการวาง" : "+ คลิกบนแผนที่เพื่อวางเสา"}
            </button>
          </header>

          <ul className="flex-1 overflow-y-auto">
            {slots.map(({ index, anchor }) => (
              <li key={index} className="border-b border-panel-border p-3">
                <div className="flex items-start gap-3">
                  {/* Slot badge */}
                  <div
                    className={`grid h-10 w-10 shrink-0 place-items-center rounded font-bold ${
                      anchor
                        ? anchor.online
                          ? "bg-emerald-700 text-white"
                          : "bg-orange-700 text-white"
                        : "bg-slate-700 text-slate-500"
                    }`}
                  >
                    {index}
                  </div>

                  {/* Info / actions */}
                  <div className="min-w-0 flex-1">
                    {!anchor ? (
                      <>
                        <div className="text-sm text-slate-500">Slot ว่าง</div>
                        <div className="text-xs text-slate-600">คลิกบนแผนที่เพื่อวางเสาในตำแหน่งนี้</div>
                      </>
                    ) : editingId === anchor.id ? (
                      <div className="flex items-center gap-2">
                        <input
                          autoFocus
                          value={draftName}
                          onChange={(e) => setDraftName(e.target.value)}
                          onKeyDown={(e) => {
                            if (e.key === "Enter") commitEdit();
                            if (e.key === "Escape") { setEditingId(null); setDraftName(""); }
                          }}
                          className="flex-1 rounded border border-emerald-500 bg-panel px-2 py-1 text-sm outline-none"
                        />
                        <button
                          onClick={commitEdit}
                          className="rounded bg-emerald-700 px-2 py-1 text-xs font-bold text-white hover:bg-emerald-600"
                        >
                          บันทึก
                        </button>
                      </div>
                    ) : (
                      <>
                        <div className="flex items-center gap-2">
                          <span className="text-sm font-semibold">{anchor.name}</span>
                          <span className="font-mono text-[10px] text-slate-500">{anchor.id}</span>
                          {anchor.online ? (
                            <span className="rounded bg-emerald-700 px-1.5 py-0.5 text-[10px] font-bold text-white">ONLINE</span>
                          ) : (
                            <span className="rounded bg-orange-700 px-1.5 py-0.5 text-[10px] font-bold text-white">OFFLINE</span>
                          )}
                        </div>
                        <div className="text-xs text-slate-400">
                          <span className="font-mono">{anchor.position.lat.toFixed(6)}, {anchor.position.lng.toFixed(6)}</span>
                        </div>
                        <div className="text-[11px] text-slate-500">ล่าสุด: {timeAgo(anchor.lastSeen)}</div>
                        <div className="mt-1.5 flex gap-1.5">
                          <button
                            onClick={() => startEdit(anchor.id, anchor.name)}
                            className="rounded bg-slate-700 px-2 py-1 text-[11px] text-slate-200 hover:bg-slate-600"
                          >
                            เปลี่ยนชื่อ
                          </button>
                          <button
                            onClick={() => removeAnchor(anchor.id)}
                            className="rounded bg-slate-700 px-2 py-1 text-[11px] text-slate-200 hover:bg-red-700 hover:text-white"
                          >
                            ลบ
                          </button>
                        </div>
                      </>
                    )}
                  </div>
                </div>
              </li>
            ))}
          </ul>

          <footer className="border-t border-panel-border bg-panel p-3 text-[11px] text-slate-500">
            <strong className="text-slate-300">หมายเหตุ:</strong> ค่า position ใช้เพื่อแสดงตำแหน่งของ Band บนแผนที่
            (ระบบจะอ้างจาก Pin ที่มี RSSI ดีที่สุดเป็นตำแหน่งล่าสุด)
          </footer>
        </aside>

        {/* ---- MAP ------------------------------------------------------- */}
        <main className="relative">
          <LiveMap />
          {placementMode === "anchor" && (
            <div className="pointer-events-none absolute left-1/2 top-4 z-[1000] -translate-x-1/2 rounded bg-orange-500 px-3 py-1 text-xs font-bold text-white shadow-lg">
              คลิกบนแผนที่เพื่อวางเสาในสล็อตว่างถัดไป
            </div>
          )}
        </main>
      </div>
    </div>
  );
}
