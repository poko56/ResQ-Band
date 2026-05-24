// ============================================================================
// WebSerial bridge to ResQ-MainNode.
//
// Wire protocol: line-delimited JSON over USB CDC (115200 baud).
// Outgoing events from MainNode are dispatched into the Zustand store;
// commands from the UI are encoded back through the writable stream.
//
// Chrome / Edge only - navigator.serial is unavailable elsewhere.
// The page must be served from https:// or http://localhost for the API
// to be exposed (an HTTPS Vercel deployment is what this project ships).
// ============================================================================

"use client";

import { useEffect } from "react";
import { useResQ, _registerSendCommand } from "./store";
import type { HubCommand, HubEvent } from "./types";
import { startMockStream, stopMockStream } from "./mock";

const SERIAL_BAUD        = 115200;
const HEARTBEAT_WATCH_MS = 8000;       // mark disconnected if no event for this long
const RECONNECT_DELAY_MS = 2000;
const BOOT_WAIT_MS       = 20_000;     // wait up to 20s for first event after open
const BOOT_PING_EVERY_MS = 800;        // poke firmware while waiting

// ----------------------------------------------------------------------------
// Module-scoped port handles. Kept outside React so commands can be issued
// from anywhere via `sendCommand()` regardless of which component is mounted.
// ----------------------------------------------------------------------------
type AnyPort = any;  // Web Serial types not in standard lib.d.ts

let activePort: AnyPort | null = null;
let activeWriter: WritableStreamDefaultWriter<string> | null = null;
let readerLoopAborter: AbortController | null = null;
let heartbeatTimer: ReturnType<typeof setInterval> | null = null;
let bootPingTimer:  ReturnType<typeof setInterval> | null = null;
let bootDeadlineTimer: ReturnType<typeof setTimeout> | null = null;

