// ============================================================================
// Log-distance path-loss fit for the range test.
//   rssi(d) = rssiAt1m - 10 * n * log10(d)
// Least squares on (log10 d, rssi). Needs at least two distinct distances.
// ============================================================================

import type { PacketLogEntry, PathLossModel, RangeTestPoint } from "./types";

export interface PathLossFit extends PathLossModel {
  r2: number;
  points: number;
}

export function fitPathLoss(points: Pick<RangeTestPoint, "distanceM" | "rssiAvg">[]): PathLossFit | null {
  const pts = points.filter((p) => p.distanceM > 0);
  if (new Set(pts.map((p) => p.distanceM)).size < 2) return null;
  const xs = pts.map((p) => Math.log10(p.distanceM));
  const ys = pts.map((p) => p.rssiAvg);
  const n = pts.length;
  const mx = xs.reduce((a, b) => a + b, 0) / n;
  const my = ys.reduce((a, b) => a + b, 0) / n;
  let sxy = 0, sxx = 0, syy = 0;
  for (let i = 0; i < n; i++) {
    sxy += (xs[i] - mx) * (ys[i] - my);
    sxx += (xs[i] - mx) ** 2;
    syy += (ys[i] - my) ** 2;
  }
  if (sxx === 0) return null;
  const slope = sxy / sxx;           // = -10 n
  const intercept = my - slope * mx; // = rssi at 1 m
  const r2 = syy === 0 ? 1 : (sxy * sxy) / (sxx * syy);
  return { rssiAt1m: intercept, pathLossN: -slope / 10, r2, points: n };
}

export function predictRssi(m: PathLossModel, distanceM: number): number {
  return m.rssiAt1m - 10 * m.pathLossN * Math.log10(distanceM);
}

// RSSI readings of `bandId` at `receiver` that arrived after `since`.
// receiver: "direct" = MainNode heard the band itself; "pin:<idx>" = a pillar.
export function collectRssi(log: PacketLogEntry[], bandId: string, receiver: string, since: number): number[] {
  const band = bandId.toUpperCase();
  const out: number[] = [];
  for (const e of log) {
    if (e.rxAt < since) continue;
    const ev = e.ev;
    if (receiver === "direct" && ev.t === "band" && ev.id.toUpperCase() === band) out.push(ev.rssi);
    if (receiver.startsWith("pin:") && ev.t === "pin_sighting" && ev.pin === Number(receiver.slice(4))) {
      const s = ev.sightings.find((x) => x.band.toUpperCase() === band);
      if (s) out.push(s.rssi);
    }
  }
  return out;
}

export function meanSd(xs: number[]): { mean: number; sd: number } {
  if (xs.length === 0) return { mean: NaN, sd: 0 };
  const mean = xs.reduce((a, b) => a + b, 0) / xs.length;
  const sd = Math.sqrt(xs.reduce((a, b) => a + (b - mean) ** 2, 0) / Math.max(1, xs.length - 1));
  return { mean, sd };
}
