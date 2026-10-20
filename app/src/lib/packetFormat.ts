// ============================================================================
// Turns a MainNode event into a one-line description for the packet console
// and TDMA view: what kind it is, which device it came from, and the key
// numbers.
// ============================================================================

import type { HubEvent, Wristband } from "./types";

export type PacketKind =
  | "band" | "sos" | "pin" | "beacon" | "assignment" | "found" | "ring"
  | "stats" | "system" | "error";

export const KIND_META: Record<PacketKind, { label: string; color: string }> = {
  band:       { label: "Heartbeat", color: "#3987e5" },
  sos:        { label: "SOS",       color: "#e66767" },
  pin:        { label: "Pin report", color: "#199e70" },
  beacon:     { label: "Beacon",    color: "#9085e9" },
  assignment: { label: "Assign",    color: "#c98500" },
  found:      { label: "Found",     color: "#d55181" },
  ring:       { label: "Ring",      color: "#d95926" },
  stats:      { label: "Stats",     color: "#6a6a6a" },
  system:     { label: "System",    color: "#909090" },
  error:      { label: "Error",     color: "#d04545" },
};

export interface PacketInfo {
  kind: PacketKind;
  source: string;    // human label of the sender
  deviceKey?: string; // "band:<ID>", "pin:<idx>", "hub", "resq:<ID>"
  summary: string;
}

export function describePacket(ev: HubEvent, wristbands: Record<string, Wristband> = {}): PacketInfo {
  const bandName = (id: string) => {
    const up = id.toUpperCase();
    return wristbands[up]?.name ? `${wristbands[up].name} (${up})` : up;
  };
  switch (ev.t) {
    case "band": {
      const sos = ev.ptype !== "HEARTBEAT";
      return {
        kind: sos ? "sos" : "band",
        source: bandName(ev.id),
        deviceKey: `band:${ev.id.toUpperCase()}`,
        summary: `${ev.ptype} seq ${ev.seq} · HR ${ev.hr || "--"} · ${ev.rssi} dBm / SNR ${ev.snr}`,
      };
    }
    case "pin_sighting":
      return {
        kind: "pin",
        source: `เสา ${ev.pin + 1}`,
        deviceKey: `pin:${ev.pin}`,
        summary: `ได้ยิน ${ev.sightings.length} กำไล${ev.sightings.length ? ": " + ev.sightings.map((s) => `${s.band.toUpperCase()} ${s.rssi}dBm`).join(", ") : ""} · ถึง hub ${ev.rssi} dBm`,
      };
    case "beacon":
      return { kind: "beacon", source: "MainNode", deviceKey: "hub", summary: `cycle ${ev.cycle}${ev.flags & 1 ? " · EMERGENCY" : ""}` };
    case "assignment":
      return { kind: "assignment", source: "MainNode", deviceKey: "hub", summary: `→ ${bandName(ev.band)} · score ${ev.score} · ${ev.reason}` };
    case "found":
      return { kind: "found", source: `ResQ-Node ${ev.node_id}`, deviceKey: `resq:${ev.node_id}`, summary: `${bandName(ev.band)} · ${ev.outcome} · ${ev.rssi} dBm` };
    case "ring_ack":
      return { kind: "ring", source: bandName(ev.band), deviceKey: `band:${ev.band.toUpperCase()}`, summary: `RING_ACK status ${ev.status}` };
    case "stats":
      return { kind: "stats", source: "MainNode", deviceKey: "hub", summary: `cycle ${ev.cycle} · rx ${ev.rx} · dropped ${ev.dropped} · heap ${ev.heap}` };
    case "err":
    case "fatal":
      return { kind: "error", source: "MainNode", deviceKey: "hub", summary: ev.msg };
    case "pin_join_req":
      return { kind: "system", source: `Pin ${ev.pin_id}`, summary: "ขอเข้าร่วม (ยังไม่มี slot)" };
    default:
      return { kind: "system", source: "MainNode", deviceKey: "hub", summary: ev.t };
  }
}

// MainNode millis() timestamp carried by the event, if any.
export function mainTs(ev: HubEvent): number | undefined {
  return "ts" in ev && typeof (ev as { ts?: unknown }).ts === "number" ? (ev as { ts: number }).ts : undefined;
}
