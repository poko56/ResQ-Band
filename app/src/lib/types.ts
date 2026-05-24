// ============================================================================
// ResQ-Band v2 web types - mirror MainNode JSON event schema (see main_node/main.cpp)
// ============================================================================

export type TriageLevel = "green" | "yellow" | "red" | "black";

export type BandStatus =
  | "registered"   // entered into roster, no signal yet
  | "active"       // healthy heartbeats
  | "silent"       // heartbeat missed > 60s
  | "sos"          // SOS_TAP or SOS_FALL received
  | "assigned"     // a rescuer is on their way
  | "rescued"      // ResQ-Node confirmed extraction
  | "deceased";    // outcome from FoundPacket

export type AssignReason =
  | "vitals_critical"
  | "fall_detected"
  | "tap_sos"
  | "silent_too_long"
  | "manual_override"
  | "battery_low";

export type FoundOutcome = "rescued" | "deceased" | "not_found" | "needs_backup";

export interface LatLng {
  lat: number;
  lng: number;
}

// ----------------------------------------------------------------------------
// Registration data the operator enters before deployment
// ----------------------------------------------------------------------------
export type BandRole = "worker" | "engineer" | "guest" | "vip";

export interface BandMedical {
  bloodType?: string;        // "A+", "O-", etc.
  allergies?: string[];      // ["penicillin", "shellfish"]
  conditions?: string[];     // ["diabetes", "asthma", "heart disease"]
  medications?: string[];    // ["insulin", "epinephrine pen"]
}

export interface BandEmergencyContact {
  name: string;
  phone: string;
  relation?: string;
}

export interface Wristband {
  id: string;                // Band device hex ID (8-char uppercase)
  name: string;
  age?: number;
  gender?: "M" | "F" | "other";
  role?: BandRole;
  photoDataUrl?: string;     // base64 image (browser-side only)
  idCardNumber?: string;
  medical?: BandMedical;
  emergencyContact?: BandEmergencyContact;
  registeredAt: number;
}

// ----------------------------------------------------------------------------
// Pin (anchor) - tower placed manually on the map
// ----------------------------------------------------------------------------
export interface Anchor {
  id: string;                // Pin device hex ID once known, else internal id
  pinIndex: number;          // 0..3 - which physical pin this is
  name: string;              // human-friendly label
  position: LatLng;          // operator-placed coordinates
  placedAt: number;
  online: boolean;
  lastSeen?: number;
}

// ----------------------------------------------------------------------------
// Live state per Band (assembled from MainNode events)
// ----------------------------------------------------------------------------
export interface PinRssi {
  pinIndex: number;
  rssi: number;
  snr: number;
  ageMs: number;
}

export interface Sighting {
  wristbandId: string;
  lastSeen: number;
  position?: LatLng;          // inferred from best-RSSI pin position
  rssiPerPin: Record<number, PinRssi>;
  triage: TriageLevel;
  heartRate: number;
  spo2: number;
  batteryPct: number;
  lastGForce: number;         // in g (already divided by 10)
  hopCount: number;
  status: BandStatus;
  lastReason?: AssignReason;
  priorityScore?: number;
  manualBoost?: number;
  assignedAt?: number;
  rescuedAt?: number;
  rescuerNodeId?: string;
  finalRssi?: number;
  outcome?: FoundOutcome;
}

// ----------------------------------------------------------------------------
// Timeline events shown in the activity feed
// ----------------------------------------------------------------------------
export type TimelineEventType =
  | "hub_connected"
  | "hub_disconnected"
  | "anchor_placed"
  | "wristband_registered"
  | "sos_received"
  | "fall_received"
  | "lost_contact"
  | "vitals_critical"
  | "assignment_dispatched"
  | "found_by_handheld"
  | "marked_rescued"
  | "manual_priority";

export interface TimelineEvent {
  id: string;
  ts: number;
  type: TimelineEventType;
  wristbandId?: string;
  anchorId?: string;
  message: string;
}

// ----------------------------------------------------------------------------
// Incident metadata
// ----------------------------------------------------------------------------
export interface Incident {
  id: string;
  name: string;
  startedAt: number;
  endedAt?: number;
  centerPoint?: LatLng;
}

// ----------------------------------------------------------------------------
// Hub link
// ----------------------------------------------------------------------------
export type HubConnectionState = "disconnected" | "connecting" | "connected" | "error";

export interface HubStatus {
  state: HubConnectionState;
  lastHeartbeat?: number;
  lastError?: string;
  mainId?: string;
  fw?: string;
  cycleId?: number;
  loraPacketsReceived: number;
  loraPacketsDropped: number;
  demoMode: boolean;
  emergencyMode: boolean;
  loraReady?: boolean;        // MainNode reports whether SX1278 SPI is alive
  loraLastError?: string;     // last lora_init_failed message
  connectStartedAt?: number;  // ms when current "connecting" phase began
  connectAttempts?: number;   // ping attempts sent so far during boot wait
}

// ----------------------------------------------------------------------------
// Wire protocol (MainNode -> Web)
// ----------------------------------------------------------------------------
export type HubEvent =
  | { t: "hello"; fw: string; board: string; main_id: string; lora_mhz: number; sf: number; tdma_cycle_ms: number; lora_ready?: boolean; ts: number }
  | { t: "stats"; cycle: number; uptime_s: number; rx: number; dropped: number; bands: number; heap: number; lora_ready?: boolean; ts: number }
  | { t: "beacon"; cycle: number; flags: number; ts: number }
  | { t: "lora_init_failed"; msg: string; pins?: Record<string, number>; ts: number }
  | { t: "lora_ready"; msg: string; ts: number }
  | { t: "band"; id: string; ptype: string; seq: number; triage: number; hr: number; spo2: number; batt: number; g_x10: number; rssi: number; snr: number; ts: number }
  | { t: "pin_sighting"; pin: number; pin_id: string; sightings: { band: string; rssi: number; snr: number; age_ms: number }[]; rssi: number; snr: number; ts: number }
  | { t: "assignment"; band: string; score: number; pin: number; rssi: number; triage: number; reason: AssignReason; ts: number }
  | { t: "found"; node_id: string; band: string; outcome: FoundOutcome; rssi: number; ts: number }
  | { t: "ring_ack"; band: string; status: number; ts: number }
  | { t: "pong"; main_id?: string; fw?: string; board?: string; cycle?: number; uptime_s?: number; lora_ready?: boolean; tdma_cycle_ms?: number; ts: number }
  | { t: "ack"; c: string }
  | { t: "err"; msg: string; c?: string }
  | { t: "fatal"; msg: string };

export type HubCommand =
  | { c: "ring_band"; band: string; duration_ms?: number; freq?: number; pattern?: number }
  | { c: "manual_priority"; band: string; boost: number }
  | { c: "mark_rescued"; band: string }
  | { c: "clear_alarm" }
  | { c: "ping" };
