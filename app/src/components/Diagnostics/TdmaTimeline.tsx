"use client";

// ============================================================================
// TDMA slot timeline: each row is one 10 s cycle, each column one 1 s slot.
// Packets sit where they were transmitted inside their slot (bar width =
// airtime), so out-of-slot senders, shared slots and overlaps are visible.
// ============================================================================

import React, { useMemo, useState } from "react";
import { useResQ } from "@/lib/store";
import { analyzeTdma, type Arrival, type Severity } from "@/lib/tdma";
import { KIND_META } from "@/lib/packetFormat";
import { SLOT_LABELS, TDMA_SLOTS, TDMA_SLOT_MS, LORA } from "@/lib/lora";

const SEVERITY_STYLE: Record<Severity, { icon: string; cls: string }> = {
  error: { icon: "⛔", cls: "border-rose-800 bg-rose-950/40 text-rose-300" },
  warn:  { icon: "⚠️", cls: "border-amber-800 bg-amber-950/30 text-amber-300" },
  info:  { icon: "ℹ️", cls: "border-app-border bg-app-surface text-app-dim" },
};

function slotsLabel(slots: Record<number, number>) {
  return Object.entries(slots)
    .sort((a, b) => Number(a[0]) - Number(b[0]))
    .map(([s, n]) => `${s}×${n}`)
    .join("  ");
}

