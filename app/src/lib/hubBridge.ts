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
// Port lifecycle
//
// A port can only be closed once its streams are released, so we keep the
// reader / writer and their pipe promises and tear them down in order. The
// old code aborted an unused AbortController and then failed to close the
// still-locked port, so "Connect" afterwards hit "port already open" and
// only a page reload helped.
//
// Chrome remembers ports the user granted, so after a reload, a MainNode
// reset or a replug we reopen the same port without asking (auto-connect).
// Only an explicit Disconnect turns that off, until Connect is pressed again.
// ----------------------------------------------------------------------------
const ESPRESSIF_USB_VID = 0x303a;           // ESP32-S3 native USB (MainNode)
const MANUAL_OFF_KEY = "resq-hub-manual-disconnect";

let activeReader: ReadableStreamDefaultReader<string> | null = null;
let readableClosed: Promise<void> | null = null;
let writableClosed: Promise<void> | null = null;
let closing = false;
let autoRetryTimer: ReturnType<typeof setTimeout> | null = null;
let autoInFlight = false;   // several triggers can fire at once (load, connect event, demo off)

function manualOff(): boolean {
  try { return sessionStorage.getItem(MANUAL_OFF_KEY) === "1"; } catch { return false; }
}
function setManualOff(on: boolean) {
  try { on ? sessionStorage.setItem(MANUAL_OFF_KEY, "1") : sessionStorage.removeItem(MANUAL_OFF_KEY); } catch { /* storage blocked */ }
}

function stopTimers() {
  stopBootPolling();
  stopKeepalive();
  if (heartbeatTimer) { clearInterval(heartbeatTimer); heartbeatTimer = null; }
}

async function closePort(): Promise<void> {
  closing = true;
  stopTimers();
  try { await activeReader?.cancel(); } catch { /* already gone */ }
  try { await readableClosed; } catch { /* pipe aborted */ }
  try { await activeWriter?.close(); } catch { /* already gone */ }
  try { await writableClosed; } catch { /* pipe aborted */ }
  try { await activePort?.close(); } catch { /* unplugged or already closed */ }
  activePort = null;
  activeReader = null;
  activeWriter = null;
  readableClosed = null;
  writableClosed = null;
  closing = false;
}

// The read loop ended without us asking (USB unplugged, MainNode reset).
async function onPortLost(reason: string) {
  if (closing) return;
  await closePort();
  useResQ.getState().setHubState("disconnected", `${reason} — จะเชื่อมต่อใหม่อัตโนมัติเมื่อ MainNode กลับมา`);
  scheduleAutoConnect(2000);
}

async function openPort(port: AnyPort): Promise<void> {
  const s = useResQ.getState();
  s.setHubState("connecting");
  s.setHubInfo({ connectStartedAt: Date.now(), connectAttempts: 0 });

  await port.open({ baudRate: SERIAL_BAUD });
  activePort = port;

  // Outbound: TextEncoderStream -> port.writable
  const encoder = new TextEncoderStream();
  writableClosed = encoder.readable.pipeTo(port.writable).catch(() => {});
  activeWriter = encoder.writable.getWriter();

  // Inbound: port.readable -> TextDecoderStream -> reader
  const decoder = new TextDecoderStream();
  readableClosed = port.readable.pipeTo(decoder.writable).catch(() => {});
  activeReader = decoder.readable.getReader();
  readPortLoop(activeReader)
    .then(() => onPortLost("การเชื่อมต่อ USB ปิดลง"))
    .catch((err) => onPortLost(`USB ขัดข้อง: ${String(err?.message ?? err)}`));

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

  // Heartbeat watchdog: MainNode sends stats every second, so a long silence
  // means the link is stuck even if the port still looks open.
  if (heartbeatTimer) clearInterval(heartbeatTimer);
  heartbeatTimer = setInterval(() => {
    const { hub, setHubState } = useResQ.getState();
    if (hub.state !== "connected") return;
    if (hub.lastHeartbeat && Date.now() - hub.lastHeartbeat > HEARTBEAT_WATCH_MS) {
      setHubState("error", "ไม่มีสัญญาณจาก MainNode > 8s");
      void sendCommand({ c: "ping" });   // a live board answers and flips back to connected
    }
  }, 2000);

  // Keepalive so MainNode's RGB stays "host attached" (white heartbeat
  // every 1.5s instead of dimming back to plain green idle).
  startKeepalive();
}

// Pick a port the user already granted: prefer MainNode's Espressif USB.
async function grantedPort(): Promise<AnyPort | null> {
  const ports: AnyPort[] = await (navigator as any).serial.getPorts();
  if (ports.length === 0) return null;
  return ports.find((p) => p.getInfo?.().usbVendorId === ESPRESSIF_USB_VID) ?? (ports.length === 1 ? ports[0] : null);
}

function scheduleAutoConnect(delayMs: number) {
  if (autoRetryTimer) clearTimeout(autoRetryTimer);
  autoRetryTimer = setTimeout(() => { autoRetryTimer = null; void autoConnect(); }, delayMs);
}

export async function autoConnect(): Promise<void> {
  if (!isWebSerialSupported() || manualOff() || activePort || closing || autoInFlight) return;
  const s = useResQ.getState();
  if (s.hub.demoMode || s.hub.state === "connecting") return;
  autoInFlight = true;
  try {
    const port = await grantedPort();
    if (!port) return;   // nothing granted yet: the user has to press Connect once
    await openPort(port);
  } catch (err) {
    // Busy (another tab / esptool) or still booting: try again shortly.
    await closePort();
    useResQ.getState().setHubState("disconnected", `ยังเชื่อมต่ออัตโนมัติไม่ได้: ${String((err as Error)?.message ?? err)}`);
    scheduleAutoConnect(5000);
  } finally {
    autoInFlight = false;
  }
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
  setManualOff(false);
  try {
    const port: AnyPort = await (navigator as any).serial.requestPort();
    await closePort();   // drop any half-open previous session first
    await openPort(port);
  } catch (err) {
    await closePort();
    useResQ.getState().setHubState("error", String((err as Error)?.message ?? err));
  }
}

export async function disconnectHub(): Promise<void> {
  setManualOff(true);
  if (autoRetryTimer) { clearTimeout(autoRetryTimer); autoRetryTimer = null; }
  await closePort();
  useResQ.getState().setHubState("disconnected");
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
async function readPortLoop(reader: ReadableStreamDefaultReader<string>): Promise<void> {
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
    // Leaving demo: pick the real MainNode back up if it was connected before.
    scheduleAutoConnect(300);
    return () => {
      unsubSync();
    };
  }, [demoMode]);

  useEffect(() => {
    const sweepTimer = setInterval(() => {
      useResQ.getState().sweepStaleNodes();
    }, 1500);

    // Reconnect by itself: on page load, and whenever a granted serial device
    // (re)appears, e.g. MainNode rebooting or the cable being replugged.
    const serial = isWebSerialSupported() ? (navigator as any).serial : null;
    const onConnect = () => scheduleAutoConnect(1500);   // let MainNode boot first
    const onDisconnect = (e: Event) => {
      if (e.target === activePort) void onPortLost("MainNode ถูกถอด/รีเซ็ต");
    };
    serial?.addEventListener("connect", onConnect);
    serial?.addEventListener("disconnect", onDisconnect);
    void autoConnect();

    // Cleanup on app unmount (not a user Disconnect, so keep auto-connect on)
    return () => {
      clearInterval(sweepTimer);
      serial?.removeEventListener("connect", onConnect);
      serial?.removeEventListener("disconnect", onDisconnect);
      void closePort();
    };
  }, []);
}
