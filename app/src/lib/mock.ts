import type { LatLng, Sighting, TriageLevel } from "./types";
import { useResQ } from "./store";

export const DEFAULT_CENTER: LatLng = { lat: 13.7563, lng: 100.5018 };

function jitter(base: number, radius = 0.0008) {
  return base + (Math.random() - 0.5) * 2 * radius;
}

function pick<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)]!;
}

const TRIAGE_POOL: TriageLevel[] = ["green", "green", "green", "yellow", "yellow", "red", "black"];

let tickHandle: ReturnType<typeof setInterval> | null = null;

export function startMockStream() {
  if (tickHandle) return;
  tickHandle = setInterval(() => {
    const { wristbands, anchors, upsertSighting, sightings } = useResQ.getState();
    const ids = Object.keys(wristbands);
    if (ids.length === 0) return;

    const target = pick(ids);
    const prev = sightings[target];
    const triage = prev?.status === "rescued" ? prev.triage : pick(TRIAGE_POOL);

    const baseHR = triage === "black" ? 0 : triage === "red" ? 38 + Math.random() * 8 : triage === "yellow" ? 110 + Math.random() * 20 : 70 + Math.random() * 25;
    const baseSpO2 = triage === "black" ? 0 : triage === "red" ? 80 + Math.random() * 5 : triage === "yellow" ? 90 + Math.random() * 4 : 96 + Math.random() * 3;

    const rssiPerAnchor: Record<string, number> = {};
    for (const a of Object.values(anchors)) {
      rssiPerAnchor[a.id] = -55 - Math.random() * 40;
    }

    const positionAnchor = Object.values(anchors)[0];
    const position = positionAnchor
      ? { lat: jitter(positionAnchor.position.lat, 0.0015), lng: jitter(positionAnchor.position.lng, 0.0015) }
      : { lat: jitter(DEFAULT_CENTER.lat), lng: jitter(DEFAULT_CENTER.lng) };

    const next: Sighting = {
      wristbandId: target,
      lastSeen: Date.now(),
      position,
      rssiPerAnchor,
      triage,
      heartRate: Math.round(baseHR),
      spo2: Math.round(baseSpO2),
      batteryPct: Math.max(5, (prev?.batteryPct ?? 100) - (Math.random() < 0.1 ? 1 : 0)),
      lastGForce: prev?.lastGForce ?? Math.round(Math.random() * 30),
      hopCount: Math.floor(Math.random() * 3),
      status: prev?.status === "rescued" ? "rescued" : triage === "red" || triage === "black" ? "sos" : "active",
    };

    upsertSighting(next);
  }, 2000);
}

export function stopMockStream() {
  if (tickHandle) {
    clearInterval(tickHandle);
    tickHandle = null;
  }
}
