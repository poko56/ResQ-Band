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
import { initFirebaseSync } from "./sync";
import { applyHubEvent } from "./hubEvents";

const SERIAL_BAUD        = 115200;
const HEARTBEAT_WATCH_MS = 8000;       // mark disconnected if no event for this long
const RECONNECT_DELAY_MS = 2000;
const BOOT_WAIT_MS       = 20_000;     // wait up to 20s for first event after open
const BOOT_PING_EVERY_MS = 800;        // poke firmware while waiting
const KEEPALIVE_MS       = 5_000;      // ping firmware so its LED stays "host attached"

// ----------------------------------------------------------------------------
// Module-scoped port handles. Kept outside React so commands can be issued
// from anywhere via `sendCommand()` regardless of which component is mounted.
// ----------------------------------------------------------------------------
type AnyPort = any;  // Web Serial types not in standard lib.d.ts

let activePort: AnyPort | null = null;
let activeWriter: WritableStreamDefaultWriter<string> | null = null;
let readerLoopAborter: AbortController | null = null;
let heartbeatTimer: ReturnType<typeof setInterval> | null = null;
let bootPingTimer:    ReturnType<typeof setInterval> | null = null;
let bootDeadlineTimer: ReturnType<typeof setTimeout> | null = null;
let keepaliveTimer:    ReturnType<typeof setInterval> | null = null;

function stopBootPolling() {
  if (bootPingTimer)     { clearInterval(bootPingTimer); bootPingTimer = null; }
  if (bootDeadlineTimer) { clearTimeout(bootDeadlineTimer); bootDeadlineTimer = null; }
}

function startKeepalive() {
  if (keepaliveTimer) return;
  keepaliveTimer = setInterval(() => {
    if (useResQ.getState().hub.state === "connected") {
      void sendCommand({ c: "ping" });
    }
  }, KEEPALIVE_MS);
}
function stopKeepalive() {
  if (keepaliveTimer) { clearInterval(keepaliveTimer); keepaliveTimer = null; }
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

    // Keepalive so MainNode's RGB stays "host attached" (white heartbeat
    // every 1.5s instead of dimming back to plain green idle).
    startKeepalive();
  } catch (err) {
    stopBootPolling();
    stopKeepalive();
    useResQ.getState().setHubState("error", String((err as Error)?.message ?? err));
  }
}

export async function disconnectHub(): Promise<void> {
  try {
    readerLoopAborter?.abort();
    readerLoopAborter = null;
    if (heartbeatTimer) { clearInterval(heartbeatTimer); heartbeatTimer = null; }
    stopBootPolling();
    stopKeepalive();
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
  useResQ.getState().notifyHubHeartbeat();
  applyHubEvent(ev);
  if (ev.t === "hello" || ev.t === "pong") stopBootPolling();
}

// ----------------------------------------------------------------------------
// React glue
// ----------------------------------------------------------------------------
export function useHubBridge() {
  const demoMode = useResQ((s) => s.hub.demoMode);

  useEffect(() => {
    let unsubSync: () => void = () => {};
    initFirebaseSync().then((unsub) => {
      unsubSync = unsub;
    }).catch(console.error);
    if (demoMode) {
      startMockStream();
      return () => {
        stopMockStream();
        unsubSync();
      };
    }
    return () => {
      unsubSync();
    };
  }, [demoMode]);

  useEffect(() => {
    const sweepTimer = setInterval(() => {
      useResQ.getState().sweepStaleNodes();
    }, 1500);

    // Cleanup on app unmount
    return () => {
      clearInterval(sweepTimer);
      void disconnectHub();
    };
  }, []);
}
