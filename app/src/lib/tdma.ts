// ============================================================================
// TDMA analysis for Dev Tools.
//
// Every MainNode event carries MainNode's millis() at RX (or right after TX
// for the beacon). Measuring each packet against the last beacon places it in
// a slot of the 10 x 1 s cycle; from that we flag devices transmitting outside
// their slot, two devices sharing a slot, overlapping transmissions (likely
// collisions) and missing beacons.
// ============================================================================

import type { PacketLogEntry, Wristband } from "./types";
import { describePacket, type PacketKind } from "./packetFormat";
import { airtimeMs, expectedSlot, PAYLOAD_BYTES, TDMA_CYCLE_MS, TDMA_SLOT_MS, TDMA_SLOTS } from "./lora";

export interface Arrival {
  key: string;            // device key
  label: string;
  kind: PacketKind;
  cycle: number;
  slot: number;
  startInSlotMs: number;  // estimated TX start within the slot
  airMs: number;
  expected?: number;
  rxAt: number;
}

export interface CycleRow {
  cycle: number;
  beacon: boolean;        // a beacon was actually seen for this cycle
  arrivals: Arrival[];
}

export type Severity = "error" | "warn" | "info";
export interface TdmaIssue {
  severity: Severity;
  key: string;
  message: string;
}

export interface DeviceSlotSummary {
  key: string;
  label: string;
  kind: PacketKind;
  packets: number;
  slots: Record<number, number>;   // slot -> count
  expected?: number;
  outOfSlot: number;
}

export interface TdmaAnalysis {
  beacons: number;
  missedBeacons: number;
  unsynced: number;               // packets seen before any beacon
  rows: CycleRow[];               // newest first
  devices: DeviceSlotSummary[];
  issues: TdmaIssue[];
}

const AIR = {
  beacon: airtimeMs(PAYLOAD_BYTES.BEACON),
  sos: airtimeMs(PAYLOAD_BYTES.SOS),
  pin: airtimeMs(PAYLOAD_BYTES.PIN_SIGHTING),
  assignment: airtimeMs(PAYLOAD_BYTES.ASSIGNMENT),
  found: airtimeMs(PAYLOAD_BYTES.FOUND),
  ringAck: airtimeMs(PAYLOAD_BYTES.RING_ACK),
};

function airFor(kind: PacketKind): number {
  switch (kind) {
    case "band": case "sos": return AIR.sos;
    case "pin": return AIR.pin;
    case "assignment": return AIR.assignment;
    case "found": return AIR.found;
    case "ring": return AIR.ringAck;
    default: return 0;
  }
}

const RADIO_KINDS = new Set<PacketKind>(["band", "sos", "pin", "assignment", "found", "ring"]);

