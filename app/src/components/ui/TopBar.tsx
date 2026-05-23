"use client";

import Link from "next/link";
import { useResQ } from "@/lib/store";

export function TopBar() {
  const incident = useResQ((s) => s.incident);
  const anchorCount = useResQ((s) => Object.keys(s.anchors).length);
  const wristbandCount = useResQ((s) => Object.keys(s.wristbands).length);

  return (
    <header className="flex items-center justify-between border-b border-panel-border bg-panel-soft px-4 py-2">
      <div className="flex items-center gap-3">
        <div className="grid h-8 w-8 place-items-center rounded-md bg-triage-red font-bold text-white">R</div>
        <div>
          <div className="text-sm font-semibold">ResQ-Band Command Center</div>
          <div className="text-xs text-slate-400">
            {incident.name} · เริ่ม{" "}
            {new Date(incident.startedAt).toLocaleString("th-TH", { hour: "2-digit", minute: "2-digit" })}
          </div>
        </div>
      </div>

      <div className="flex items-center gap-4 text-xs">
        <span className="rounded bg-panel px-2 py-1 text-slate-300">
          เสา <span className="font-mono text-orange-400">{anchorCount}</span> ตัว
        </span>
        <span className="rounded bg-panel px-2 py-1 text-slate-300">
          กำไล <span className="font-mono text-sky-400">{wristbandCount}</span> ตัว
        </span>
        <nav className="flex gap-2">
          <Link href="/" className="rounded bg-panel px-3 py-1 hover:bg-slate-700">
            แดชบอร์ด
          </Link>
          <Link href="/setup" className="rounded bg-panel px-3 py-1 hover:bg-slate-700">
            ตั้งค่ากำไล
          </Link>
          <Link href="/timeline" className="rounded bg-panel px-3 py-1 hover:bg-slate-700">
            Timeline
          </Link>
        </nav>
      </div>
    </header>
  );
}
