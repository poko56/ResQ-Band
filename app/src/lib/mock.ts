// ============================================================================
// Demo stream for testing the dashboard without hardware.
//
// Emits the same JSON events MainNode sends over USB (beacon, band,
// pin_sighting, stats) through applyHubEvent, on the real TDMA schedule:
//   slot 0  beacon        slot 1..2  bands #1/#2 (phase-locked)
//   slot 3..6  pillars 1-4 report what they heard last cycle
// Extra bands beyond two free-run and drift across slots, the way the real
// duty-cycled band firmware does, so the TDMA view has something to flag.
// Signal strength follows a log-distance path-loss model from each band's
// simulated position, so coverage, positions and range fits look plausible.
// ============================================================================

import type { HubEvent, LatLng, TriageLevel } from "./types";
import { useResQ } from "./store";
import { applyHubEvent } from "./hubEvents";
import { airtimeMs, LORA, PAYLOAD_BYTES, SLOT_TX_OFFSET_MS, TDMA_SLOTS, TDMA_SLOT_MS } from "./lora";

export const DEFAULT_CENTER: LatLng = { lat: 13.7563, lng: 100.5018 };

function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)]!;
}
function gauss(sd: number): number {
  // Box-Muller
  const u = 1 - Math.random(), v = Math.random();
  return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v) * sd;
}
function metersBetween(a: LatLng, b: LatLng): number {
  const R = 6371000, toRad = Math.PI / 180;
  const dLat = (b.lat - a.lat) * toRad, dLng = (b.lng - a.lng) * toRad;
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(a.lat * toRad) * Math.cos(b.lat * toRad) * Math.sin(dLng / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(h));
}

// "True" channel the demo simulates (the range test should recover ~these).
const SIM_RSSI_1M = -42;
const SIM_PATH_N = 2.8;
function simRssi(distanceM: number): number {
  return Math.round(SIM_RSSI_1M - 10 * SIM_PATH_N * Math.log10(Math.max(distanceM, 0.5)) + gauss(3));
}
function simSnr(rssi: number): number {
  return Math.round(Math.max(-15, Math.min(12, (rssi + 120) / 3 + gauss(1.5))));
}

const TRIAGE_POOL: TriageLevel[] = ["green", "green", "green", "yellow", "yellow", "red", "black"];
const TRIAGE_BYTE: Record<TriageLevel, number> = { green: 0, yellow: 1, red: 2, black: 3 };

let slotHandle: ReturnType<typeof setInterval> | null = null;
let mockStart = 0;
let slot = 0;
let cycle = 0;
let rx = 0;
let runts = 0;
const seqs: Record<string, number> = {};
const bandPos: Record<string, LatLng> = {};
// What each pillar heard during the current cycle (reported next cycle).
const heard: Record<number, Record<string, { rssi: number; snr: number; at: number }>> = {};

const mainMillis = () => Date.now() - mockStart;
const emit = (ev: HubEvent) => applyHubEvent(ev);

function bandPosition(id: string, center: LatLng): LatLng {
  if (!bandPos[id]) {
    const r = 15 + Math.random() * 120, a = Math.random() * 2 * Math.PI;
    bandPos[id] = {
      lat: center.lat + (r * Math.cos(a)) / 111_320,
      lng: center.lng + (r * Math.sin(a)) / (111_320 * Math.cos((center.lat * Math.PI) / 180)),
    };
  }
  return bandPos[id];
}

function anchorCenter(): LatLng {
  const placed = Object.values(useResQ.getState().anchors).filter((a) => a.position);
  if (placed.length === 0) return DEFAULT_CENTER;
  return {
    lat: placed.reduce((s, a) => s + a.position!.lat, 0) / placed.length,
    lng: placed.reduce((s, a) => s + a.position!.lng, 0) / placed.length,
  };
}

