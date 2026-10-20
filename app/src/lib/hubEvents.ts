// ============================================================================
// Applies one MainNode event to the store. Shared by the WebSerial bridge and
// the demo stream so both drive exactly the same state (and the packet log).
// ============================================================================

import { useResQ } from "./store";
import type { HubEvent } from "./types";

export function applyHubEvent(ev: HubEvent): void {
  const s = useResQ.getState();
  s.logPacket(ev);

  switch (ev.t) {
    case "hello":
      s.setHubInfo({ mainId: ev.main_id, fw: ev.fw, cycleId: 0, loraReady: ev.lora_ready, buzzerMuted: ev.muted });
      s.setHubState("connected");
      break;

    case "stats":
      s.setHubInfo({
        cycleId: ev.cycle,
        loraReady: ev.lora_ready,
        buzzerMuted: ev.muted ?? s.hub.buzzerMuted,
        loraPacketsReceived: ev.rx,
        loraPacketsDropped: ev.dropped,
      });
      s.recordHubStats({
        rx: ev.rx,
        dropped: ev.dropped,
        uptimeS: ev.uptime_s,
        cycle: ev.cycle,
        heap: ev.heap,
      });
      break;

    case "buzzer":
      s.setHubInfo({ buzzerMuted: ev.muted });
      break;

    case "lora_init_failed":
      s.setHubInfo({ loraReady: false, loraLastError: ev.msg });
      break;

    case "lora_ready":
      s.setHubInfo({ loraReady: true, loraLastError: undefined });
      break;

    case "wifi_status":
      s.setHubInfo({ wifi: { connected: ev.connected, ssid: ev.ssid, ip: ev.ip, rssi: ev.rssi } });
      break;

    case "ota_status":
      s.setHubInfo({
        ota: {
          stage:     ev.stage,
          current:   ev.current ?? s.hub.ota?.current,
          latest:    ev.latest  ?? s.hub.ota?.latest,
          available: ev.available ?? s.hub.ota?.available,
          url:       ev.url     ?? s.hub.ota?.url,
          msg:       ev.msg,
          updatedAt: Date.now(),
        },
      });
      break;

    case "pin_button":
      s.registerAnchorId(ev.pin_id, ev.slot);
      s.setPlacementMode(ev.pin_id);
      break;

    case "pin_join_req":
      s.addUnassignedAnchor(ev.pin_id);
      break;

    case "beacon":
      s.recordActivity({ key: "hub", kind: "hub", label: "MainNode", type: "BEACON" });
      s.setHubInfo({ cycleId: ev.cycle, emergencyMode: (ev.flags & 0x01) !== 0 });
      break;

    case "band":
      s.incHubPackets();
      s.upsertSightingFromBand(ev);
      break;

    case "pin_sighting":
      s.incHubPackets();
      s.applyPinSighting(ev);
      break;

    case "assignment":
      s.recordActivity({ key: "hub", kind: "hub", label: "MainNode", type: "ASSIGNMENT" });
      s.applyAssignment(ev);
      break;

    case "found":
      s.recordActivity({ key: `resq:${ev.node_id}`, kind: "resq", label: `ResQ-Node ${ev.node_id}`, type: "FOUND" });
      s.applyFound(ev);
      break;

    case "pong":
      // Treat pong as hello-equivalent: it carries the same identity payload
      // so a late-attaching client still learns mainId / fw / cycle without
      // waiting up to TDMA_CYCLE_MS for a beacon to roll around.
      if (ev.main_id || ev.fw || ev.lora_ready !== undefined) {
        s.setHubInfo({
          mainId:    ev.main_id ?? s.hub.mainId,
          fw:        ev.fw      ?? s.hub.fw,
          cycleId:   ev.cycle   ?? s.hub.cycleId,
          loraReady: ev.lora_ready,
          buzzerMuted: ev.muted ?? s.hub.buzzerMuted,
        });
      }
      s.setHubState("connected");
      break;

    case "ring_ack": {
      const id = ev.band.toUpperCase();
      const name = s.wristbands[id]?.name;
      s.recordActivity({ key: `band:${id}`, kind: "band", label: name ? `${name} (${id})` : `Band ${id}`, type: "RING_ACK" });
      break;
    }

    case "ack":
      // diagnostic - nothing to do for now
      break;

    case "err":
    case "fatal":
      console.warn("[hub] err event", ev);
      s.setHubState("error", ev.msg);
      break;
  }
}
