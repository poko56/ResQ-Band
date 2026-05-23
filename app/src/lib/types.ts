export type TriageLevel = "green" | "yellow" | "red" | "black";

export type WristbandStatus = "registered" | "active" | "sos" | "silent" | "rescued";

export interface LatLng {
  lat: number;
  lng: number;
}

export interface Wristband {
  id: string;
  name: string;
  owner?: string;
  role?: string;
  registeredAt: number;
}

export interface Anchor {
  id: string;
  name: string;
  position: LatLng;
  placedAt: number;
  online: boolean;
  lastSeen?: number;
}

export interface Sighting {
  wristbandId: string;
  lastSeen: number;
  position?: LatLng;
  rssiPerAnchor: Record<string, number>;
  triage: TriageLevel;
  heartRate: number;
  spo2: number;
  batteryPct: number;
  lastGForce: number;
  hopCount: number;
  status: WristbandStatus;
  foundByHandheld?: { position: LatLng; ts: number; handheldId: string };
}

export type TimelineEventType =
  | "anchor_placed"
  | "wristband_registered"
  | "sos_received"
  | "lost_contact"
  | "vitals_critical"
  | "found_by_handheld"
  | "marked_rescued";

export interface TimelineEvent {
  id: string;
  ts: number;
  type: TimelineEventType;
  wristbandId?: string;
  anchorId?: string;
  message: string;
}

export interface Incident {
  id: string;
  name: string;
  startedAt: number;
  endedAt?: number;
  centerPoint?: LatLng;
}

export interface SignalQuality {
  rssi: number;
  snr: number;
  zoneLabel: string;
}
