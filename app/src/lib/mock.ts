// ============================================================================
// Mock event generator for UI testing without real MainNode hardware.
// Mirrors the JSON event shapes that MainNode emits over USB Serial so it
// can drive the same store handlers (upsertSightingFromBand, applyPinSighting).
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

export function startMockStream() {
  if (tickHandle) return;
  tickHandle = setInterval(() => {
    const { wristbands, anchors, upsertSightingFromBand, applyPinSighting, sightings } = useResQ.getState();
    const ids = Object.keys(wristbands);
    if (ids.length === 0) return;

    const target = pick(ids);
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

    upsertSightingFromBand({
      id: target,
      ptype: triage === "red" || triage === "black" ? "SOS_FALL" : "HEARTBEAT",
      seq: Math.floor(Math.random() * 1000),
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
    for (const anchor of anchorList.slice(0, 4)) {
      applyPinSighting({
        pin: anchor.pinIndex,
        pin_id: anchor.id,
        sightings: [{
          band: target,
          rssi: Math.round(-60 - Math.random() * 30),
          snr: Math.round(5 + Math.random() * 8),
          age_ms: Math.round(Math.random() * 1000),
        }],
        ts: Date.now(),
      });
    }
    // Tiny position jitter for the active sighting via anchor proximity
    if (anchorList[0] && prev) {
      const useResQState = useResQ.getState();
      const existing = useResQState.sightings[target];
      if (existing) {
        useResQState.sightings[target] = {
          ...existing,
          position: { lat: jitter(anchorList[0].position.lat, 0.0015), lng: jitter(anchorList[0].position.lng, 0.0015) },
        };
      }
    }
  }, 2000);
}

export function stopMockStream() {
  if (tickHandle) {
    clearInterval(tickHandle);
    tickHandle = null;
  }
}
