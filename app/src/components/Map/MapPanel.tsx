"use client";

import dynamic from "next/dynamic";
import { useResQ } from "@/lib/store";

const LiveMap = dynamic(() => import("./LiveMap"), {
  ssr: false,
  loading: () => (
    <div className="grid h-full place-items-center text-sm text-slate-500">กำลังโหลดแผนที่...</div>
  ),
});

export function MapPanel() {
  const placementMode = useResQ((s) => s.placementMode);
  const setPlacementMode = useResQ((s) => s.setPlacementMode);
  const anchors = useResQ((s) => s.anchors);

  return (
    <div className="relative h-full w-full">
      <LiveMap />

      <div className="absolute right-3 top-3 z-[1000] flex flex-col gap-2">
        <button
          onClick={() => setPlacementMode(placementMode === "anchor" ? "none" : "anchor")}
          className={`rounded px-3 py-2 text-xs font-bold shadow-lg ${
            placementMode === "anchor" ? "bg-orange-500 text-white" : "bg-panel-soft text-slate-200 hover:bg-slate-700"
          }`}
        >
          {placementMode === "anchor" ? "ยกเลิก" : "+ วางเสา Anchor"}
        </button>
        <div className="rounded bg-panel-soft px-3 py-2 text-xs text-slate-400 shadow-lg">
          เสาทั้งหมด: <span className="font-mono text-orange-400">{Object.keys(anchors).length}</span>
          {Object.keys(anchors).length < 4 && (
            <div className="text-[10px] text-amber-400">แนะนำ ≥ 4 ตัวสำหรับ triangulation</div>
          )}
        </div>
      </div>
    </div>
  );
}
