import { effectiveRssi, median, metersBetween, pathLossDistanceM, RSSI_SAMPLE_MAX_AGE_MS, RSSI_SAMPLES, estimatePosition, type RangeReading } from "./locate";
import { fitPillarCal, type PillarCalFit, type PillarCalPoint, type PillarCalReading } from "./pillarCal";
import { create } from "zustand";
import { createJSONStorage, persist, type StateStorage } from "zustand/middleware";
import { nanoid } from "nanoid";
import type {
  Anchor,
  AssignReason,
  BandLinkDiagnostic,
  BandStatus,
  ConnectionDiagnostics,
  DisconnectEvent,
  HubEvent,
  HubStatus,
  Incident,
  LatLng,
  LinkHealthPoint,
  PacketLogEntry,
  PathLossModel,
  RangeTestPoint,
  PinLinkDiagnostic,
  PinRssi,
  Sighting,
  TimelineEvent,
  TimelineEventType,
  TriageLevel,
  UnassignedAnchor,
  Wristband,
} from "./types";
import { ACTIVITY_BUCKET_MS, applyActivity, formatDuration, sweepActivity, type ActivityInput } from "./activity";

type PlacementMode = "none" | string; // "none" or anchorId

const TRIAGE_FROM_BYTE: Record<number, TriageLevel> = { 0: "green", 1: "yellow", 2: "red", 3: "black", 4: "nodata" };
const SPO2_NO_SENSOR = 255;   // with triage 4: the band has no working pulse sensor

interface ResQState {
  incident: Incident;
  wristbands: Record<string, Wristband>;
  anchors: Record<string, Anchor>;       // keyed by anchor.id (Pin device hex when known)
  unassignedAnchors: Record<string, UnassignedAnchor>; // recently seen unassigned pins
  sightings: Record<string, Sighting>;
  timeline: TimelineEvent[];
  placementMode: PlacementMode;
  searchMode: boolean;
  hub: HubStatus;
  diagnostics: ConnectionDiagnostics;

  // --- Hub link wiring (called from hubBridge.ts) -------------------------
  setHubState: (state: HubStatus["state"], error?: string) => void;
  setHubInfo: (partial: Partial<HubStatus>) => void;
  notifyHubHeartbeat: () => void;
  incHubPackets: () => void;
  incHubDropped: () => void;
  recordHubStats: (stats: { rx: number; dropped: number; uptimeS: number; cycle: number; heap?: number }) => void;
  resetDiagnostics: () => void;

  // --- Dev Tools ---------------------------------------------------------
  packetLog: PacketLogEntry[];
  logPacket: (ev: HubEvent) => void;
  clearPacketLog: () => void;
  pathLoss: PathLossModel;                 // used for positions + coverage
  setPathLoss: (m: PathLossModel) => void;
  rangePoints: RangeTestPoint[];
  addRangePoint: (p: Omit<RangeTestPoint, "id" | "ts">) => void;
  removeRangePoint: (id: string) => void;
  clearRangePoints: () => void;
  recordActivity: (input: Omit<ActivityInput, "now">) => void;
  setDemoMode: (enabled: boolean) => void;

  // --- Roster ops --------------------------------------------------------
  registerWristband: (input: Omit<Wristband, "registeredAt" | "id"> & { id?: string }) => Wristband;
  removeWristband: (id: string) => void;

  placeAnchor: (position: LatLng, name?: string) => Anchor;
  removeAnchor: (id: string) => void;
  renameAnchor: (id: string, name: string) => void;
  setAnchorPosition: (id: string, position: LatLng) => void;
  // Calibrate pillar RSSI offsets from a band sitting at `truePos`.
  // Multi-point pillar calibration (see lib/pillarCal.ts).
  pillarCalPoints: PillarCalPoint[];
  addPillarCalPoint: (bandId: string, truePos: LatLng) => { ok: boolean; error?: string };
  removePillarCalPoint: (id: string) => void;
  clearPillarCalPoints: () => void;
  applyPillarCal: () => PillarCalFit;
  clearPillarOffsets: () => void;
  registerAnchorId: (id: string, pinIndex?: number) => void;
  addUnassignedAnchor: (id: string) => void;

  setPlacementMode: (mode: PlacementMode) => void;

  // --- Live updates from MainNode events ---------------------------------
  upsertSightingFromBand: (ev: {
    id: string; ptype: string; seq: number; triage: number;
    hr: number; spo2: number; batt: number; g_x10: number;
    rssi: number; snr: number; ts: number;
  }) => void;
  applyPinSighting: (ev: {
    pin: number; pin_id: string;
    sightings: { band: string; rssi: number; snr: number; age_ms: number }[];
    ts: number;
    rssi?: number;
    snr?: number;
  }) => void;
  applyAssignment: (ev: {
    band: string; score: number; pin: number; rssi: number;
    triage: number; reason: AssignReason; ts: number;
  }) => void;
  applyFound: (ev: {
    node_id: string; band: string; outcome: string; rssi: number; ts: number;
  }) => void;

