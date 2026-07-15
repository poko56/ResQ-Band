"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useResQ } from "@/lib/store";

const NAV: { href: string; label: string }[] = [
  { href: "/",         label: "Live" },
  { href: "/register", label: "Roster" },
  { href: "/setup",    label: "Anchors" },
  { href: "/search",   label: "Dispatch" },
  { href: "/timeline", label: "Log" },
];

export function TopBar() {
  const incident       = useResQ((s) => s.incident);
  const anchorCount    = useResQ((s) => Object.keys(s.anchors).length);
  const wristbandCount = useResQ((s) => Object.keys(s.wristbands).length);
  const searchMode     = useResQ((s) => s.searchMode);
  const emergencyMode  = useResQ((s) => s.hub.emergencyMode);
  const pathname       = usePathname();

  return (
    <header className="flex h-8 items-stretch bg-app-surface border-b border-app-divider select-none">
      {/* Brand */}
      <div className="flex items-center gap-2 px-3 border-r border-app-divider">
        <div className="grid h-4 w-4 place-items-center bg-triage-red text-white text-[10px] font-bold">R</div>
        <span className="text-xs font-semibold tracking-[0.2em] text-app-text">RESQ</span>
        <span className="font-mono text-2xs text-app-muted">v0.2.0</span>
      </div>

      {/* Tabs */}
      <nav className="flex items-stretch">
        {NAV.map((n) => {
          const active = pathname === n.href;
          return (
            <Link key={n.href} href={n.href} className={`tab ${active ? "is-active" : ""}`}>
              {n.label}
            </Link>
          );
        })}
      </nav>

      {/* Right cluster */}
      <div className="ml-auto flex items-stretch">
        {emergencyMode && (
          <span className="flex items-center px-2 bg-triage-red text-white text-2xs font-bold tracking-wider uppercase">
            EMERGENCY
          </span>
        )}
        {searchMode && !emergencyMode && (
          <span className="flex items-center px-2 bg-triage-yellow text-app-bg text-2xs font-bold tracking-wider uppercase">
            SEARCH
          </span>
        )}

        <div className="flex items-center gap-2 px-3 border-l border-app-divider">
          <span className="text-xs text-app-dim truncate max-w-[260px]">{incident.name}</span>
          <span className="font-mono text-2xs text-app-muted">
            {new Date(incident.startedAt).toLocaleTimeString("th-TH", { hour: "2-digit", minute: "2-digit" })}
          </span>
        </div>

        <div className="flex items-center gap-3 px-3 border-l border-app-divider">
          <span className="text-2xs uppercase tracking-wider text-app-muted">Anchors</span>
          <span className={`font-mono text-xs ${anchorCount === 4 ? "text-status-ok" : "text-status-warn"}`}>{anchorCount}/4</span>
          <span className="text-2xs uppercase tracking-wider text-app-muted ml-1">Bands</span>
          <span className="font-mono text-xs text-status-info">{wristbandCount}</span>
        </div>
      </div>
    </header>
  );
}
