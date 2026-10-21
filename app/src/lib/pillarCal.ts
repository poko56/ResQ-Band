// ============================================================================
// Multi-point pillar calibration.
//
// Each point is a band left at a spot the operator clicked on the map, with
// the RSSI every pillar heard from it. Across the points we fit, per pillar,
//     rssi = a_pillar - 10 * n * log10(distance)
// with one shared n. The pillar intercepts differ by antenna/mounting/board;
// the offset that lines a pillar up with the map's path-loss model is
// RSSI@1m - a_pillar. With few points, or distances too alike to pin down n,
// the model's n is kept and only the offsets are fitted.
//
// Positions are a 1/d^2 weighted average, so only the RATIO of distances
// matters: n and the offsets between pillars set the position, while the
// absolute RSSI@1m does not.
// ============================================================================

import { estimatePosition, metersBetween, median, type RangeReading } from "./locate";
import type { Anchor, LatLng, PathLossModel } from "./types";

export interface PillarCalReading {
  anchorId: string;
  rssi: number;      // median effective RSSI, no offset applied
  samples: number;
}

export interface PillarCalPoint {
  id: string;
  bandId: string;
  truePos: LatLng;
  ts: number;
  readings: PillarCalReading[];
}

export interface PillarCalFit {
  ok: boolean;
  error?: string;
  n: number;
  nFitted: boolean;                    // false: kept the model's n
  offsets: Record<string, number>;     // anchorId -> dB
  perPoint: { id: string; beforeM?: number; afterM?: number }[];
  meanBeforeM?: number;
  meanAfterM?: number;
}

const N_MIN = 1.5, N_MAX = 5;
// log10-distance spread needed before n is trusted (0.1 ~ distances differing by ~2x).
const MIN_LOG_SPREAD = 0.1;

// Where the position estimate puts a calibration point with the given offsets.
export function estimatePoint(
  p: PillarCalPoint, anchors: Record<string, Anchor>, offsets: Record<string, number>, model: PathLossModel,
): LatLng | undefined {
  const readings: RangeReading[] = [];
  for (const r of p.readings) {
    const a = anchors[r.anchorId];
    if (!a?.position) continue;
    const rssi = r.rssi + (offsets[r.anchorId] ?? 0);
    readings.push({ anchor: a.position, distanceM: Math.pow(10, (model.rssiAt1m - rssi) / (10 * model.pathLossN)) });
  }
  return readings.length ? estimatePosition(readings) : undefined;
}

export function fitPillarCal(points: PillarCalPoint[], anchors: Record<string, Anchor>, model: PathLossModel): PillarCalFit {
  const rows: { anchorId: string; x: number; y: number }[] = [];
  for (const p of points) {
    for (const r of p.readings) {
      const a = anchors[r.anchorId];
      if (!a?.position) continue;
      rows.push({ anchorId: r.anchorId, x: Math.log10(Math.max(1, metersBetween(a.position, p.truePos))), y: r.rssi });
    }
  }
  const fail = (error: string): PillarCalFit => ({ ok: false, error, n: model.pathLossN, nFitted: false, offsets: {}, perPoint: [] });
  if (points.length === 0 || rows.length === 0) return fail("ยังไม่มีจุดวัด");

  // Shared slope from the variation WITHIN each pillar (pillar intercepts removed).
  const byAnchor = new Map<string, { x: number; y: number }[]>();
  for (const r of rows) byAnchor.set(r.anchorId, [...(byAnchor.get(r.anchorId) ?? []), r]);
  let sxx = 0, sxy = 0;
  for (const rs of byAnchor.values()) {
    const mx = rs.reduce((s, r) => s + r.x, 0) / rs.length;
    const my = rs.reduce((s, r) => s + r.y, 0) / rs.length;
    for (const r of rs) { sxx += (r.x - mx) ** 2; sxy += (r.x - mx) * (r.y - my); }
  }
  let n = model.pathLossN, nFitted = false;
  if (points.length >= 3 && sxx / rows.length >= MIN_LOG_SPREAD ** 2) {
    const fitted = -sxy / (10 * sxx);
    if (fitted >= N_MIN && fitted <= N_MAX) { n = fitted; nFitted = true; }
  }

  const offsets: Record<string, number> = {};
  for (const [id, rs] of byAnchor) {
    offsets[id] = Math.round(median(rs.map((r) => model.rssiAt1m - 10 * n * r.x - r.y)) * 10) / 10;
  }

  const fittedModel = { ...model, pathLossN: n };
  const perPoint = points.map((p) => {
    const before = estimatePoint(p, anchors, {}, model);
    const after = estimatePoint(p, anchors, offsets, fittedModel);
    return {
      id: p.id,
      beforeM: before ? metersBetween(before, p.truePos) : undefined,
      afterM: after ? metersBetween(after, p.truePos) : undefined,
    };
  });
  const mean = (xs: (number | undefined)[]) => {
    const v = xs.filter((x): x is number => x !== undefined);
    return v.length ? v.reduce((s, x) => s + x, 0) / v.length : undefined;
  };
  return {
    ok: true, n, nFitted, offsets, perPoint,
    meanBeforeM: mean(perPoint.map((p) => p.beforeM)),
    meanAfterM: mean(perPoint.map((p) => p.afterM)),
  };
}
