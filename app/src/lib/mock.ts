// ============================================================================
// Mock event generator for UI testing without real MainNode hardware.
// Mirrors the JSON event shapes that MainNode emits over USB Serial so it
// can drive the same store handlers (upsertSightingFromBand, applyPinSighting, recordHubStats).
// ============================================================================

import type { LatLng, TriageLevel } from "./types";
import { useResQ } from "./store";

export const DEFAULT_CENTER: LatLng = { lat: 13.7563, lng: 100.5018 };

function jitter(base: number, radius = 0.0008) {
  return base + (Math.random() - 0.5) * 2 * radius;
}

function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)]!;
}

const TRIAGE_POOL: TriageLevel[] = ["green", "green", "green", "yellow", "yellow", "red", "black"];
const TRIAGE_BYTE: Record<TriageLevel, number> = { green: 0, yellow: 1, red: 2, black: 3 };

let tickHandle: ReturnType<typeof setInterval> | null = null;
let statsHandle: ReturnType<typeof setInterval> | null = null;
const mockSeqs: Record<string, number> = {};
let mockRx = 0;
let mockRunts = 0;      // frames MainNode itself throws away (short / bad CRC)
let mockTurn = 0;
let mockCycle = 0;

export function startMockStream() {
  if (tickHandle) return;

  tickHandle = setInterval(() => {
    const { wristbands, anchors, upsertSightingFromBand, applyPinSighting, sightings, incHubPackets } = useResQ.getState();
    const ids = Object.keys(wristbands);
    if (ids.length === 0) return;

    // Round-robin so every band reports regularly, like the real 10 s heartbeat.
    const target = ids[mockTurn++ % ids.length]!;
    const prev = sightings[target];
    const triage = prev?.status === "rescued" ? prev.triage : pick(TRIAGE_POOL);

    const baseHR =
      triage === "black" ? 0 :
      triage === "red"   ? 38 + Math.random() * 8 :
      triage === "yellow" ? 110 + Math.random() * 20 :
      70 + Math.random() * 25;
    const baseSpO2 =
      triage === "black" ? 0 :
      triage === "red"   ? 80 + Math.random() * 5 :
      triage === "yellow" ? 90 + Math.random() * 4 :
      96 + Math.random() * 3;

    // Monotonic sequence numbering with occasional packet drop (8% chance)
    let seq = (mockSeqs[target] ?? 0) + 1;
    if (Math.random() < 0.08) {
      const dropCount = Math.floor(1 + Math.random() * 2);
      seq += dropCount;
    }
    mockSeqs[target] = seq;
    mockRx++;
    incHubPackets();

    upsertSightingFromBand({
      id: target,
      ptype: triage === "red" || triage === "black" ? "SOS_FALL" : "HEARTBEAT",
      seq,
      triage: TRIAGE_BYTE[triage],
      hr: Math.round(baseHR),
      spo2: Math.round(baseSpO2),
      batt: Math.max(5, Math.round(prev?.batteryPct ?? 100) - (Math.random() < 0.1 ? 1 : 0)),
      g_x10: triage === "red" ? Math.round(40 + Math.random() * 40) : Math.round(Math.random() * 20),
      rssi: -60 - Math.round(Math.random() * 30),
      snr: Math.round(5 + Math.random() * 8),
      ts: Date.now(),
    });

    // Fake pin sightings from each placed anchor so the map can infer positions.
    const anchorList = Object.values(anchors);
    // Pillar 3 goes dark for 40 s out of every 2 min so Dev Tools has an
    // outage to show.
    const pin3Outage = (Date.now() / 1000) % 120 < 40;
    for (const anchor of anchorList.slice(0, 4)) {
      if (anchor.pinIndex === 2 && pin3Outage) continue;
      applyPinSighting({
        pin: anchor.pinIndex,
        pin_id: anchor.id,
        sightings: [{
          band: target,
          rssi: Math.round(-65 - Math.random() * 25),
          snr: Math.round(4 + Math.random() * 8),
          age_ms: Math.round(Math.random() * 500),
        }],
        ts: Date.now(),
        rssi: Math.round(-70 - Math.random() * 20),
        snr: Math.round(6 + Math.random() * 6),
      });
    }

    // Tiny position jitter for the active sighting via anchor proximity
    if (anchorList[0] && prev) {
      const useResQState = useResQ.getState();
      const existing = useResQState.sightings[target];
      if (existing) {
        useResQState.sightings[target] = {
          ...existing,
          position: anchorList[0]?.position ? { lat: jitter(anchorList[0].position.lat, 0.0015), lng: jitter(anchorList[0].position.lng, 0.0015) } : undefined,
        };
      }
    }
  }, 2000);

  // Periodically emit hub stats so Dev Diagnostics graphs live performance
  if (!statsHandle) {
    statsHandle = setInterval(() => {
      mockCycle++;
      if (Math.random() < 0.05) mockRunts++;
      const { recordHubStats, setHubInfo } = useResQ.getState();
      setHubInfo({ cycleId: mockCycle, loraPacketsReceived: mockRx, loraPacketsDropped: mockRunts });
      recordHubStats({
        rx: mockRx,
        dropped: mockRunts,
        uptimeS: mockCycle * 3,
        cycle: mockCycle,
        heap: 184000 + Math.round(Math.random() * 4000),
      });
    }, 3000);
  }
}

export function stopMockStream() {
  if (tickHandle) {
    clearInterval(tickHandle);
    tickHandle = null;
  }
  if (statsHandle) {
    clearInterval(statsHandle);
    statsHandle = null;
  }
}