  // --- Local actions that ALSO send commands to MainNode ------------------
  markRescued: (wristbandId: string) => void;        // sends mark_rescued cmd
  manualBoost: (wristbandId: string, boost: number) => void;
  ringWristband: (wristbandId: string, opts?: { durationMs?: number; freq?: number; pattern?: number }) => void;

  // --- Misc --------------------------------------------------------------
  pushEvent: (type: TimelineEventType, message: string, refs?: { wristbandId?: string; anchorId?: string }) => void;
  resetIncident: () => void;
  sweepStaleNodes: () => void;
}

const defaultIncident = (): Incident => ({
  id: nanoid(8),
  name: "ภารกิจไม่มีชื่อ",
  startedAt: Date.now(),
});

const defaultHub = (): HubStatus => ({
  state: "disconnected",
  loraPacketsReceived: 0,
  loraPacketsDropped: 0,
  demoMode: false,
  emergencyMode: false,
});

// Diagnostics are saved across reloads. Packets sent while the page was closed
// were never heard, so the first packet from each band/pillar in this page load
// only sets the baseline instead of counting the gap as loss.
const linkBaselined = new Set<string>();
function firstSinceLoad(key: string): boolean {
  if (linkBaselined.has(key)) return false;
  linkBaselined.add(key);
  return true;
}

const defaultDiagnostics = (): ConnectionDiagnostics => ({
  bands: {},
  pins: {},
  activity: {},
  hubStats: {
    totalRx: 0,
    totalDropped: 0,
    uptimeS: 0,
    currentCycle: 0,
    history: [],
  },
  disconnectEvents: [],
});

// Fold one event into the activity ledger; adds a "recovered" log entry when
// the node was offline.
function foldActivity(diag: ConnectionDiagnostics, input: ActivityInput): ConnectionDiagnostics {
  const { node, recoveredAfterMs } = applyActivity(diag.activity[input.key], input);
  let disconnectEvents = diag.disconnectEvents;
  if (recoveredAfterMs !== undefined) {
    disconnectEvents = [{
      id: nanoid(6),
      ts: input.now,
      type: "recovered" as const,
      target: input.label,
      message: `${input.label} กลับมาเชื่อมต่อแล้ว (หายไป ${formatDuration(recoveredAfterMs)})`,
    }, ...disconnectEvents].slice(0, 100);
  }
  return { ...diag, activity: { ...diag.activity, [input.key]: node }, disconnectEvents };
}

function bandLabel(id: string, wristbands: Record<string, Wristband>): string {
  const name = wristbands[id]?.name;
  return name ? `${name} (${id})` : `Band ${id}`;
}

function statusFromPacketType(ptype: string, prev?: BandStatus): BandStatus {
  if (ptype === "SOS_TAP" || ptype === "SOS_FALL" || ptype === "SOS_VITALS") return "sos";
  if (prev === "rescued" || prev === "deceased") return prev;
  if (prev === "assigned") return "assigned";
  return "active";
}

// Forward decl - filled in by hubBridge so the store doesn't import it (no cycle).
let _sendCommand: ((cmd: object) => void) | null = null;
export function _registerSendCommand(fn: (cmd: object) => void) {
  _sendCommand = fn;
}

// localStorage writes are batched: persist() serialises the saved slice on
// every state change, which with sightings saved too (and photos in the
// roster) would mean a large JSON write per incoming packet. Writes land at
// most once a second and are flushed when the page is hidden or closed.
const SAVE_DELAY_MS = 1000;
const pendingWrites = new Map<string, string>();
let saveTimer: ReturnType<typeof setTimeout> | null = null;
// persist() writes on every set(), even before rehydrate() has loaded the saved
// state, which would overwrite it with empty defaults. Hold writes until then.
let storeHydrated = false;
function flushWrites() {
  if (saveTimer) { clearTimeout(saveTimer); saveTimer = null; }
  for (const [k, v] of pendingWrites) {
    try { localStorage.setItem(k, v); } catch (err) { console.warn("[store] save failed", err); }
  }
  pendingWrites.clear();
}
const batchedStorage: StateStorage = {
  getItem: (k) => pendingWrites.get(k) ?? (typeof localStorage === "undefined" ? null : localStorage.getItem(k)),
  setItem: (k, v) => {
    if (!storeHydrated) return;
    pendingWrites.set(k, v);
    saveTimer ??= setTimeout(flushWrites, SAVE_DELAY_MS);
  },
  removeItem: (k) => { pendingWrites.delete(k); try { localStorage.removeItem(k); } catch { /* blocked */ } },
};
if (typeof window !== "undefined") {
  window.addEventListener("pagehide", flushWrites);
  document.addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden") flushWrites(); });
}

const PACKET_LOG_MAX = 2000;
let _packetSeq = 0;

