"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useResQ } from "@/lib/store";

const NAV = [
  { href: "/",         label: "แดชบอร์ด" },
  { href: "/register", label: "ลงทะเบียนผู้สวม" },
  { href: "/setup",    label: "ตั้งค่าเสา (Pin)" },
  { href: "/search",   label: "ค้นหา / Dispatch" },
  { href: "/timeline", label: "Timeline" },
];

export function TopBar() {
  const incident = useResQ((s) => s.incident);
  const anchorCount = useResQ((s) => Object.keys(s.anchors).length);
  const wristbandCount = useResQ((s) => Object.keys(s.wristbands).length);
  const searchMode = useResQ((s) => s.searchMode);
  const emergencyMode = useResQ((s) => s.hub.emergencyMode);
  const pathname = usePathname();

  return (
    <header className="flex items-center justify-between border-b border-panel-border bg-panel-soft px-4 py-2">
      <div className="flex items-center gap-3">
        <div className={`grid h-8 w-8 place-items-center rounded-md font-bold text-white ${emergencyMode ? "bg-triage-red animate-pulse" : "bg-triage-red"}`}>
          R
        </div>
        <div>
          <div className="text-sm font-semibold">
            ResQ-Band Command Center
            {emergencyMode && <span className="ml-2 rounded bg-red-700 px-1.5 py-0.5 text-[10px] font-bold uppercase">EMERGENCY</span>}
            {searchMode && !emergencyMode && <span className="ml-2 rounded bg-amber-700 px-1.5 py-0.5 text-[10px] font-bold uppercase">SEARCH MODE</span>}
          </div>
          <div className="text-xs text-slate-400">
            {incident.name} · เริ่ม{" "}
            {new Date(incident.startedAt).toLocaleString("th-TH", { hour: "2-digit", minute: "2-digit" })}
          </div>
        </div>
      </div>

      <div className="flex items-center gap-4 text-xs">
        <span className="rounded bg-panel px-2 py-1 text-slate-300">
          เสา <span className="font-mono text-orange-400">{anchorCount}</span>/4
        </span>
        <span className="rounded bg-panel px-2 py-1 text-slate-300">
          กำไล <span className="font-mono text-sky-400">{wristbandCount}</span>
        </span>
        <nav className="flex gap-1">
          {NAV.map((n) => {
            const active = pathname === n.href;
            return (
              <Link
                key={n.href}
                href={n.href}
                className={`rounded px-3 py-1 transition ${
                  active
                    ? "bg-emerald-700 font-semibold text-white"
                    : "bg-panel text-slate-300 hover:bg-slate-700"
                }`}
              >
                {n.label}
              </Link>
            );
          })}
        </nav>
      </div>
    </header>
  );
}
