// ============================================================================
// Band position from pillar RSSI.
//
// 1. Effective RSSI: the SX127x packet RSSI stops falling at the noise floor
//    (~-120 dBm at SF9), so a weak packet reads -120 whether it is 300 m or
//    1 km away. Below the floor the SNR goes negative and carries the rest of
//    the information, so signal power ~= RSSI + SNR when SNR < 0.
// 2. Smoothing: one packet's RSSI swings +-5 dB (fading, body blocking), which
//    is +-60 % in distance. Each pillar keeps its last few readings and the
//    median is used.
// 3. Position: pillar positions averaged with weight 1/d^2. Simulated against
//    4 pillars 60 m apart with 4 dB RSSI noise and an uncalibrated model, this
//    beat both the old 1/d single-packet average (inside 8 m vs 11 m mean
//    error, outside 18 m vs 27 m) and least-squares trilateration, which
//    chases the noise and throws points far outside (p90 54 m inside).
// ============================================================================

import type { LatLng, PathLossModel } from "./types";

export const RSSI_SAMPLES = 5;            // readings kept per pillar
export const RSSI_SAMPLE_MAX_AGE_MS = 60_000;

export function effectiveRssi(rssi: number, snr: number): number {
  return snr < 0 ? rssi + snr : rssi;
}

export function median(xs: number[]): number {
  if (xs.length === 0) return NaN;
  const s = [...xs].sort((a, b) => a - b);
  const m = s.length >> 1;
  return s.length % 2 ? s[m]! : (s[m - 1]! + s[m]!) / 2;
}

export function pathLossDistanceM(rssi: number, m: PathLossModel): number {
  return Math.pow(10, (m.rssiAt1m - rssi) / (10 * m.pathLossN));
}

export function metersBetween(a: LatLng, b: LatLng): number {
  const R = 6371000, toRad = Math.PI / 180;
  const dLat = (b.lat - a.lat) * toRad, dLng = (b.lng - a.lng) * toRad;
  const h = Math.sin(dLat / 2) ** 2 + Math.cos(a.lat * toRad) * Math.cos(b.lat * toRad) * Math.sin(dLng / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(h));
}

export interface RangeReading {
  anchor: LatLng;
  distanceM: number;     // from the path-loss model
}

export function estimatePosition(readings: RangeReading[]): LatLng | undefined {
  let sw = 0, lat = 0, lng = 0;
  for (const r of readings) {
    const d = Math.min(3000, Math.max(1, r.distanceM));
    const w = 1 / (d * d);
    sw += w; lat += r.anchor.lat * w; lng += r.anchor.lng * w;
  }
  return sw > 0 ? { lat: lat / sw, lng: lng / sw } : undefined;
}