export function analyzeTdma(log: PacketLogEntry[], wristbands: Record<string, Wristband>, keepCycles = 12): TdmaAnalysis {
  let beacon: { cycle: number; start: number; ts: number } | null = null;
  let beacons = 0, missedBeacons = 0, unsynced = 0;
  const rowsByCycle = new Map<number, CycleRow>();
  const allArrivals: Arrival[] = [];

  for (const e of log) {
    const ev = e.ev as { t: string; ts?: number; cycle?: number };
    if (typeof ev.ts !== "number") continue;

    if (ev.t === "beacon") {
      // A timestamp going backwards means MainNode rebooted: start over.
      if (beacon && ev.ts < beacon.ts) beacon = null;
      if (beacon && ev.ts - beacon.ts > TDMA_CYCLE_MS * 1.5) {
        missedBeacons += Math.round((ev.ts - beacon.ts) / TDMA_CYCLE_MS) - 1;
      }
      beacon = { cycle: ev.cycle ?? 0, start: ev.ts - AIR.beacon, ts: ev.ts };
      beacons++;
      const row = rowsByCycle.get(beacon.cycle) ?? { cycle: beacon.cycle, beacon: true, arrivals: [] };
      row.beacon = true;
      rowsByCycle.set(beacon.cycle, row);
      continue;
    }

    const info = describePacket(e.ev, wristbands);
    if (!RADIO_KINDS.has(info.kind) || !info.deviceKey) continue;
    if (!beacon || ev.ts < beacon.start) { unsynced++; continue; }

    const air = airFor(info.kind);
    // MainNode timestamps the end of reception (or its own TX), so step back
    // by the airtime to find when the transmission started.
    const isOwnTx = info.kind === "assignment";
    const startOffset = isOwnTx ? ev.ts - beacon.start : ev.ts - air - beacon.start;
    if (startOffset < 0) { unsynced++; continue; }
    const cycle = beacon.cycle + Math.floor(startOffset / TDMA_CYCLE_MS);
    const inCycle = startOffset % TDMA_CYCLE_MS;
    const slot = Math.floor(inCycle / TDMA_SLOT_MS);

    let expected: number | undefined;
    if (info.kind === "pin") expected = expectedSlot("pin", Number(info.deviceKey.slice(4)));
    else if (info.kind === "assignment") expected = expectedSlot("main_cmd");
    else if (info.kind === "found") expected = expectedSlot("resq");
    else if (info.kind === "sos") expected = undefined;   // emergencies may use any slot (ALOHA)

    const arrival: Arrival = {
      key: info.deviceKey, label: info.source, kind: info.kind, cycle, slot,
      startInSlotMs: inCycle - slot * TDMA_SLOT_MS, airMs: air, expected, rxAt: e.rxAt,
    };
    allArrivals.push(arrival);
    const row = rowsByCycle.get(cycle) ?? { cycle, beacon: false, arrivals: [] };
    row.arrivals.push(arrival);
    rowsByCycle.set(cycle, row);
  }

  const rows = [...rowsByCycle.values()].sort((a, b) => b.cycle - a.cycle).slice(0, keepCycles);
  const recentCycles = new Set(rows.map((r) => r.cycle));
  const recent = allArrivals.filter((a) => recentCycles.has(a.cycle));

  // ---- per-device summary ----
  const devMap = new Map<string, DeviceSlotSummary>();
  for (const a of recent) {
    const d = devMap.get(a.key) ?? { key: a.key, label: a.label, kind: a.kind === "sos" ? "band" : a.kind, packets: 0, slots: {}, expected: a.expected, outOfSlot: 0 };
    d.packets++;
    d.slots[a.slot] = (d.slots[a.slot] ?? 0) + 1;
    if (a.expected !== undefined && a.slot !== a.expected) d.outOfSlot++;
    devMap.set(a.key, d);
  }
  const devices = [...devMap.values()].sort((a, b) => a.key.localeCompare(b.key, undefined, { numeric: true }));

  // ---- issues ----
  const issues: TdmaIssue[] = [];
  if (beacons === 0) {
    issues.push({ severity: "error", key: "hub", message: "ยังไม่เห็น beacon จาก MainNode เลย — อุปกรณ์อื่นซิงก์ TDMA ไม่ได้" });
  } else if (missedBeacons > 0) {
    issues.push({ severity: "warn", key: "hub", message: `beacon หายไป ${missedBeacons} รอบ (MainNode ไม่ได้ส่ง หรือ log ขาดช่วง)` });
  }

  for (const d of devices) {
    if (d.kind === "pin" && d.outOfSlot > 0) {
      issues.push({ severity: "error", key: d.key, message: `${d.label} ส่งนอก slot ${d.expected} ไป ${d.outOfSlot}/${d.packets} ครั้ง — ยังไม่ซิงก์กับ beacon หรือนาฬิกาเพี้ยน` });
    }
    if (d.kind === "assignment" && d.outOfSlot > 0) {
      issues.push({ severity: "warn", key: d.key, message: `MainNode ส่งคำสั่ง ASSIGNMENT นอก slot 7 ไป ${d.outOfSlot}/${d.packets} ครั้ง (firmware ส่งทุก 2 วินาทีโดยไม่รอ slot)` });
    }
    if (d.kind === "band") {
      const used = Object.keys(d.slots).map(Number);
      if (used.length >= 3) {
        issues.push({ severity: "warn", key: d.key, message: `${d.label} ไม่ล็อก slot — ใช้ ${used.length} slot ต่างกัน (${used.sort((a, b) => a - b).join(", ")}) ใน ${d.packets} heartbeat` });
      }
      const inPinSlots = used.filter((s) => s >= 3 && s <= 6);
      if (inPinSlots.length > 0) {
        issues.push({ severity: "warn", key: d.key, message: `${d.label} ส่งทับ slot ของเสา (${inPinSlots.map((s) => `เสา ${s - 2}`).join(", ")}) — เสี่ยงชนกับรายงานของเสา` });
      }
    }
  }

  // Two devices in the same slot of the same cycle, and actual time overlap.
  let shared = 0, overlaps = 0;
  const overlapPairs = new Map<string, number>();
  for (const r of rows) {
    const bySlot = new Map<number, Arrival[]>();
    for (const a of r.arrivals) bySlot.set(a.slot, [...(bySlot.get(a.slot) ?? []), a]);
    for (const [, list] of bySlot) {
      const keys = new Set(list.map((a) => a.key));
      if (keys.size > 1) shared++;
    }
    const sorted = [...r.arrivals].sort((a, b) => (a.slot * TDMA_SLOT_MS + a.startInSlotMs) - (b.slot * TDMA_SLOT_MS + b.startInSlotMs));
    for (let i = 1; i < sorted.length; i++) {
      const p = sorted[i - 1], c = sorted[i];
      if (p.key === c.key) continue;
      const pEnd = p.slot * TDMA_SLOT_MS + p.startInSlotMs + p.airMs;
      const cStart = c.slot * TDMA_SLOT_MS + c.startInSlotMs;
      if (cStart < pEnd) {
        overlaps++;
        const k = [p.label, c.label].sort().join(" ↔ ");
        overlapPairs.set(k, (overlapPairs.get(k) ?? 0) + 1);
      }
    }
  }
  for (const [pair, n] of overlapPairs) {
    issues.push({ severity: "error", key: pair, message: `${pair} ส่งเวลาทับกัน ${n} ครั้ง — ได้ยินทั้งคู่ถือว่าโชคดี ถ้าแรงพอ ๆ กันจะชนจนหายทั้งคู่` });
  }
  if (shared > 0 && overlaps === 0) {
    issues.push({ severity: "info", key: "slots", message: `มี ${shared} ครั้งที่ 2 อุปกรณ์ใช้ slot เดียวกันในรอบเดียวกัน แต่เวลาส่งยังไม่ทับกัน` });
  }
  if (unsynced > 0 && beacons > 0) {
    issues.push({ severity: "info", key: "unsynced", message: `${unsynced} packet มาถึงก่อน beacon แรกที่บันทึกไว้ จึงจัด slot ไม่ได้` });
  }
  if (issues.length === 0 && recent.length > 0) {
    issues.push({ severity: "info", key: "ok", message: "ทุกอุปกรณ์ส่งตรง slot ไม่มีการทับกันในช่วงที่ดู" });
  }

  return { beacons, missedBeacons, unsynced, rows, devices, issues };
}

export { TDMA_SLOTS };