// Default path-loss model until a range test calibrates it. Field fit
// 2026-10-20, band -> handheld at 1-23.5 m: RSSI@1m -75.8 dBm, n 2.51, R^2 0.97
// (the handheld firmware uses the same curve).
export const DEFAULT_PATH_LOSS: PathLossModel = { rssiAt1m: -76, pathLossN: 2.5, source: "ค่าเริ่มต้น (วัดภาคสนาม 7 จุด, R² 0.97)" };
// The earlier uncalibrated default; browsers that saved it get the new one.
const OLD_DEFAULT_SOURCE = "ค่าเริ่มต้น (ยังไม่ได้ปรับเทียบ)";

export function rssiToDistanceM(rssi: number, m: PathLossModel): number {
  return pathLossDistanceM(rssi, m);
}

// RSSI the position uses for one pillar: median of its recent effective readings.
export function pinRssiForRange(p: PinRssi, now = Date.now()): number {
  const recent = (p.samples ?? []).filter((x) => now - x.t < RSSI_SAMPLE_MAX_AGE_MS).map((x) => x.v);
  return recent.length ? median(recent) : effectiveRssi(p.rssi, p.snr);
}

// Band position from the pillars that heard it recently (see lib/locate.ts).
function calculateWeightedPosition(rssiPerPin: Record<number, PinRssi>, anchors: Record<string, Anchor>, model: PathLossModel = DEFAULT_PATH_LOSS): LatLng | undefined {
  const now = Date.now();
  const readings: RangeReading[] = [];
  for (const s of Object.values(rssiPerPin)) {
    if (now - s.timestamp >= 45000) continue;   // a pillar can miss a report or two
    const anchor = Object.values(anchors).find((a) => a.pinIndex === s.pinIndex);
    if (!anchor?.position) continue;
    const rssi = pinRssiForRange(s, now) + (anchor.rssiOffsetDb ?? 0);
    readings.push({ anchor: anchor.position, distanceM: rssiToDistanceM(rssi, model) });
  }
  return estimatePosition(readings);
}