export function TdmaTimeline() {
  const log = useResQ((s) => s.packetLog);
  const wristbands = useResQ((s) => s.wristbands);
  const [cycles, setCycles] = useState(12);
  const [hover, setHover] = useState<Arrival | null>(null);

  const a = useMemo(() => analyzeTdma(log, wristbands, cycles), [log, wristbands, cycles]);

  return (
    <div className="space-y-3">
      <div className="panel">
        <div className="panel-header justify-between">
          <span>TDMA Timeline · แต่ละแถว = 1 รอบ (10 วินาที), แต่ละช่อง = 1 slot</span>
          <div className="flex items-center gap-1 font-sans">
            {[6, 12, 24].map((n) => (
              <button key={n} onClick={() => setCycles(n)}
                className={`btn btn-sm ${cycles === n ? "btn-accent" : "btn-ghost border border-app-border"}`}>{n} รอบ</button>
            ))}
          </div>
        </div>

        <div className="overflow-x-auto p-3">
          <div className="min-w-[760px]">
            {/* Slot header */}
            <div className="grid grid-cols-[64px_repeat(10,minmax(0,1fr))] gap-px text-center text-3xs text-app-muted">
              <div />
              {SLOT_LABELS.map((l, i) => (
                <div key={i} className="truncate px-1 py-1">
                  <div className="font-mono text-app-dim">slot {i}</div>
                  <div>{l}</div>
                </div>
              ))}
            </div>

            {a.rows.length === 0 ? (
              <div className="py-10 text-center text-xs text-app-muted">
                ยังไม่มีข้อมูล — ต้องเห็น beacon จาก MainNode ก่อน (เชื่อมต่อ USB หรือเปิด Demo)
              </div>
            ) : (
              <div className="space-y-px">
                {a.rows.map((row) => {
                  const bySlot: Arrival[][] = Array.from({ length: TDMA_SLOTS }, () => []);
                  row.arrivals.forEach((x) => bySlot[x.slot].push(x));
                  return (
                    <div key={row.cycle} className="grid grid-cols-[64px_repeat(10,minmax(0,1fr))] gap-px">
                      <div className="flex items-center gap-1 pr-1 font-mono text-3xs text-app-dim">
                        <span className={`h-1.5 w-1.5 rounded-full ${row.beacon ? "bg-violet-400" : "bg-rose-500"}`}
                              title={row.beacon ? "เห็น beacon ของรอบนี้" : "ไม่เห็น beacon ของรอบนี้"} />
                        #{row.cycle}
                      </div>
                      {bySlot.map((list, slot) => {
                        const conflict = new Set(list.map((x) => x.key)).size > 1;
                        const wrong = list.some((x) => x.expected !== undefined && x.expected !== slot);
                        return (
                          <div key={slot}
                            className={`relative h-6 bg-app-input ${conflict ? "ring-1 ring-inset ring-rose-500" : wrong ? "ring-1 ring-inset ring-amber-500" : ""}`}>
                            {slot === 0 && row.beacon && (
                              <div className="absolute inset-y-1 left-0 rounded-sm opacity-80"
                                   style={{ width: "18%", backgroundColor: KIND_META.beacon.color }} />
                            )}
                            {list.map((x, i) => (
                              <div key={i}
                                onMouseEnter={() => setHover(x)} onMouseLeave={() => setHover(null)}
                                className="absolute inset-y-1 cursor-default rounded-sm"
                                style={{
                                  left: `${(x.startInSlotMs / TDMA_SLOT_MS) * 100}%`,
                                  width: `max(4px, ${(x.airMs / TDMA_SLOT_MS) * 100}%)`,
                                  backgroundColor: KIND_META[x.kind].color,
                                  outline: x.expected !== undefined && x.expected !== slot ? "1px solid #f59e0b" : undefined,
                                }}
                                title={`${x.label} · slot ${x.slot} @ ${Math.round(x.startInSlotMs)} ms · ${Math.round(x.airMs)} ms on air${x.expected !== undefined ? ` · ควรอยู่ slot ${x.expected}` : ""}`}
                              />
                            ))}
                          </div>
                        );
                      })}
                    </div>
                  );
                })}
              </div>
            )}

            {/* Legend */}
            <div className="mt-3 flex flex-wrap items-center gap-4 border-t border-app-divider pt-2 text-2xs text-app-muted">
              {(["beacon", "band", "sos", "pin", "assignment", "found", "ring"] as const).map((k) => (
                <span key={k} className="flex items-center gap-1.5">
                  <span className="h-2 w-3 rounded-sm" style={{ backgroundColor: KIND_META[k].color }} />{KIND_META[k].label}
                </span>
              ))}
              <span className="flex items-center gap-1.5"><span className="h-2.5 w-3 ring-1 ring-amber-500" />ผิด slot</span>
              <span className="flex items-center gap-1.5"><span className="h-2.5 w-3 ring-1 ring-rose-500" />2 เครื่องใน slot เดียว</span>
              <span>ความกว้างแท่ง = เวลาส่งจริง (SF{LORA.sf})</span>
            </div>
            <div className="mt-1 h-4 text-2xs text-app-dim">
              {hover && `${hover.label} · รอบ #${hover.cycle} · slot ${hover.slot} เริ่มที่ ${Math.round(hover.startInSlotMs)} ms · ใช้เวลาส่ง ${Math.round(hover.airMs)} ms${hover.expected !== undefined ? ` · ควรอยู่ slot ${hover.expected}` : ""}`}
            </div>
          </div>
        </div>
      </div>

      <div className="grid gap-3 lg:grid-cols-2">
        {/* Issues */}
        <div className="panel">
          <div className="panel-header justify-between">
            <span>สิ่งที่ตรวจพบ</span>
            <span className="font-mono text-2xs text-app-dim">beacon {a.beacons} · หาย {a.missedBeacons}</span>
          </div>
          <ul className="space-y-1.5 p-3 text-xs">
            {a.issues.length === 0 && <li className="text-app-muted">ยังไม่มีข้อมูลพอให้วิเคราะห์</li>}
            {a.issues.map((iss, i) => (
              <li key={i} className={`flex gap-2 rounded border px-2 py-1.5 ${SEVERITY_STYLE[iss.severity].cls}`}>
                <span>{SEVERITY_STYLE[iss.severity].icon}</span>
                <span>{iss.message}</span>
              </li>
            ))}
          </ul>
        </div>

        {/* Per-device slot usage */}
        <div className="panel">
          <div className="panel-header"><span>Slot ที่แต่ละอุปกรณ์ใช้ ({a.rows.length} รอบล่าสุด)</span></div>
          <table className="w-full text-left text-2xs">
            <thead className="text-app-muted">
              <tr className="border-b border-app-divider">
                <th className="px-3 py-1.5 font-normal">อุปกรณ์</th>
                <th className="px-3 py-1.5 font-normal">ควรอยู่</th>
                <th className="px-3 py-1.5 font-normal">ใช้จริง (slot×ครั้ง)</th>
                <th className="px-3 py-1.5 text-right font-normal">ผิด slot</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-app-divider font-mono">
              {a.devices.length === 0 && (
                <tr><td colSpan={4} className="px-3 py-4 text-center font-sans text-app-muted">ยังไม่มีข้อมูล</td></tr>
              )}
              {a.devices.map((d) => (
                <tr key={d.key}>
                  <td className="px-3 py-1.5 font-sans">
                    <span className="mr-1.5 inline-block h-2 w-2 rounded-full" style={{ backgroundColor: KIND_META[d.kind].color }} />
                    <span className="text-app-text">{d.label}</span>
                  </td>
                  <td className="px-3 py-1.5 text-app-dim">{d.expected ?? (d.kind === "band" ? "1 หรือ 2" : "-")}</td>
                  <td className="px-3 py-1.5 text-app-text">{slotsLabel(d.slots)}</td>
                  <td className={`px-3 py-1.5 text-right ${d.outOfSlot ? "text-amber-400" : "text-app-muted"}`}>{d.outOfSlot || "-"}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <p className="border-t border-app-divider px-3 py-2 text-3xs text-app-muted">
            เวลาวัดจาก timestamp ของ MainNode เทียบกับ beacon แล้วหักเวลาส่ง (airtime) ออก จึงคลาดได้ราว ±10 ms
            · กำไลใน firmware ปัจจุบันปิดวิทยุเพื่อประหยัดไฟ จึงส่งตามนาฬิกาตัวเองแทนการล็อก slot — ถ้าเห็นว่าไม่ล็อก slot แปลว่าเป็นไปตามที่ firmware ทำ
          </p>
        </div>
      </div>
    </div>
  );
}
