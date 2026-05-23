"use client";

import { useEffect, useRef } from "react";
import { useResQ } from "./store";
import { startMockStream, stopMockStream } from "./mock";
import type { Sighting, TriageLevel } from "./types";

interface HubMessage {
  type: "hello" | "heartbeat" | "lora_rx" | "stats";
  hub_id?: string;
  ts?: number;
  packet?: WirePacket;
  rssi?: number;
  snr?: number;
}

interface WirePacket {
  device_id: string;
  packet_type: "heartbeat" | "sos" | "triage";
  sequence: number;
  triage: 0 | 1 | 2 | 3;
  heart_rate: number;
  spo2: number;
  battery_pct: number;
  last_g_force_x10: number;
  hop_count: number;
}

const TRIAGE_FROM_BYTE: Record<number, TriageLevel> = {
  0: "green",
  1: "yellow",
  2: "red",
  3: "black",
};

const HEARTBEAT_TIMEOUT_MS = 15_000;
const RECONNECT_DELAY_MS = 3_000;

export function useHubBridge() {
  const url = useResQ((s) => s.hub.url);
  const demoMode = useResQ((s) => s.hub.demoMode);
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const heartbeatWatcher = useRef<ReturnType<typeof setInterval> | null>(null);
  const closedByUs = useRef(false);

  useEffect(() => {
    if (demoMode) {
      startMockStream();
      return () => stopMockStream();
    }

    const { setHubState, notifyHubHeartbeat, incHubPackets, upsertSighting } = useResQ.getState();
    closedByUs.current = false;

    const connect = () => {
      setHubState("connecting");
      try {
        const ws = new WebSocket(url);
        wsRef.current = ws;

        ws.onopen = () => {
          setHubState("connected");
        };

        ws.onmessage = (event) => {
          try {
            const msg = JSON.parse(event.data) as HubMessage;
            if (msg.type === "hello" || msg.type === "heartbeat") {
              notifyHubHeartbeat(msg.hub_id);
            } else if (msg.type === "lora_rx" && msg.packet) {
              incHubPackets();
              const p = msg.packet;
              const sighting: Sighting = {
                wristbandId: p.device_id,
                lastSeen: Date.now(),
                rssiPerAnchor: msg.rssi !== undefined ? { hub: msg.rssi } : {},
                triage: TRIAGE_FROM_BYTE[p.triage] ?? "green",
                heartRate: p.heart_rate,
                spo2: p.spo2,
                batteryPct: p.battery_pct,
                lastGForce: p.last_g_force_x10 / 10,
                hopCount: p.hop_count,
                status: p.packet_type === "sos" ? "sos" : "active",
              };
              upsertSighting(sighting);
            }
          } catch (err) {
            console.warn("Hub message parse error", err);
          }
        };

        ws.onerror = () => {
          setHubState("error", "WebSocket error");
        };

        ws.onclose = () => {
          wsRef.current = null;
          if (!closedByUs.current) {
            setHubState("disconnected");
            reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY_MS);
          }
        };
      } catch (err) {
        setHubState("error", String(err));
        reconnectTimer.current = setTimeout(connect, RECONNECT_DELAY_MS);
      }
    };

    heartbeatWatcher.current = setInterval(() => {
      const { hub, setHubState: setState } = useResQ.getState();
      if (hub.state === "connected" && hub.lastHeartbeat && Date.now() - hub.lastHeartbeat > HEARTBEAT_TIMEOUT_MS) {
        setState("disconnected", "Heartbeat timeout");
        wsRef.current?.close();
      }
    }, 5000);

    connect();

    return () => {
      closedByUs.current = true;
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      if (heartbeatWatcher.current) clearInterval(heartbeatWatcher.current);
      wsRef.current?.close();
      wsRef.current = null;
      useResQ.getState().setHubState("disconnected");
    };
  }, [url, demoMode]);
}
