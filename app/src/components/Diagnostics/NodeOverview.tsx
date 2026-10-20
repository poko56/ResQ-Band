"use client";

// ============================================================================
// Node overview for Dev Tools: system totals, a per-device dropout timeline,
// traffic per minute, and a status card for every device (MainNode, pillars,
// wristbands, handhelds) built from the activity ledger in lib/activity.ts.
// ============================================================================

import React, { useEffect, useMemo, useState } from "react";
import { useSessionState } from "@/lib/useSessionState";
import { useResQ } from "@/lib/store";
import {
  ACTIVITY_BUCKET_MS,
  formatBytes,
  formatDuration,
  lossPct,
  totalDowntimeMs,
  uptimePct,
} from "@/lib/activity";
import type { NodeActivity, NodeKind } from "@/lib/types";
import { TrendChart } from "./TrendChart";

const KIND_ORDER: NodeKind[] = ["hub", "pin", "band", "resq"];
const KIND_LABEL: Record<NodeKind, string> = {
  hub: "MainNode (ศูนย์สั่งการ)",
  pin: "เสา (ResQ-Pin)",
  band: "กำไล (Band-Node)",
  resq: "เครื่องกู้ภัย (ResQ-Node)",
};

// What each packet/event type means, for the "ทำอะไรบ้าง" breakdown.
const TYPE_LABEL: Record<string, string> = {
  HEARTBEAT: "ส่งชีพจร",
  SOS_VITALS: "SOS ชีพจรวิกฤต",
  SOS_TAP: "SOS เคาะ",
  SOS_FALL: "SOS ล้ม",
  RING_ACK: "ตอบรับเสียงเตือน",
  PIN_SIGHTING: "รายงาน RSSI",
  BEACON: "ส่ง beacon",
  ASSIGNMENT: "สั่งงานกู้ภัย",
  STATS: "สถิติ USB",
  FOUND: "แจ้งผลการช่วย",
};

const WINDOWS = [
  { min: 5, label: "5 นาที" },
  { min: 15, label: "15 นาที" },
  { min: 30, label: "30 นาที" },
];

function lossText(pct: number): string {
  if (pct <= 2) return "text-emerald-400";
  if (pct <= 10) return "text-amber-400";
  return "text-rose-400";
}

function sortNodes(nodes: NodeActivity[]): NodeActivity[] {
  return [...nodes].sort((a, b) =>
    KIND_ORDER.indexOf(a.kind) - KIND_ORDER.indexOf(b.kind) || a.key.localeCompare(b.key, undefined, { numeric: true }),
  );
}

// Ages and uptime change every second even when no packet arrives.
function useNow(intervalMs = 1000): number {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), intervalMs);
    return () => clearInterval(t);
  }, [intervalMs]);
  return now;
}

