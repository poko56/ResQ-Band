"use client";

import Link from "next/link";
import dynamic from "next/dynamic";
import { useResQ } from "@/lib/store";

const LiveMap = dynamic(() => import("./LiveMap"), {
  ssr: false,
  loading: () => (
    <div className="grid h-full place-items-center text-sm text-slate-500">กำลังโหลดแผนที่...</div>
  ),
});

const MAX_PINS = 4;

export function MapPanel() {
  const placementMode = useResQ((s) => s.placementMode);
  const setPlacementMode = useResQ((s) => s.setPlacementMode);
  const anchors = useResQ((s) => s.anchors);

  const slots = Array.from({ length: MAX_PINS }, (_, i) => {
    return Object.values(anchors).find((a) => a.pinIndex === i);
  });
  const placedCount = slots.filter(Boolean).length;
  const onlineCount = slots.filter((a) => a?.online).length;

  return (
    <div className="relative h-full w-full">
      <LiveMap />

      <div className="absolute right-3 top-3 z-[1000] flex w-[220px] flex-col gap-2">
        <button
          onClick={() => setPlacementMode(placementMode === "anchor" ? "none" : "anchor")}
          className={`rounded px-3 py-2 text-xs font-bold shadow-lg ${
            placementMode === "anchor"
              ? "bg-orange-500 text-white"
              : "bg-panel-soft text-slate-200 hover:bg-slate-700"
          }`}
        >
          {placementMode === "anchor" ? "✕ ยกเลิก" : "+ วางเสา Pin"}
        </button>

        <div className="rounded border border-panel-border bg-panel-soft p-2 shadow-lg">
          <div className="mb-1.5 flex items-center justify-between text-[11px] uppercase text-slate-400">
            <span>เสา ResQ-Pin</span>
            <span className="font-mono">
              <span className={onlineCount === placedCount && placedCount > 0 ? "text-emerald-400" : "text-orange-400"}>{onlineCount}</span>
              <span className="text-slate-500">/{placedCount}/{MAX_PINS}</span>
            </span>
          </div>
          <div className="grid grid-cols-4 gap-1">
            {slots.map((a, i) => (
              <div
                key={i}
                className={`grid h-8 place-items-center rounded text-[11px] font-bold ${
                  !a
                    ? "border border-dashed border-slate-600 text-slate-600"
                    : a.online
                      ? "bg-emerald-700 text-white"
                      : "bg-orange-700 text-white"
                }`}
                title={a ? `${a.name} (${a.online ? "online" : "offline"})` : `Slot ${i} ว่าง`}
              >
                {i}
              </div>
            ))}
          </div>
          {placedCount < MAX_PINS && (
            <p className="mt-1.5 text-[10px] text-amber-400">
              แนะนำให้วางครบ 4 ตัวเพื่อ coverage รอบพื้นที่
            </p>
          )}
          <Link
            href="/setup"
            className="mt-2 block rounded bg-panel px-2 py-1 text-center text-[11px] text-slate-300 hover:bg-slate-700"
          >
            จัดการเสาเต็มรูปแบบ →
          </Link>
        </div>
      </div>
    </div>
  );
}
