// ============================================================================
// Node activity ledger for Dev Tools.
//
// Pure helpers that fold MainNode events into per-device counters: packets by
// type, over-the-air bytes, known losses, 10 s activity buckets for the
// dropout timeline, and online/offline periods.
// ============================================================================

import type { ActivityBucket, NodeActivity, NodeKind } from "./types";

export const ACTIVITY_BUCKET_MS = 10_000;          // one TDMA cycle
export const ACTIVITY_WINDOW_BUCKETS = 180;        // 30 minutes of history

// Silence before a node counts as offline (matches sweepStaleNodes).
export const OFFLINE_AFTER_MS: Record<NodeKind, number> = {
  hub: 8_000,
  pin: 25_000,
  band: 30_000,
  resq: 120_000,   // handheld only talks when it has something to report
};

// LoRa payload sizes from lib/ResQProtocol (static_asserted there).
export const PACKET_BYTES: Record<string, number> = {
  HEARTBEAT: 18,
  SOS_TAP: 18,
  SOS_FALL: 18,
  SOS_VITALS: 18,
  BEACON: 18,
  PIN_SIGHTING: 47,
  ASSIGNMENT: 24,
  FOUND: 20,
  RING_ACK: 10,
  RING_CMD: 14,
  PIN_JOIN_REQ: 9,
  PIN_BUTTON: 9,
};

export interface ActivityInput {
  key: string;
  kind: NodeKind;
  label: string;
  type: string;       // packet type label, e.g. "HEARTBEAT"
  lost?: number;      // packets known missing right before this one
  now: number;
}

export interface ActivityResult {
  node: NodeActivity;
  recoveredAfterMs?: number;   // set when the node came back from offline
}

function bucketStart(ts: number): number {
  return ts - (ts % ACTIVITY_BUCKET_MS);
}

function addToBuckets(buckets: ActivityBucket[], ts: number, bytes: number, lost: number): ActivityBucket[] {
  const t = bucketStart(ts);
  const last = buckets[buckets.length - 1];
  let next: ActivityBucket[];
  if (last && last.t === t) {
    next = buckets.slice(0, -1);
    next.push({ t, rx: last.rx + 1, lost: last.lost + lost, bytes: last.bytes + bytes });
  } else {
    next = [...buckets, { t, rx: 1, lost, bytes }];
  }
  const minT = t - (ACTIVITY_WINDOW_BUCKETS - 1) * ACTIVITY_BUCKET_MS;
  return next.filter((b) => b.t >= minT);
}

export function applyActivity(prev: NodeActivity | undefined, input: ActivityInput): ActivityResult {
  const { now } = input;
  const bytes = PACKET_BYTES[input.type] ?? 0;
  const lost = Math.max(0, input.lost ?? 0);

  if (!prev) {
    return {
      node: {
        key: input.key,
        kind: input.kind,
        label: input.label,
        firstSeen: now,
        lastSeen: now,
        rx: 1,
        lost,
        bytes,
        byType: { [input.type]: 1 },
        buckets: addToBuckets([], now, bytes, lost),
        online: true,
        onlineSince: now,
        downtimeMs: 0,
        disconnects: 0,
      },
    };
  }

  let recoveredAfterMs: number | undefined;
  let { online, onlineSince, offlineSince, downtimeMs } = prev;
  if (!online) {
    recoveredAfterMs = now - (offlineSince ?? prev.lastSeen);
    downtimeMs += recoveredAfterMs;
    online = true;
    onlineSince = now;
    offlineSince = undefined;
  }

  return {
    recoveredAfterMs,
    node: {
      ...prev,
      label: input.label,
      lastSeen: now,
      rx: prev.rx + 1,
      lost: prev.lost + lost,
      bytes: prev.bytes + bytes,
      byType: { ...prev.byType, [input.type]: (prev.byType[input.type] ?? 0) + 1 },
      buckets: addToBuckets(prev.buckets, now, bytes, lost),
      online,
      onlineSince,
      offlineSince,
      downtimeMs,
    },
  };
}

// Flip nodes that went quiet to offline. Returns null when nothing changed.
export function sweepActivity(
  activity: Record<string, NodeActivity>,
  now: number,
): { activity: Record<string, NodeActivity>; wentOffline: NodeActivity[] } | null {
  const wentOffline: NodeActivity[] = [];
  let next: Record<string, NodeActivity> | null = null;
  for (const key in activity) {
    const n = activity[key];
    if (n.online && now - n.lastSeen > OFFLINE_AFTER_MS[n.kind]) {
      next ??= { ...activity };
      const off = { ...n, online: false, offlineSince: n.lastSeen, disconnects: n.disconnects + 1 };
      next[key] = off;
      wentOffline.push(off);
    }
  }
  return next ? { activity: next, wentOffline } : null;
}

// Time a node has been offline in total, including an offline period that is
// still open.
export function totalDowntimeMs(n: NodeActivity, now: number): number {
  return n.downtimeMs + (n.online ? 0 : now - (n.offlineSince ?? n.lastSeen));
}

export function uptimePct(n: NodeActivity, now: number): number {
  const span = now - n.firstSeen;
  if (span <= 0) return 100;
  return Math.max(0, Math.min(100, (1 - totalDowntimeMs(n, now) / span) * 100));
}

// Packets that went missing, as a share of the ones that should have arrived.
export function lossPct(n: { rx: number; lost: number }): number {
  const expected = n.rx + n.lost;
  return expected > 0 ? (n.lost / expected) * 100 : 0;
}

export function formatBytes(b: number): string {
  if (b < 1024) return `${b} B`;
  if (b < 1024 * 1024) return `${(b / 1024).toFixed(1)} KB`;
  return `${(b / 1024 / 1024).toFixed(2)} MB`;
}

export function formatDuration(ms: number): string {
  const s = Math.max(0, Math.round(ms / 1000));
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ${s % 60}s`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}m`;
}