export function NodeOverview() {
  const activity = useResQ((s) => s.diagnostics.activity);
  const now = useNow();
  const [windowMin, setWindowMin] = useSessionState("overview:window", 15);

  const nodes = useMemo(() => sortNodes(Object.values(activity)), [activity]);

  const totals = useMemo(() => {
    let rx = 0, lost = 0, bytes = 0, disconnects = 0, online = 0, lastMinRx = 0, lastMinBytes = 0;
    const minuteAgo = now - 60_000;
    for (const n of nodes) {
      rx += n.rx;
      lost += n.lost;
      bytes += n.bytes;
      disconnects += n.disconnects;
      if (n.online) online++;
      for (const b of n.buckets) {
        if (b.t >= minuteAgo) { lastMinRx += b.rx; lastMinBytes += b.bytes; }
      }
    }
    return { rx, lost, bytes, disconnects, online, lastMinRx, lastMinBytes, loss: lossPct({ rx, lost }) };
  }, [nodes, now]);

  return (
    <div className="space-y-3">
      <SystemSummary totals={totals} nodeCount={nodes.length} />

      <div className="panel p-3">
        <div className="panel-header -mx-3 -mt-3 mb-3 justify-between">
          <span>ไทม์ไลน์การเชื่อมต่อ: หลุดตอนไหน นานแค่ไหน (ช่องละ {ACTIVITY_BUCKET_MS / 1000} วินาที = 1 รอบ TDMA)</span>
          <div className="flex items-center gap-1 font-sans">
            {WINDOWS.map((w) => (
              <button
                key={w.min}
                onClick={() => setWindowMin(w.min)}
                className={`btn btn-sm ${windowMin === w.min ? "btn-accent" : "btn-ghost border border-app-border"}`}
              >
                {w.label}
              </button>
            ))}
          </div>
        </div>
        {nodes.length === 0 ? (
          <EmptyState />
        ) : (
          <>
            <UptimeTimeline nodes={nodes} now={now} windowMin={windowMin} />
            <TimelineLegend />
          </>
        )}
      </div>

      <div className="panel p-3">
        <div className="panel-header -mx-3 -mt-3 mb-3 justify-between">
          <span>กราฟเส้น: ปริมาณข้อมูลและการหลุดต่อนาที</span>
          <span className="font-mono text-2xs text-app-dim">รวมทุกอุปกรณ์ · {windowMin} นาทีล่าสุด</span>
        </div>
        {nodes.length === 0 ? <EmptyState /> : <TrendChart nodes={nodes} now={now} windowMin={windowMin} />}
      </div>

      <div className="panel p-3">
        <div className="panel-header -mx-3 -mt-3 mb-3 justify-between">
          <span>สถานะอุปกรณ์ทั้งหมด (Node Status)</span>
          <span className="font-mono text-2xs text-app-dim">{totals.online} / {nodes.length} ออนไลน์</span>
        </div>
        {nodes.length === 0 ? (
          <EmptyState />
        ) : (
          <div className="space-y-3">
            {KIND_ORDER.map((kind) => {
              const group = nodes.filter((n) => n.kind === kind);
              if (group.length === 0) return null;
              return (
                <div key={kind}>
                  <div className="mb-1.5 text-2xs uppercase tracking-wider text-app-muted">
                    {KIND_LABEL[kind]} · {group.filter((n) => n.online).length}/{group.length} ออนไลน์
                  </div>
                  <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-4 gap-2.5">
                    {group.map((n) => <NodeCard key={n.key} node={n} now={now} />)}
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}

// ----------------------------------------------------------------------------
// System summary
// ----------------------------------------------------------------------------
function SystemSummary({ totals, nodeCount }: {
  totals: { rx: number; lost: number; bytes: number; disconnects: number; online: number; lastMinRx: number; lastMinBytes: number; loss: number };
  nodeCount: number;
}) {
  const items: { label: string; value: string; sub: string; tone?: string }[] = [
    { label: "อุปกรณ์ออนไลน์", value: `${totals.online} / ${nodeCount}`, sub: "MainNode + เสา + กำไล + ResQ-Node",
      tone: nodeCount > 0 && totals.online === nodeCount ? "text-emerald-400" : "text-amber-400" },
    { label: "แพ็กเก็ตทั้งหมด", value: totals.rx.toLocaleString(), sub: `${totals.lastMinRx} แพ็กเก็ตในนาทีล่าสุด` },
    { label: "ข้อมูลทั้งหมด", value: formatBytes(totals.bytes), sub: `${formatBytes(totals.lastMinBytes)}/นาที (LoRa payload)` },
    { label: "หลุดรวม", value: `${totals.loss.toFixed(1)}%`, sub: `${totals.lost} จาก ${totals.rx + totals.lost} แพ็กเก็ต`, tone: lossText(totals.loss) },
    { label: "ขาดการติดต่อ", value: `${totals.disconnects}`, sub: "ครั้ง (รวมทุกอุปกรณ์)",
      tone: totals.disconnects > 0 ? "text-rose-400" : "text-emerald-400" },
  ];
  return (
    <div className="grid grid-cols-2 md:grid-cols-5 gap-2">
      {items.map((it) => (
        <div key={it.label} className="panel p-2.5">
          <div className="text-2xs uppercase tracking-wider text-app-muted">{it.label}</div>
          <div className={`mt-1 text-xl font-bold font-mono ${it.tone ?? "text-app-text"}`}>{it.value}</div>
          <div className="mt-0.5 truncate text-2xs text-app-muted">{it.sub}</div>
        </div>
      ))}
    </div>
  );
}

// ----------------------------------------------------------------------------
// Uptime timeline - one row per device, one cell per TDMA cycle
// ----------------------------------------------------------------------------
type CellState = "ok" | "lossy" | "missed" | "idle" | "none";

const CELL_COLOR: Record<CellState, string> = {
  ok: "#10b981",
  lossy: "#f59e0b",
  missed: "#f43f5e",
  idle: "#334155",
  none: "transparent",
};

function cellStates(n: NodeActivity, now: number, windowMin: number) {
  const count = Math.round((windowMin * 60_000) / ACTIVITY_BUCKET_MS);
  const currentStart = now - (now % ACTIVITY_BUCKET_MS);
  const byT = new Map(n.buckets.map((b) => [b.t, b]));
  const cells: { t: number; state: CellState; rx: number; lost: number }[] = [];
  for (let i = count - 1; i >= 0; i--) {
    const t = currentStart - i * ACTIVITY_BUCKET_MS;
    const b = byT.get(t);
    let state: CellState;
    if (t + ACTIVITY_BUCKET_MS <= n.firstSeen) state = "none";
    else if (b && b.lost > 0) state = "lossy";
    else if (b) state = "ok";
    else if (t === currentStart) state = "idle";              // cycle still in progress
    else state = n.kind === "resq" ? "idle" : "missed";        // handheld is event-driven
    cells.push({ t, state, rx: b?.rx ?? 0, lost: b?.lost ?? 0 });
  }
  return cells;
}

function UptimeTimeline({ nodes, now, windowMin }: { nodes: NodeActivity[]; now: number; windowMin: number }) {
  return (
    <div className="space-y-1">
      {nodes.map((n) => {
        const cells = cellStates(n, now, windowMin);
        const w = 100 / cells.length;
        const missed = cells.filter((c) => c.state === "missed").length;
        const lossy = cells.filter((c) => c.state === "lossy").length;
        return (
          <div key={n.key} className="flex items-center gap-2">
            <div className="w-40 shrink-0 truncate text-2xs">
              <span className={`mr-1.5 inline-block h-1.5 w-1.5 rounded-full ${n.online ? "bg-emerald-500" : "bg-rose-500"}`} />
              <span className="text-app-text">{n.label}</span>
            </div>
            <svg className="h-4 flex-1" viewBox="0 0 100 10" preserveAspectRatio="none" role="img"
                 aria-label={`${n.label}: ${missed} รอบหาย, ${lossy} รอบหลุดบางส่วน`}>
              {cells.map((c, i) => (
                <rect key={c.t} x={i * w} y={0} width={Math.max(w - 0.15, 0.05)} height={10} fill={CELL_COLOR[c.state]}>
                  <title>
                    {new Date(c.t).toLocaleTimeString("th-TH")} · {c.state === "missed" ? "ไม่ได้รับเลย" : c.state === "none" ? "ยังไม่เริ่ม" : `${c.rx} แพ็กเก็ต${c.lost ? `, หลุด ${c.lost}` : ""}`}
                  </title>
                </rect>
              ))}
            </svg>
            <div className="w-36 shrink-0 whitespace-nowrap text-right font-mono text-3xs text-app-muted">
              {missed > 0 ? <span className="text-rose-400">หาย {missed} รอบ</span> : <span className="text-emerald-400">ครบทุกรอบ</span>}
              {lossy > 0 && <span className="text-amber-400"> · หลุด {lossy}</span>}
            </div>
          </div>
        );
      })}
      <div className="flex justify-between pl-[10.5rem] pr-[9.5rem] font-mono text-3xs text-app-dim">
        <span>-{windowMin} นาที</span>
        <span>ตอนนี้</span>
      </div>
    </div>
  );
}

function TimelineLegend() {
  const items: [CellState, string][] = [
    ["ok", "ได้รับครบ"],
    ["lossy", "ได้รับแต่มีแพ็กเก็ตหลุด"],
    ["missed", "ไม่ได้รับเลยในรอบนั้น"],
    ["idle", "ไม่มีข้อมูล / รอบปัจจุบัน"],
  ];
  return (
    <div className="mt-2 flex flex-wrap gap-4 border-t border-app-divider pt-2 text-2xs text-app-muted">
      {items.map(([state, label]) => (
        <span key={state} className="flex items-center gap-1.5">
          <span className="h-2 w-3 rounded-xs" style={{ backgroundColor: CELL_COLOR[state] }} />
          {label}
        </span>
      ))}
    </div>
  );
}

// ----------------------------------------------------------------------------
// Per-device status card
// ----------------------------------------------------------------------------
function NodeCard({ node: n, now }: { node: NodeActivity; now: number }) {
  const hub = useResQ((s) => s.hub);
  const pinDiag = useResQ((s) => (n.kind === "pin" ? s.diagnostics.pins[Number(n.key.slice(4))] : undefined));
  const bandDiag = useResQ((s) => (n.kind === "band" ? s.diagnostics.bands[n.key.slice(5)] : undefined));
  const sighting = useResQ((s) => (n.kind === "band" ? s.sightings[n.key.slice(5)] : undefined));

  const loss = lossPct(n);
  const up = uptimePct(n, now);
  const down = totalDowntimeMs(n, now);
  const types = Object.entries(n.byType).sort((a, b) => b[1] - a[1]);

  const rows: [string, React.ReactNode][] = [
    ["ล่าสุด", `${formatDuration(now - n.lastSeen)} ที่แล้ว`],
    [n.online ? "ออนไลน์มา" : "ออฟไลน์มา",
      n.online ? formatDuration(now - n.onlineSince) : <span className="text-rose-400">{formatDuration(now - (n.offlineSince ?? n.lastSeen))}</span>],
    ["Uptime", <span className={up >= 98 ? "text-emerald-400" : up >= 90 ? "text-amber-400" : "text-rose-400"}>{up.toFixed(1)}%</span>],
    ["ขาดการติดต่อ", `${n.disconnects} ครั้ง · รวม ${formatDuration(down)}`],
    ["แพ็กเก็ต", `${n.rx.toLocaleString()} ได้รับ · ${n.lost} หลุด`],
    ["หลุด", <span className={lossText(loss)}>{loss.toFixed(1)}%</span>],
    ["ข้อมูล", formatBytes(n.bytes)],
  ];

  if (n.kind === "hub") {
    rows.push(["Firmware", hub.fw ?? "--"]);
    rows.push(["TDMA cycle", `#${hub.cycleId ?? 0}`]);
    rows.push(["LoRa", hub.loraReady === false ? <span className="text-rose-400">ไม่พร้อม</span> : "พร้อม"]);
  }
  if (n.kind === "pin" && pinDiag) {
    rows.push(["Device ID", pinDiag.pinId]);
    rows.push(["Slot TDMA", `${3 + pinDiag.pinIndex}`]);
    rows.push(["สัญญาณถึง Hub", `${pinDiag.rssiToHub} dBm · ${pinDiag.snrToHub} dB`]);
    rows.push(["ได้ยินกำไล", `${pinDiag.bandsHeardHistory.slice(-1)[0]?.count ?? 0} เครื่อง`]);
  }
  if (n.kind === "band") {
    if (bandDiag) {
      rows.push(["Seq ล่าสุด", `${bandDiag.lastSeq ?? "--"}`]);
      rows.push(["สัญญาณถึง Hub", `${bandDiag.currentRssi} dBm · ${bandDiag.currentSnr} dB`]);
      rows.push(["เสาที่ได้ยิน", `${Object.keys(bandDiag.pinStats).length} ต้น`]);
    }
    if (sighting) rows.push(["ชีพจร / SpO₂", `${sighting.heartRate || "--"} / ${sighting.spo2 || "--"}%`]);
  }

  return (
    <div className={`rounded border p-2.5 ${n.online ? "border-app-border bg-app-surface" : "border-rose-900 bg-rose-950/20"}`}>
      <div className="flex items-center justify-between gap-2">
        <span className="truncate text-xs font-semibold text-app-text">{n.label}</span>
        <span className={`shrink-0 rounded px-1.5 py-0.5 text-3xs font-semibold ${n.online ? "border border-emerald-800 bg-emerald-950 text-emerald-400" : "border border-rose-800 bg-rose-950 text-rose-400"}`}>
          {n.online ? "ONLINE" : "OFFLINE"}
        </span>
      </div>

      <div className="mt-2 space-y-0.5 text-2xs">
        {rows.map(([k, v]) => (
          <div key={k} className="flex justify-between gap-2">
            <span className="text-app-muted">{k}</span>
            <span className="truncate text-right font-mono text-app-text">{v}</span>
          </div>
        ))}
      </div>

      <div className="mt-2 border-t border-app-divider pt-1.5">
        <div className="mb-1 text-3xs uppercase tracking-wider text-app-muted">ทำอะไรบ้าง</div>
        <div className="flex flex-wrap gap-1">
          {types.map(([t, c]) => (
            <span key={t} className="rounded border border-app-border px-1 py-0.5 font-mono text-3xs text-app-dim" title={t}>
              {TYPE_LABEL[t] ?? t} ×{c}
            </span>
          ))}
        </div>
      </div>
    </div>
  );
}

function EmptyState() {
  return (
    <div className="py-6 text-center text-xs text-app-muted">
      ยังไม่มีข้อมูลจากอุปกรณ์ — เชื่อมต่อ MainNode หรือเปิดโหมด Demo
    </div>
  );
}
