import { create } from "zustand";
import { nanoid } from "nanoid";
import type {
  Anchor,
  Incident,
  LatLng,
  Sighting,
  TimelineEvent,
  TimelineEventType,
  Wristband,
} from "./types";

type PlacementMode = "none" | "anchor";

export type HubConnectionState = "disconnected" | "connecting" | "connected" | "error";

export interface HubStatus {
  url: string;
  state: HubConnectionState;
  lastHeartbeat?: number;
  lastError?: string;
  hubId?: string;
  loraPacketsReceived: number;
  demoMode: boolean;
}

const DEFAULT_HUB_URL =
  typeof window !== "undefined"
    ? (localStorage.getItem("resq.hubUrl") ?? `ws://${window.location.hostname}:81/ws`)
    : "ws://192.168.1.100:81/ws";

interface ResQState {
  incident: Incident;
  wristbands: Record<string, Wristband>;
  anchors: Record<string, Anchor>;
  sightings: Record<string, Sighting>;
  timeline: TimelineEvent[];
  placementMode: PlacementMode;
  hub: HubStatus;

  setHubUrl: (url: string) => void;
  setHubState: (state: HubConnectionState, error?: string) => void;
  notifyHubHeartbeat: (hubId?: string) => void;
  incHubPackets: () => void;
  setDemoMode: (enabled: boolean) => void;

  setPlacementMode: (mode: PlacementMode) => void;

  registerWristband: (input: Omit<Wristband, "id" | "registeredAt"> & { id?: string }) => Wristband;
  removeWristband: (id: string) => void;

  placeAnchor: (position: LatLng, name?: string) => Anchor;
  removeAnchor: (id: string) => void;

  upsertSighting: (sighting: Sighting) => void;
  markRescued: (wristbandId: string) => void;

  pushEvent: (type: TimelineEventType, message: string, refs?: { wristbandId?: string; anchorId?: string }) => void;

  resetIncident: () => void;
}

const defaultIncident = (): Incident => ({
  id: nanoid(8),
  name: "ภารกิจไม่มีชื่อ",
  startedAt: Date.now(),
});

export const useResQ = create<ResQState>((set, get) => ({
  incident: defaultIncident(),
  wristbands: {},
  anchors: {},
  sightings: {},
  timeline: [],
  placementMode: "none",
  hub: {
    url: DEFAULT_HUB_URL,
    state: "disconnected",
    loraPacketsReceived: 0,
    demoMode: false,
  },

  setHubUrl: (url) => {
    if (typeof window !== "undefined") localStorage.setItem("resq.hubUrl", url);
    set((s) => ({ hub: { ...s.hub, url } }));
  },

  setHubState: (state, error) =>
    set((s) => ({ hub: { ...s.hub, state, lastError: error } })),

  notifyHubHeartbeat: (hubId) =>
    set((s) => ({ hub: { ...s.hub, lastHeartbeat: Date.now(), hubId: hubId ?? s.hub.hubId, state: "connected" } })),

  incHubPackets: () =>
    set((s) => ({ hub: { ...s.hub, loraPacketsReceived: s.hub.loraPacketsReceived + 1 } })),

  setDemoMode: (enabled) =>
    set((s) => ({ hub: { ...s.hub, demoMode: enabled } })),

  setPlacementMode: (mode) => set({ placementMode: mode }),

  registerWristband: (input) => {
    const id = input.id ?? nanoid(8).toUpperCase();
    const wb: Wristband = {
      id,
      name: input.name,
      owner: input.owner,
      role: input.role,
      registeredAt: Date.now(),
    };
    set((s) => ({ wristbands: { ...s.wristbands, [id]: wb } }));
    get().pushEvent("wristband_registered", `ลงทะเบียนกำไล ${wb.name} (${id})`, { wristbandId: id });
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
    const id = nanoid(6).toUpperCase();
    const anchor: Anchor = {
      id,
      name: name ?? `Anchor-${id}`,
      position,
      placedAt: Date.now(),
      online: true,
      lastSeen: Date.now(),
    };
    set((s) => ({ anchors: { ...s.anchors, [id]: anchor }, placementMode: "none" }));
    get().pushEvent("anchor_placed", `วางเสา ${anchor.name} ที่ ${position.lat.toFixed(5)}, ${position.lng.toFixed(5)}`, { anchorId: id });
    return anchor;
  },

  removeAnchor: (id) => {
    set((s) => {
      const next = { ...s.anchors };
      delete next[id];
      return { anchors: next };
    });
  },

  upsertSighting: (sighting) => {
    set((s) => ({ sightings: { ...s.sightings, [sighting.wristbandId]: sighting } }));
  },

  markRescued: (wristbandId) => {
    set((s) => {
      const existing = s.sightings[wristbandId];
      if (!existing) return s;
      return {
        sightings: {
          ...s.sightings,
          [wristbandId]: { ...existing, status: "rescued" },
        },
      };
    });
    const wb = get().wristbands[wristbandId];
    get().pushEvent("marked_rescued", `กู้ภัยช่วย ${wb?.name ?? wristbandId} ออกมาได้แล้ว`, { wristbandId });
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
    });
  },
}));