function sendHeartbeat(id: string, rxOffsetMs: number) {
  const { sightings, anchors } = useResQ.getState();
  const prev = sightings[id];
  const triage = prev?.status === "rescued" ? prev.triage : pick(TRIAGE_POOL);
  const hr = triage === "black" ? 0 : triage === "red" ? 38 + Math.random() * 8 : triage === "yellow" ? 110 + Math.random() * 20 : 70 + Math.random() * 25;
  const spo2 = triage === "black" ? 0 : triage === "red" ? 80 + Math.random() * 5 : triage === "yellow" ? 90 + Math.random() * 4 : 96 + Math.random() * 3;

  // Occasional lost heartbeats show up as a sequence gap.
  let seq = (seqs[id] ?? 0) + 1;
  if (Math.random() < 0.08) seq += 1 + Math.floor(Math.random() * 2);
  seqs[id] = seq;

  const center = anchorCenter();
  const pos = bandPosition(id, center);

  // Pillars that can hear this band log it for their next report.
  for (const a of Object.values(anchors)) {
    if (!a.position || a.pinIndex > 3) continue;
    const r = simRssi(metersBetween(a.position, pos));
    if (r < LORA.sensitivityDbm) continue;
    (heard[a.pinIndex] ??= {})[id] = { rssi: r, snr: simSnr(r), at: Date.now() };
  }

  // MainNode itself sits at the centre of the pillars.
  const direct = simRssi(metersBetween(center, pos) + 20);
  if (direct < LORA.sensitivityDbm) return;   // out of range of the hub
  rx++;
  emit({
    t: "band",
    id,
    ptype: triage === "red" ? "SOS_VITALS" : "HEARTBEAT",   // firmware only raises SOS on a real, critical pulse
    seq,
    triage: TRIAGE_BYTE[triage],
    hr: Math.round(hr),
    spo2: Math.round(spo2),
    batt: 0,
    g_x10: 0,
    rssi: direct,
    snr: simSnr(direct),
    ts: mainMillis() + rxOffsetMs,
  } as HubEvent);
}

function slotTick() {
  const { wristbands, anchors } = useResQ.getState();
  const ids = Object.keys(wristbands).sort();
  const slotStart = mainMillis();
  const sosAir = Math.round(airtimeMs(PAYLOAD_BYTES.SOS));

  if (slot === 0) {
    cycle++;
    emit({ t: "beacon", cycle, flags: 0, ts: slotStart + Math.round(airtimeMs(PAYLOAD_BYTES.BEACON)) } as HubEvent);
  }

  // Bands #1 and #2 are phase-locked to their slots.
  if (slot === 1 && ids[0]) sendHeartbeat(ids[0], SLOT_TX_OFFSET_MS + sosAir + Math.round(Math.random() * 20));
  if (slot === 2 && ids[1]) sendHeartbeat(ids[1], SLOT_TX_OFFSET_MS + sosAir + Math.round(Math.random() * 20));

  // Any further bands free-run: their slot creeps forward every 3 cycles.
  ids.slice(2).forEach((id, k) => {
    const s = (1 + Math.floor(cycle / 3) + k * 3) % TDMA_SLOTS;
    if (s === slot) sendHeartbeat(id, Math.round(Math.random() * (TDMA_SLOT_MS - sosAir)) + sosAir);
  });

  // Pillar reports in slots 3..6. Pillar 3 goes dark for 40 s every 2 min so
  // the timelines have an outage to show.
  if (slot >= 3 && slot <= 6) {
    const pinIndex = slot - 3;
    const anchor = Object.values(anchors).find((a) => a.pinIndex === pinIndex);
    const outage = pinIndex === 2 && (Date.now() / 1000) % 120 < 40;
    if (anchor && !outage) {
      const center = anchorCenter();
      const up = anchor.position ? simRssi(metersBetween(anchor.position, center) + 20) : -85;
      const list = Object.entries(heard[pinIndex] ?? {})
        .filter(([, h]) => Date.now() - h.at < 15_000)
        .slice(0, 4)
        .map(([band, h]) => ({ band, rssi: h.rssi, snr: h.snr, age_ms: Date.now() - h.at }));
      heard[pinIndex] = {};
      if (up >= LORA.sensitivityDbm) {
        rx++;
        emit({
          t: "pin_sighting",
          pin: pinIndex,
          pin_id: anchor.id,
          sightings: list,
          rssi: up,
          snr: simSnr(up),
          ts: slotStart + SLOT_TX_OFFSET_MS + Math.round(airtimeMs(PAYLOAD_BYTES.PIN_SIGHTING)),
        } as HubEvent);
      }
    }
  }

  // MainNode stats once a second, like the firmware.
  if (Math.random() < 0.02) runts++;
  emit({
    t: "stats", cycle, uptime_s: Math.floor(slotStart / 1000), rx, dropped: runts,
    bands: ids.length, heap: 184000 + Math.round(Math.random() * 4000), ts: slotStart,
  } as HubEvent);

  slot = (slot + 1) % TDMA_SLOTS;
}

export function startMockStream() {
  if (slotHandle) return;
  mockStart = Date.now();
  slot = 0;
  slotHandle = setInterval(slotTick, TDMA_SLOT_MS);
  slotTick();
}

export function stopMockStream() {
  if (slotHandle) {
    clearInterval(slotHandle);
    slotHandle = null;
  }
}
