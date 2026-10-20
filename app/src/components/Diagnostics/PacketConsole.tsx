"use client";

// ============================================================================
// Live packet console: every event MainNode sends over USB, filterable by kind
// and device, with pause, raw JSON and export. Answers "is the hub hearing
// band X?" without unplugging anything.
// ============================================================================

import React, { useMemo, useState } from "react";
import { useResQ } from "@/lib/store";
import { describePacket, KIND_META, mainTs, type PacketKind } from "@/lib/packetFormat";
import type { PacketLogEntry } from "@/lib/types";

const KINDS: PacketKind[] = ["band", "sos", "pin", "beacon", "assignment", "found", "ring", "stats", "system", "error"];
const DEFAULT_HIDDEN: PacketKind[] = ["stats", "system"];
const MAX_ROWS = 300;

function timeLabel(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString("th-TH", { hour12: false }) + "." + String(d.getMilliseconds()).padStart(3, "0");
}

export function PacketConsole() {
  const log = useResQ((s) => s.packetLog);
  const wristbands = useResQ((s) => s.wristbands);
  const clear = useResQ((s) => s.clearPacketLog);

  const [hidden, setHidden] = useState<Set<PacketKind>>(new Set(DEFAULT_HIDDEN));
  const [query, setQuery] = useState("");
  const [frozen, setFrozen] = useState<PacketLogEntry[] | null>(null);
  const [open, setOpen] = useState<number | null>(null);

  const source = frozen ?? log;

  const rows = useMemo(() => {
    const q = query.trim().toLowerCase();
    const out: { e: PacketLogEntry; info: ReturnType<typeof describePacket> }[] = [];
    for (let i = source.length - 1; i >= 0 && out.length < MAX_ROWS; i--) {
      const e = source[i];
      const info = describePacket(e.ev, wristbands);
      if (hidden.has(info.kind)) continue;
      if (q && !`${info.source} ${info.summary} ${JSON.stringify(e.ev)}`.toLowerCase().includes(q)) continue;
      out.push({ e, info });
    }
    return out;
  }, [source, hidden, query, wristbands]);

  const counts = useMemo(() => {
    const c: Partial<Record<PacketKind, number>> = {};
    const minuteAgo = Date.now() - 60_000;
    let perMin = 0;
    for (const e of source) {
      const k = describePacket(e.ev, wristbands).kind;
      c[k] = (c[k] ?? 0) + 1;
      if (e.rxAt >= minuteAgo && k !== "stats") perMin++;
    }
    return { byKind: c, perMin };
  }, [source, wristbands]);

  function toggle(k: PacketKind) {
    setHidden((h) => {
      const n = new Set(h);
      n.has(k) ? n.delete(k) : n.add(k);
      return n;
    });
  }

  function exportJsonl() {
    const body = source.map((e) => JSON.stringify({ rxAt: new Date(e.rxAt).toISOString(), ...e.ev })).join("\n");
    const blob = new Blob([body + "\n"], { type: "application/x-ndjson" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `resq-packets-${Date.now()}.jsonl`;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  return (
    <div className="panel flex min-h-0 flex-col">
      <div className="panel-header justify-between">
        <span>Packet Console · ทุก event จาก MainNode แบบสด</span>
        <span className="font-mono text-2xs text-app-dim">
          {counts.perMin} packet/นาที (ไม่นับ stats) · เก็บล่าสุด {source.length} รายการ
        </span>
      </div>

      {/* Filters: one row above the list */}
      <div className="flex flex-wrap items-center gap-2 border-b border-app-divider p-2">
        <input
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          placeholder="ค้นหา ID / ชื่อ / ข้อความ เช่น 3CB88B2B"
          className="h-6 w-64 rounded-sm border border-app-border bg-app-input px-2 text-2xs text-app-text placeholder:text-app-muted"
        />
        <div className="flex flex-wrap gap-1">
          {KINDS.map((k) => (
            <button
              key={k}
              onClick={() => toggle(k)}
              className={`flex h-6 items-center gap-1.5 rounded-sm border px-2 text-2xs ${hidden.has(k) ? "border-app-border text-app-muted opacity-60" : "border-app-border text-app-text"}`}
              title={hidden.has(k) ? "กดเพื่อแสดง" : "กดเพื่อซ่อน"}
            >
              <span className="inline-block h-2 w-2 rounded-full" style={{ backgroundColor: KIND_META[k].color }} />
              {KIND_META[k].label}
              <span className="font-mono text-app-muted">{counts.byKind[k] ?? 0}</span>
            </button>
          ))}
        </div>
        <div className="ml-auto flex gap-1">
          <button onClick={() => setFrozen(frozen ? null : [...log])}
            className={`btn btn-sm ${frozen ? "btn-accent" : "btn-ghost border border-app-border"}`}>
            {frozen ? "▶ ต่อ" : "⏸ หยุดดู"}
          </button>
          <button onClick={exportJsonl} className="btn btn-sm btn-ghost border border-app-border">Export .jsonl</button>
          <button onClick={() => { clear(); setFrozen(null); }} className="btn btn-sm btn-ghost border border-app-border hover:text-status-err">ล้าง</button>
        </div>
      </div>

      <div className="max-h-[60vh] min-h-[240px] overflow-y-auto font-mono text-2xs">
        {rows.length === 0 ? (
          <div className="py-10 text-center font-sans text-xs text-app-muted">
            {source.length === 0 ? "ยังไม่มี packet — เชื่อมต่อ MainNode หรือเปิดโหมด Demo" : "ไม่มี packet ที่ตรงกับตัวกรอง"}
          </div>
        ) : (
          <table className="w-full border-collapse">
            <thead className="sticky top-0 bg-app-panel text-left text-app-muted">
              <tr className="border-b border-app-divider">
                <th className="px-2 py-1 font-sans font-normal">เวลา</th>
                <th className="px-2 py-1 font-sans font-normal">MainNode ms</th>
                <th className="px-2 py-1 font-sans font-normal">ชนิด</th>
                <th className="px-2 py-1 font-sans font-normal">จาก</th>
                <th className="px-2 py-1 font-sans font-normal">รายละเอียด</th>
              </tr>
            </thead>
            <tbody>
              {rows.map(({ e, info }) => (
                <React.Fragment key={e.id}>
                  <tr onClick={() => setOpen(open === e.id ? null : e.id)}
                      className="cursor-pointer border-b border-app-divider/60 hover:bg-app-raised">
                    <td className="whitespace-nowrap px-2 py-0.5 text-app-dim">{timeLabel(e.rxAt)}</td>
                    <td className="whitespace-nowrap px-2 py-0.5 text-app-muted">{mainTs(e.ev) ?? ""}</td>
                    <td className="whitespace-nowrap px-2 py-0.5">
                      <span className="inline-flex items-center gap-1.5">
                        <span className="inline-block h-2 w-2 rounded-full" style={{ backgroundColor: KIND_META[info.kind].color }} />
                        <span className="text-app-text">{KIND_META[info.kind].label}</span>
                      </span>
                    </td>
                    <td className="whitespace-nowrap px-2 py-0.5 text-app-text">{info.source}</td>
                    <td className="px-2 py-0.5 text-app-dim">{info.summary}</td>
                  </tr>
                  {open === e.id && (
                    <tr className="bg-app-input">
                      <td colSpan={5} className="px-3 py-2">
                        <pre className="select-text whitespace-pre-wrap break-all text-app-text">{JSON.stringify(e.ev, null, 2)}</pre>
                      </td>
                    </tr>
                  )}
                </React.Fragment>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}