function stopBootPolling() {
  if (bootPingTimer)     { clearInterval(bootPingTimer); bootPingTimer = null; }
  if (bootDeadlineTimer) { clearTimeout(bootDeadlineTimer); bootDeadlineTimer = null; }
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
export function isWebSerialSupported(): boolean {
  return typeof window !== "undefined" && "serial" in (navigator as any);
}

export async function connectHub(): Promise<void> {
  if (!isWebSerialSupported()) {
    useResQ.getState().setHubState("error", "Browser ไม่รองรับ WebSerial — ใช้ Chrome / Edge");
    return;
  }
  try {
    useResQ.getState().setHubState("connecting");
    useResQ.getState().setHubInfo({ connectStartedAt: Date.now(), connectAttempts: 0 });

    const port: AnyPort = await (navigator as any).serial.requestPort();
    await port.open({ baudRate: SERIAL_BAUD });
    activePort = port;

    // Outbound: TextEncoderStream -> port.writable
    const encoder = new TextEncoderStream();
    encoder.readable.pipeTo(port.writable).catch(() => {/* swallow on close */});
    activeWriter = encoder.writable.getWriter();

    // Inbound: port.readable -> TextDecoderStream -> reader
    readerLoopAborter = new AbortController();
    readPortLoop(port).catch((err) => {
      useResQ.getState().setHubState("error", String(err?.message ?? err));
    });

    // Boot wait: poke firmware with `ping` so a freshly-rebooted MainNode
    // that already missed its boot-time `hello` still answers within a
    // second. We stop polling on the first event from the device, or after
    // BOOT_WAIT_MS, whichever comes first.
    stopBootPolling();
    bootPingTimer = setInterval(() => {
      if (useResQ.getState().hub.state === "connected") { stopBootPolling(); return; }
      const attempts = (useResQ.getState().hub.connectAttempts ?? 0) + 1;
      useResQ.getState().setHubInfo({ connectAttempts: attempts });
      void sendCommand({ c: "ping" });
    }, BOOT_PING_EVERY_MS);
    bootDeadlineTimer = setTimeout(() => {
      if (useResQ.getState().hub.state === "connecting") {
        useResQ.getState().setHubState(
          "error",
          `ไม่ตอบสนองภายใน ${BOOT_WAIT_MS / 1000}s - ลองกดปุ่ม RST ที่บอร์ดแล้วเชื่อมต่อใหม่`,
        );
      }
      stopBootPolling();
    }, BOOT_WAIT_MS);

    // Heartbeat watchdog: if we don't see any event for HEARTBEAT_WATCH_MS,
    // mark disconnected so the UI nudges the user.
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    heartbeatTimer = setInterval(() => {
      const { hub, setHubState } = useResQ.getState();
      if (hub.state !== "connected") return;
      if (hub.lastHeartbeat && Date.now() - hub.lastHeartbeat > HEARTBEAT_WATCH_MS) {
        setHubState("error", "ไม่มีสัญญาณจาก MainNode > 8s");
      }
    }, 2000);
  } catch (err) {
    stopBootPolling();
    useResQ.getState().setHubState("error", String((err as Error)?.message ?? err));
  }
}

export async function disconnectHub(): Promise<void> {
  try {
    readerLoopAborter?.abort();
    readerLoopAborter = null;
    if (heartbeatTimer) { clearInterval(heartbeatTimer); heartbeatTimer = null; }
    stopBootPolling();
    try { await activeWriter?.close(); } catch { /* ignore */ }
    activeWriter = null;
    try { await activePort?.close(); } catch { /* ignore */ }
    activePort = null;
    useResQ.getState().setHubState("disconnected");
  } catch (err) {
    console.warn("[hub] disconnect error", err);
  }
}

export async function sendCommand(cmd: HubCommand): Promise<void> {
  if (!activeWriter) {
    console.warn("[hub] sendCommand called but no active writer", cmd);
    return;
  }
  try {
    await activeWriter.write(JSON.stringify(cmd) + "\n");
  } catch (err) {
    console.warn("[hub] write failed", err);
  }
}

// Register sendCommand with the store so its actions can dispatch commands
// without an import cycle.
_registerSendCommand((cmd) => { void sendCommand(cmd as HubCommand); });

// ----------------------------------------------------------------------------
// Read loop
// ----------------------------------------------------------------------------
async function readPortLoop(port: AnyPort): Promise<void> {
  const decoder = new TextDecoderStream();
  port.readable.pipeTo(decoder.writable).catch(() => {/* closing */});
  const reader = decoder.readable.getReader();

  let buffer = "";
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      buffer += value;
      let nl;
      while ((nl = buffer.indexOf("\n")) >= 0) {
        const line = buffer.slice(0, nl).trim();
        buffer = buffer.slice(nl + 1);
        if (line.length === 0) continue;
        try {
          const ev = JSON.parse(line) as HubEvent;
          dispatchHubEvent(ev);
        } catch (err) {
          console.warn("[hub] parse fail", line, err);
        }
      }
    }
  } finally {
    try { reader.releaseLock(); } catch { /* ignore */ }
  }
}

// ----------------------------------------------------------------------------
// Event dispatcher
// ----------------------------------------------------------------------------
function dispatchHubEvent(ev: HubEvent): void {
  const s = useResQ.getState();
  s.notifyHubHeartbeat();

  switch (ev.t) {
    case "hello":
      s.setHubInfo({ mainId: ev.main_id, fw: ev.fw, cycleId: 0, loraReady: ev.lora_ready });
      s.setHubState("connected");
      stopBootPolling();
      break;

    case "stats":
      s.setHubInfo({ cycleId: ev.cycle, loraReady: ev.lora_ready });
      break;

    case "lora_init_failed":
      s.setHubInfo({ loraReady: false, loraLastError: ev.msg });
      break;

    case "lora_ready":
      s.setHubInfo({ loraReady: true, loraLastError: undefined });
      break;

    case "beacon":
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
      s.applyAssignment(ev);
      break;

    case "found":
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
        });
      }
      s.setHubState("connected");
      stopBootPolling();
      break;

    case "ring_ack":
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

// ----------------------------------------------------------------------------
// React glue
// ----------------------------------------------------------------------------
export function useHubBridge() {
  const demoMode = useResQ((s) => s.hub.demoMode);

  useEffect(() => {
    if (demoMode) {
      startMockStream();
      return () => stopMockStream();
    }
    return () => {
      // Only cleanup if we actually had something running
    };
  }, [demoMode]);

  useEffect(() => {
    // Cleanup on app unmount
    return () => {
      void disconnectHub();
    };
  }, []);
}
