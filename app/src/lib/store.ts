import { create } from "zustand";
import { nanoid } from "nanoid";
import type {
  Anchor,
  AssignReason,
  BandStatus,
  HubStatus,
  Incident,
  LatLng,
  PinRssi,
  Sighting,
  TimelineEvent,
  TimelineEventType,
  TriageLevel,
  Wristband,
} from "./types";

type PlacementMode = "none" | "anchor";

const TRIAGE_FROM_BYTE: Record<number, TriageLevel> = { 0: "green", 1: "yellow", 2: "red", 3: "black" };

interface ResQState {
  incident: Incident;
  wristbands: Record<string, Wristband>;
  anchors: Record<string, Anchor>;       // keyed by anchor.id (Pin device hex when known)
  sightings: Record<string, Sighting>;
  timeline: TimelineEvent[];
  placementMode: PlacementMode;
  searchMode: boolean;
  hub: HubStatus;

  // --- Hub link wiring (called from hubBridge.ts) -------------------------
  setHubState: (state: HubStatus["state"], error?: string) => void;
  setHubInfo: (partial: Partial<HubStatus>) => void;
  notifyHubHeartbeat: () => void;
  incHubPackets: () => void;
  incHubDropped: () => void;
  setDemoMode: (enabled: boolean) => void;

  // --- Roster ops --------------------------------------------------------
  registerWristband: (input: Omit<Wristband, "registeredAt" | "id"> & { id?: string }) => Wristband;
  removeWristband: (id: string) => void;

  placeAnchor: (position: LatLng, name?: string) => Anchor;
  removeAnchor: (id: string) => void;
  renameAnchor: (id: string, name: string) => void;
  setAnchorPosition: (id: string, position: LatLng) => void;

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

function statusFromPacketType(ptype: string, prev?: BandStatus): BandStatus {
  if (ptype === "SOS_TAP" || ptype === "SOS_FALL") return "sos";
  if (prev === "rescued" || prev === "deceased") return prev;
  if (prev === "assigned") return "assigned";
  return "active";
}

// Forward decl - filled in by hubBridge so the store doesn't import it (no cycle).
let _sendCommand: ((cmd: object) => void) | null = null;
export function _registerSendCommand(fn: (cmd: object) => void) {
  _sendCommand = fn;
}

export const useResQ = create<ResQState>((set, get) => ({
  incident: defaultIncident(),
  wristbands: {},
  anchors: {},
  sightings: {},
  timeline: [],
  placementMode: "none",
  searchMode: false,
  hub: defaultHub(),

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
      name: name ?? `Pin-${pinIndex}`,
      position,
      placedAt: Date.now(),
      online: false,
      lastSeen: undefined,
    };
    set((s) => ({ anchors: { ...s.anchors, [id]: anchor }, placementMode: "none" }));
    get().pushEvent("anchor_placed",
      `วางเสา ${anchor.name} (slot ${pinIndex}) ที่ ${position.lat.toFixed(5)}, ${position.lng.toFixed(5)}`,
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

  upsertSightingFromBand: (ev) => {
    const triage = TRIAGE_FROM_BYTE[ev.triage] ?? "green";
    set((s) => {
      const prev = s.sightings[ev.id];
      const status = statusFromPacketType(ev.ptype, prev?.status);
      const next: Sighting = {
        wristbandId: ev.id,
        lastSeen: Date.now(),
        position: prev?.position,
        rssiPerPin: prev?.rssiPerPin ?? {},
        triage,
        heartRate: ev.hr,
        spo2: ev.spo2,
        batteryPct: ev.batt,
        lastGForce: ev.g_x10 / 10,
        hopCount: 0,
        status,
        priorityScore: prev?.priorityScore,
        manualBoost: prev?.manualBoost,
        assignedAt: prev?.assignedAt,
      };
      const searchMode = s.searchMode || status === "sos";
      return { sightings: { ...s.sightings, [ev.id]: next }, searchMode };
    });
    if (ev.ptype === "SOS_TAP" || ev.ptype === "SOS_FALL") {
      const wb = get().wristbands[ev.id];
      const who = wb?.name ?? ev.id;
      get().pushEvent(
        ev.ptype === "SOS_FALL" ? "fall_received" : "sos_received",
        `${who}: ${ev.ptype === "SOS_FALL" ? "ตรวจพบการล้ม" : "กดเคาะขอความช่วยเหลือ"} (HR ${ev.hr}, SpO₂ ${ev.spo2})`,
        { wristbandId: ev.id },
      );
    }
  },

  applyPinSighting: (ev) => {
    // Update anchor online state
    set((s) => {
      const nextAnchors = { ...s.anchors };
      const matched = Object.values(s.anchors).find((a) => a.pinIndex === ev.pin);
      if (matched) {
        nextAnchors[matched.id] = { ...matched, online: true, lastSeen: Date.now(),
          id: matched.id === ev.pin_id ? matched.id : matched.id /* keep slot id */ };
      }
      // Update RSSI per pin on each band's sighting
      const nextSightings = { ...s.sightings };
      for (const sg of ev.sightings) {
        const bandId = sg.band.toUpperCase();
        const prev = nextSightings[bandId];
        const rssiEntry: PinRssi = { pinIndex: ev.pin, rssi: sg.rssi, snr: sg.snr, ageMs: sg.age_ms };
        const rssiPerPin = { ...(prev?.rssiPerPin ?? {}), [ev.pin]: rssiEntry };
        // Infer position: best RSSI pin -> use its anchor position
        let position = prev?.position;
        const bestPin = Object.values(rssiPerPin).reduce((best, p) => (!best || p.rssi > best.rssi ? p : best), undefined as PinRssi | undefined);
        if (bestPin) {
          const anchor = Object.values(s.anchors).find((a) => a.pinIndex === bestPin.pinIndex);
          if (anchor) position = anchor.position;
        }
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
      return { anchors: nextAnchors, sightings: nextSightings };
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
    const status: BandStatus = ev.outcome === "deceased" ? "deceased" : "rescued";
    set((s) => {
      const prev = s.sightings[bandId];
      if (!prev) return s;
      return {
        sightings: {
          ...s.sightings,
          [bandId]: {
            ...prev,
            status,
            rescuedAt: Date.now(),
            rescuerNodeId: ev.node_id,
            finalRssi: ev.rssi,
            outcome: ev.outcome as Sighting["outcome"],
          },
        },
      };
    });
    const wb = get().wristbands[bandId];
    get().pushEvent("found_by_handheld",
      `กู้ภัยพบ ${wb?.name ?? bandId} แล้ว (outcome: ${ev.outcome})`,
      { wristbandId: bandId });
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
}));