export const useResQ = create<ResQState>()(
  persist(
    (set, get) => ({
  incident: defaultIncident(),
  wristbands: {},
  anchors: {},
  unassignedAnchors: {},
  sightings: {},
  timeline: [],
  placementMode: "none",
  searchMode: false,
  hub: defaultHub(),
  diagnostics: defaultDiagnostics(),

  packetLog: [],
  logPacket: (ev) =>
    set((s) => ({
      packetLog: [...s.packetLog.slice(-(PACKET_LOG_MAX - 1)), { id: ++_packetSeq, rxAt: Date.now(), ev }],
    })),
  clearPacketLog: () => set({ packetLog: [] }),

  pathLoss: DEFAULT_PATH_LOSS,
  setPathLoss: (m) => set({ pathLoss: { ...m, updatedAt: Date.now() } }),

  rangePoints: [],
  addRangePoint: (p) =>
    set((s) => ({ rangePoints: [...s.rangePoints, { ...p, id: nanoid(6), ts: Date.now() }] })),
  removeRangePoint: (id) =>
    set((s) => ({ rangePoints: s.rangePoints.filter((r) => r.id !== id) })),
  clearRangePoints: () => set({ rangePoints: [] }),

  recordHubStats: (stats) =>
    set((s) => {
      const now = Date.now();
      const prev = s.diagnostics.hubStats;
      const historyPoint = {
        ts: now,
        rx: stats.rx,
        dropped: stats.dropped,
        heap: stats.heap ?? 0,
      };
      // MainNode's own "dropped" counter (runt/CRC-failed LoRa frames) is the
      // hub's loss; count only the increase since the previous stats line.
      const droppedDelta = prev.history.length > 0 ? Math.max(0, stats.dropped - prev.totalDropped) : 0;
      return {
        diagnostics: foldActivity({
          ...s.diagnostics,
          hubStats: {
            totalRx: stats.rx,
            totalDropped: stats.dropped,
            uptimeS: stats.uptimeS,
            currentCycle: stats.cycle,
            history: [...prev.history, historyPoint].slice(-120),
          },
        }, {
          key: "hub",
          kind: "hub",
          label: "MainNode",
          type: "STATS",
          lost: droppedDelta,
          now,
        }),
      };
    }),

  resetDiagnostics: () =>
    set({
      diagnostics: defaultDiagnostics(),
    }),

  recordActivity: (input) =>
    set((s) => ({ diagnostics: foldActivity(s.diagnostics, { ...input, now: Date.now() }) })),

  setHubState: (state, error) =>
    set((s) => ({ hub: { ...s.hub, state, lastError: error } })),

  setHubInfo: (partial) =>
    set((s) => ({ hub: { ...s.hub, ...partial } })),

  notifyHubHeartbeat: () =>
    set((s) => {
      const becameConnected = s.hub.state !== "connected";
      return { hub: {
        ...s.hub,
        lastHeartbeat: Date.now(),
        // Any event from the MainNode counts as proof of connection.
        state: "connected",
        lastError: undefined,
        connectStartedAt: becameConnected ? undefined : s.hub.connectStartedAt,
        connectAttempts:  becameConnected ? undefined : s.hub.connectAttempts,
      } };
    }),

  incHubPackets: () =>
    set((s) => ({ hub: { ...s.hub, loraPacketsReceived: s.hub.loraPacketsReceived + 1 } })),

  incHubDropped: () =>
    set((s) => ({ hub: { ...s.hub, loraPacketsDropped: s.hub.loraPacketsDropped + 1 } })),

  setDemoMode: (enabled) =>
    set((s) => ({ hub: { ...s.hub, demoMode: enabled } })),

  setPlacementMode: (mode) => set({ placementMode: mode }),

  registerWristband: (input) => {
    const id = (input.id ?? nanoid(8).toUpperCase()).toUpperCase();
    const wb: Wristband = { ...input, id, registeredAt: Date.now() };
    set((s) => ({ wristbands: { ...s.wristbands, [wb.id]: wb } }));
    get().pushEvent("wristband_registered", `ลงทะเบียนกำไล ${wb.name} (${wb.id})`, { wristbandId: wb.id });
    return wb;
  },

  removeWristband: (id) => {
    set((s) => {
      const next = { ...s.wristbands };
      delete next[id];
      return { wristbands: next };
    });
  },

  placeAnchor: (position, name) => {
    // Pick lowest free pinIndex in 0..3 so deletes free up slots
    const used = new Set(Object.values(get().anchors).map((a) => a.pinIndex));
    let pinIndex = 0;
    while (used.has(pinIndex) && pinIndex < 4) pinIndex++;
    if (pinIndex >= 4) pinIndex = 3;   // overflow → overwrite last slot conceptually
    const id = nanoid(6).toUpperCase();
    const anchor: Anchor = {
      id,
      pinIndex,
      // Display is 1-based (เสา 1..4); pinIndex stays 0-based internally.
      name: name ?? `Pin-${pinIndex + 1}`,
      position,
      placedAt: Date.now(),
      online: false,
      lastSeen: undefined,
    };
    set((s) => ({ anchors: { ...s.anchors, [id]: anchor }, placementMode: "none" }));
    get().pushEvent("anchor_placed",
      `วางเสา ${anchor.name} (เสา ${pinIndex + 1}) ที่ ${position.lat.toFixed(5)}, ${position.lng.toFixed(5)}`,
      { anchorId: id });
    return anchor;
  },

  removeAnchor: (id) => {
    set((s) => {
      const next = { ...s.anchors };
      delete next[id];
      return { anchors: next };
    });
  },

  renameAnchor: (id, name) => {
    set((s) => {
      const a = s.anchors[id];
      if (!a) return s;
      return { anchors: { ...s.anchors, [id]: { ...a, name } } };
    });
  },

  setAnchorPosition: (id, position) => {
    set((s) => {
      const a = s.anchors[id];
      if (!a) return s;
      return { anchors: { ...s.anchors, [id]: { ...a, position } } };
    });
  },

  pillarCalPoints: [],

  addPillarCalPoint: (bandId, truePos) => {
    const s = get();
    const sighting = s.sightings[bandId];
    const now = Date.now();
    const readings: PillarCalReading[] = [];
    for (const a of Object.values(s.anchors)) {
      if (!a.position) continue;
      const pr = sighting?.rssiPerPin[a.pinIndex];
      if (!pr || now - pr.timestamp > RSSI_SAMPLE_MAX_AGE_MS) continue;
      const samples = (pr.samples ?? []).filter((x) => now - x.t < RSSI_SAMPLE_MAX_AGE_MS).length;
      readings.push({ anchorId: a.id, rssi: Math.round(pinRssiForRange(pr, now) * 10) / 10, samples });
    }
    if (readings.length < 2) return { ok: false, error: "ต้องมีอย่างน้อย 2 เสาที่ได้ยินกำไลนี้ในนาทีที่ผ่านมา" };
    set({ pillarCalPoints: [...s.pillarCalPoints, { id: nanoid(6), bandId, truePos, ts: now, readings }] });
    return { ok: true };
  },

  removePillarCalPoint: (id) => set((s) => ({ pillarCalPoints: s.pillarCalPoints.filter((p) => p.id !== id) })),
  clearPillarCalPoints: () => set({ pillarCalPoints: [] }),

  applyPillarCal: () => {
    const s = get();
    const fit = fitPillarCal(s.pillarCalPoints, s.anchors, s.pathLoss);
    if (!fit.ok) return fit;
    const anchors = Object.fromEntries(Object.entries(s.anchors).map(([k, a]) =>
      [k, k in fit.offsets ? { ...a, rssiOffsetDb: fit.offsets[k] } : a]));
    const pathLoss = fit.nFitted
      ? { ...s.pathLoss, pathLossN: fit.n, source: `ปรับเทียบเสา ${s.pillarCalPoints.length} จุด`, updatedAt: Date.now() }
      : s.pathLoss;
    set({ anchors, pathLoss });
    return fit;
  },

  clearPillarOffsets: () =>
    set((s) => ({
      anchors: Object.fromEntries(Object.entries(s.anchors).map(([k, a]) => [k, { ...a, rssiOffsetDb: undefined }])),
    })),

  registerAnchorId: (id, pinIndex) => {
    set((s) => {
      if (s.anchors[id]) return s;
      // If we don't have a specific pinIndex, pick the lowest free one (0..3)
      let idx = pinIndex;
      if (idx === undefined || idx < 0) {
        const used = new Set(Object.values(s.anchors).map((a) => a.pinIndex));
        idx = 0;
        while (used.has(idx) && idx < 4) idx++;
        if (idx >= 4) idx = 3;
      }
      const anchor: Anchor = {
        id,
        pinIndex: idx,
        name: `Pin-${idx + 1}`,   // 1-based display, 0-based idx internally
        online: true, // We just heard from it
        placedAt: Date.now(),
        lastSeen: Date.now(),
      };
      // Once registered, remove it from unassigned list
      const nextUnassigned = { ...s.unassignedAnchors };
      delete nextUnassigned[id];
      return { anchors: { ...s.anchors, [id]: anchor }, unassignedAnchors: nextUnassigned };
    });
  },

  addUnassignedAnchor: (id) => {
    set((s) => {
      // Don't add if already assigned
      if (s.anchors[id]) return s;
      return {
        unassignedAnchors: {
          ...s.unassignedAnchors,
          [id]: { id, lastSeen: Date.now() },
        }
      };
    });
  },

  upsertSightingFromBand: (ev) => {
    // The band decides black (on skin, no pulse) vs nodata (no reading). Very
    // old firmware sent GREEN with HR 0; that is no reading, not a healthy band.
    const byTriage = TRIAGE_FROM_BYTE[ev.triage] ?? "green";
    const triage: TriageLevel = ev.hr === 0 && byTriage === "green" ? "nodata" : byTriage;
    const sensorMissing = triage === "nodata" && ev.spo2 === SPO2_NO_SENSOR;
    const now = Date.now();
    set((s) => {
      const prev = s.sightings[ev.id];
      const status = statusFromPacketType(ev.ptype, prev?.status);
      const next: Sighting = {
        wristbandId: ev.id,
        lastSeen: now,
        position: prev?.position,
        rssiPerPin: prev?.rssiPerPin ?? {},
        triage,
        heartRate: ev.hr,
        spo2: sensorMissing ? 0 : ev.spo2,
        sensorMissing,
        batteryPct: ev.batt,
        lastGForce: ev.g_x10 / 10,
        hopCount: 0,
        status,
        priorityScore: prev?.priorityScore,
        manualBoost: prev?.manualBoost,
        assignedAt: prev?.assignedAt,
      };
      const searchMode = s.searchMode || status === "sos";

      // --- Dev Link Diagnostics Update ---
      const prevBandDiag = s.diagnostics.bands[ev.id];
      let totalExpected = prevBandDiag?.totalExpected ?? 0;
      let totalReceived = (prevBandDiag?.totalReceived ?? 0) + 1;
      let totalLost = prevBandDiag?.totalLost ?? 0;
      let lostInBetween = 0;

      const resumed = firstSinceLoad(`band:${ev.id}`);
      if (resumed && prevBandDiag) {
        totalExpected += 1;
      } else if (prevBandDiag && prevBandDiag.lastSeq !== undefined) {
        const lastSeq = prevBandDiag.lastSeq;
        // 16-bit sequence delta
        const seqDiff = (ev.seq >= lastSeq) ? (ev.seq - lastSeq) : (65536 - lastSeq + ev.seq);
        if (seqDiff > 0) {
          lostInBetween = seqDiff > 1 ? seqDiff - 1 : 0;
          totalExpected += seqDiff;
          totalLost += lostInBetween;
        } else {
          totalExpected += 1;
        }
      } else {
        totalExpected = 1;
      }

      const lossRatePct = totalExpected > 0 ? (totalLost / totalExpected) * 100 : 0;
      const historyPoint: LinkHealthPoint = {
        ts: now,
        rssi: ev.rssi,
        snr: ev.snr,
        seq: ev.seq,
        lostCount: lostInBetween,
        source: "direct",
      };

      const nextDisconnectEvents = [...s.diagnostics.disconnectEvents];
      if (lostInBetween > 0) {
        nextDisconnectEvents.unshift({
          id: nanoid(6),
          ts: now,
          type: "drop",
          target: ev.id,
          message: `Band ${ev.id} พลาดการส่ง ${lostInBetween} แพ็กเก็ต (seq: ${prevBandDiag?.lastSeq} -> ${ev.seq})`,
        });
      }

      const nextBandDiag: BandLinkDiagnostic = {
        id: ev.id,
        firstSeen: prevBandDiag?.firstSeen ?? now,
        lastSeen: now,
        lastSeq: ev.seq,
        totalExpected,
        totalReceived,
        totalLost,
        lossRatePct,
        consecutiveMissed: 0,
        currentRssi: ev.rssi,
        currentSnr: ev.snr,
        history: [...(prevBandDiag?.history ?? []), historyPoint].slice(-60),
        pinStats: prevBandDiag?.pinStats ?? {},
      };

      return {
        sightings: { ...s.sightings, [ev.id]: next },
        searchMode,
        diagnostics: foldActivity({
          ...s.diagnostics,
          bands: { ...s.diagnostics.bands, [ev.id]: nextBandDiag },
          disconnectEvents: nextDisconnectEvents.slice(0, 100),
        }, {
          key: `band:${ev.id}`,
          kind: "band",
          label: bandLabel(ev.id, s.wristbands),
          type: ev.ptype,
          lost: lostInBetween,
          now,
        }),
      };
    });
    if (ev.ptype === "SOS_TAP" || ev.ptype === "SOS_FALL" || ev.ptype === "SOS_VITALS") {
      const wb = get().wristbands[ev.id];
      const who = wb?.name ?? ev.id;
      const desc =
        ev.ptype === "SOS_VITALS"
          ? "สัญญาณชีพวิกฤต"
          : ev.ptype === "SOS_FALL"
          ? "ตรวจพบการล้ม"
          : "กดเคาะขอความช่วยเหลือ";
      get().pushEvent(
        ev.ptype === "SOS_FALL" ? "fall_received" : "sos_received",
        `${who}: ${desc} (HR ${ev.hr}, SpO₂ ${ev.spo2})`,
        { wristbandId: ev.id },
      );
    }
  },

  applyPinSighting: (ev) => {
    // Update anchor online state
    set((s) => {
      const nextAnchors = { ...s.anchors };
      // Identify a pin by its DEVICE id (pin_id), never by its slot index.
      // Matching on pinIndex let two different pins collide on the same slot
      // (both showed "Pin-1"). The firmware's ev.pin is authoritative for the
      // slot, so adopt it every time and keep the anchor keyed by its device id.
      const id = ev.pin_id || `idx${ev.pin}`;
      const existing = s.anchors[id];
      // Preserve a user-given custom name; refresh a default "Pin-N" label so a
      // stale/1-off name self-corrects from the real firmware index.
      const isDefaultName = !existing || /^Pin-\d+$/.test(existing.name);
      nextAnchors[id] = {
        id,
        pinIndex: ev.pin,
        name: isDefaultName ? `Pin-${ev.pin + 1}` : existing.name,
        position: existing?.position,
        rssiOffsetDb: existing?.rssiOffsetDb,
        placedAt: existing?.placedAt ?? Date.now(),
        online: true,
        lastSeen: Date.now(),
      };
      // Update RSSI per pin on each band's sighting
      const nextSightings = { ...s.sightings };
      for (const sg of ev.sightings) {
        const bandId = sg.band.toUpperCase();
        const prev = nextSightings[bandId];
        const heardAt = Date.now() - sg.age_ms;
        const prevPin = prev?.rssiPerPin?.[ev.pin];
        // Same packet re-reported (pillars repeat a sighting until it ages out): don't count it twice.
        const isNew = !prevPin || Math.abs(prevPin.timestamp - heardAt) > 1500;
        const samples = isNew
          ? [...(prevPin?.samples ?? []).filter((x) => heardAt - x.t < RSSI_SAMPLE_MAX_AGE_MS), { v: effectiveRssi(sg.rssi, sg.snr), t: heardAt }].slice(-RSSI_SAMPLES)
          : prevPin!.samples;
        const rssiEntry: PinRssi = { pinIndex: ev.pin, rssi: sg.rssi, snr: sg.snr, ageMs: sg.age_ms, timestamp: heardAt, samples };
        const rssiPerPin = { ...(prev?.rssiPerPin ?? {}), [ev.pin]: rssiEntry };
        // Infer position from all recently heard pillars
        let position = calculateWeightedPosition(rssiPerPin, s.anchors, s.pathLoss) ?? prev?.position;
        nextSightings[bandId] = {
          wristbandId: bandId,
          lastSeen: prev?.lastSeen ?? Date.now(),
          position,
          rssiPerPin,
          triage: prev?.triage ?? "green",
          heartRate: prev?.heartRate ?? 0,
          spo2: prev?.spo2 ?? 0,
          batteryPct: prev?.batteryPct ?? 0,
          lastGForce: prev?.lastGForce ?? 0,
          hopCount: prev?.hopCount ?? 0,
          status: prev?.status ?? "active",
          priorityScore: prev?.priorityScore,
          manualBoost: prev?.manualBoost,
          assignedAt: prev?.assignedAt,
        };
      }

      // --- Dev Link Diagnostics for Pillars ---
      const now = Date.now();
      const prevPinDiag = s.diagnostics.pins[ev.pin];
      // A pin reports once per TDMA cycle, so a longer gap means missed reports.
      const prevPinActivity = s.diagnostics.activity[`pin:${ev.pin}`];
      const missedReports = prevPinActivity && !firstSinceLoad(`pin:${ev.pin}`)
        ? Math.max(0, Math.round((now - prevPinActivity.lastSeen) / ACTIVITY_BUCKET_MS) - 1)
        : 0;
      const pinRssi = ev.rssi ?? -90;
      const pinSnr = ev.snr ?? 0;
      const nextPinDiag: PinLinkDiagnostic = {
        pinIndex: ev.pin,
        pinId: id,
        lastSeen: now,
        rssiToHub: pinRssi,
        snrToHub: pinSnr,
        reportsCount: (prevPinDiag?.reportsCount ?? 0) + 1,
        historyToHub: [...(prevPinDiag?.historyToHub ?? []), { ts: now, rssi: pinRssi, snr: pinSnr }].slice(-60),
        bandsHeardHistory: [...(prevPinDiag?.bandsHeardHistory ?? []), { ts: now, count: ev.sightings.length }].slice(-60),
      };

      const nextBandsDiag = { ...s.diagnostics.bands };
      for (const sg of ev.sightings) {
        const bandId = sg.band.toUpperCase();
        const bDiag = nextBandsDiag[bandId];
        if (bDiag) {
          const prevPinStat = bDiag.pinStats[ev.pin];
          const nextPinStat = {
            heardCount: (prevPinStat?.heardCount ?? 0) + 1,
            lastRssi: sg.rssi,
            lastSnr: sg.snr,
            lastSeen: now,
            history: [...(prevPinStat?.history ?? []), { ts: now, rssi: sg.rssi, snr: sg.snr }].slice(-60),
          };
          nextBandsDiag[bandId] = {
            ...bDiag,
            pinStats: { ...bDiag.pinStats, [ev.pin]: nextPinStat },
          };
        }
      }

      return {
        anchors: nextAnchors,
        sightings: nextSightings,
        diagnostics: foldActivity({
          ...s.diagnostics,
          pins: { ...s.diagnostics.pins, [ev.pin]: nextPinDiag },
          bands: nextBandsDiag,
        }, {
          key: `pin:${ev.pin}`,
          kind: "pin",
          label: `เสา ${ev.pin + 1}`,
          type: "PIN_SIGHTING",
          lost: missedReports,
          now,
        }),
      };
    });
  },

  applyAssignment: (ev) => {
    const bandId = ev.band.toUpperCase();
    set((s) => {
      const prev = s.sightings[bandId];
      if (!prev) return s;
      return {
        sightings: {
          ...s.sightings,
          [bandId]: {
            ...prev,
            status: prev.status === "rescued" || prev.status === "deceased" ? prev.status : "assigned",
            priorityScore: ev.score,
            lastReason: ev.reason,
            assignedAt: Date.now(),
          },
        },
      };
    });
    const wb = get().wristbands[bandId];
    get().pushEvent("assignment_dispatched",
      `สั่งให้ค้นหา ${wb?.name ?? bandId} (score ${ev.score}, เหตุผล: ${ev.reason})`,
      { wristbandId: bandId });
  },

  applyFound: (ev) => {
    const bandId = ev.band.toUpperCase();
    const outcome = ev.outcome as NonNullable<Sighting["outcome"]>;
    const prev0 = get().sightings[bandId];
    // The handheld repeats FOUND up to 3 times (no ack): log each outcome once.
    const repeat = prev0?.outcome === outcome && prev0.rescuedAt !== undefined && Date.now() - prev0.rescuedAt < 60_000;
    set((s) => {
      const prev = s.sightings[bandId];
      if (!prev) return s;
      // Only rescued / deceased close the case. "not found" and "needs backup"
      // put the band back in the queue for another dispatch.
      const status: BandStatus =
        outcome === "deceased" ? "deceased" :
        outcome === "rescued"  ? "rescued"  :
        prev.status === "assigned" ? "active" : prev.status;
      return {
        sightings: {
          ...s.sightings,
          [bandId]: {
            ...prev,
            status,
            rescuedAt: repeat ? prev.rescuedAt : Date.now(),
            rescuerNodeId: ev.node_id,
            finalRssi: ev.rssi,
            outcome,
          },
        },
      };
    });
    if (repeat) return;
    const wb = get().wristbands[bandId];
    const who = wb?.name ?? bandId;
    const text: Record<string, string> = {
      rescued: `กู้ภัยช่วย ${who} ออกมาได้แล้ว`,
      deceased: `กู้ภัยพบ ${who} เสียชีวิต`,
      not_found: `กู้ภัยไปถึงจุดของ ${who} แต่ไม่พบคน — กลับเข้าคิวค้นหา`,
      needs_backup: `กู้ภัยที่จุดของ ${who} ขอกำลังเสริม/อุปกรณ์`,
    };
    get().pushEvent("found_by_handheld", `${text[outcome] ?? `ผลจากเครื่องค้นหา: ${outcome}`} (ResQ-Node ${ev.node_id})`, { wristbandId: bandId });
  },

  markRescued: (wristbandId) => {
    set((s) => {
      const existing = s.sightings[wristbandId];
      if (!existing) return s;
      return {
        sightings: {
          ...s.sightings,
          [wristbandId]: { ...existing, status: "rescued", rescuedAt: Date.now() },
        },
      };
    });
    const wb = get().wristbands[wristbandId];
    get().pushEvent("marked_rescued", `กู้ภัยช่วย ${wb?.name ?? wristbandId} ออกมาได้แล้ว`, { wristbandId });
    _sendCommand?.({ c: "mark_rescued", band: wristbandId });
  },

  manualBoost: (wristbandId, boost) => {
    set((s) => {
      const existing = s.sightings[wristbandId];
      if (!existing) return s;
      return {
        sightings: {
          ...s.sightings,
          [wristbandId]: { ...existing, manualBoost: boost },
        },
      };
    });
    get().pushEvent("manual_priority", `ปรับลำดับความสำคัญ ${wristbandId} +${boost}`, { wristbandId });
    _sendCommand?.({ c: "manual_priority", band: wristbandId, boost });
  },

  ringWristband: (wristbandId, opts) => {
    _sendCommand?.({
      c: "ring_band",
      band: wristbandId,
      duration_ms: opts?.durationMs ?? 3000,
      freq: opts?.freq ?? 2000,
      pattern: opts?.pattern ?? 1,
    });
  },

  pushEvent: (type, message, refs) => {
    const ev: TimelineEvent = {
      id: nanoid(8),
      ts: Date.now(),
      type,
      message,
      wristbandId: refs?.wristbandId,
      anchorId: refs?.anchorId,
    };
    set((s) => ({ timeline: [ev, ...s.timeline].slice(0, 500) }));
  },

  resetIncident: () => {
    set({
      incident: defaultIncident(),
      anchors: {},
      sightings: {},
      timeline: [],
      searchMode: false,
    });
  },

  sweepStaleNodes: () => {
    const now = Date.now();
    set((s) => {
      let changed = false;
      const nextAnchors = { ...s.anchors };
      const nextSightings = { ...s.sightings };
      const nextDisconnectEvents = [...s.diagnostics.disconnectEvents];

      for (const id in nextAnchors) {
        const a = nextAnchors[id];
        if (a.online && a.lastSeen && now - a.lastSeen > 25000) {
          nextAnchors[id] = { ...a, online: false };
          changed = true;
          nextDisconnectEvents.unshift({
            id: nanoid(6),
            ts: now,
            type: "lost",
            target: a.name,
            message: `เสา ${a.name} ขาดการติดต่อ (> 25s)`,
          });
        }
      }

      for (const id in nextSightings) {
        const sg = nextSightings[id];
        if (sg.status === "active" || sg.status === "sos" || sg.status === "assigned") {
          if (now - sg.lastSeen > 30000) {
            nextSightings[id] = { ...sg, status: "silent" };
            changed = true;
            nextDisconnectEvents.unshift({
              id: nanoid(6),
              ts: now,
              type: "lost",
              target: id,
              message: `Band ${id} ขาดการติดต่อ (> 30s)`,
            });
          }
        }
      }

      // Hub and handheld have no anchor/sighting entry, so log their
      // disconnects from the activity ledger.
      const swept = sweepActivity(s.diagnostics.activity, now);
      if (swept) {
        changed = true;
        for (const n of swept.wentOffline) {
          if (n.kind !== "hub" && n.kind !== "resq") continue;
          nextDisconnectEvents.unshift({
            id: nanoid(6),
            ts: now,
            type: "lost",
            target: n.label,
            message: `${n.label} ขาดการติดต่อ (เงียบ ${formatDuration(now - n.lastSeen)})`,
          });
        }
      }

      return changed ? {
        anchors: nextAnchors,
        sightings: nextSightings,
        diagnostics: {
          ...s.diagnostics,
          activity: swept?.activity ?? s.diagnostics.activity,
          disconnectEvents: nextDisconnectEvents.slice(0, 100),
        },
      } : s;
    });
  },
}), {
  name: "resq-storage",
  // Rehydrate from localStorage after mount (ClientLayout), not during module
  // load: the static HTML is rendered with the default state, and reading
  // localStorage before React hydrates made the first client render differ
  // from it (React error #418 on every page).
  skipHydration: true,
  storage: createJSONStorage(() => batchedStorage),
  onRehydrateStorage: () => (state) => {
    storeHydrated = true;
    if (state?.pathLoss.source === OLD_DEFAULT_SOURCE) state.setPathLoss(DEFAULT_PATH_LOSS);
  },
  // Mission data survives a reload: who was seen where, what happened, and
  // whether search mode was on, and the link diagnostics. The packet log is
  // rebuilt from the radio and is not saved.
  partialize: (state) => ({
    incident: state.incident,
    wristbands: state.wristbands,
    anchors: state.anchors,
    sightings: state.sightings,
    timeline: state.timeline,
    searchMode: state.searchMode,
    pathLoss: state.pathLoss,
    rangePoints: state.rangePoints,
    pillarCalPoints: state.pillarCalPoints,
    diagnostics: state.diagnostics,
  })
}));
