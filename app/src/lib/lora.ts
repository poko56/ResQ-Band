// ============================================================================
// LoRa radio + TDMA constants mirrored from include/ResQConfig.h, and the
// helpers Dev Tools uses to place packets in their TDMA slots.
// ============================================================================

export const LORA = {
  sf: 9,
  bandwidthHz: 125_000,
  codingRate: 5,          // 4/5
  preambleSymbols: 8,
  crc: true,
  // Typical SX1278 receive sensitivity at SF9 / 125 kHz (datasheet).
  sensitivityDbm: -129,
};

export const TDMA_CYCLE_MS = 10_000;
export const TDMA_SLOT_MS = 1_000;
export const TDMA_SLOTS = 10;
export const SLOT_TX_OFFSET_MS = 50;   // devices wait this long into their slot

export const SLOT_LABELS: string[] = [
  "Beacon", "Band 1", "Band 2", "เสา 1", "เสา 2", "เสา 3", "เสา 4", "Main cmd", "ResQ-Node", "Emergency",
];

export const PAYLOAD_BYTES = {
  BEACON: 18,
  SOS: 18,          // HEARTBEAT / SOS_*
  PIN_SIGHTING: 47,
  ASSIGNMENT: 24,
  FOUND: 20,
  RING_CMD: 14,
  RING_ACK: 10,
};

// Semtech AN1200.13 time-on-air for an explicit-header packet.
export function airtimeMs(payloadBytes: number, sf = LORA.sf, bw = LORA.bandwidthHz, cr = LORA.codingRate): number {
  const tSym = (Math.pow(2, sf) / bw) * 1000;
  const lowDr = tSym > 16 ? 1 : 0;
  const crc = LORA.crc ? 1 : 0;
  const header = 0;   // 0 = explicit header present
  const num = 8 * payloadBytes - 4 * sf + 28 + 16 * crc - 20 * header;
  const payloadSym = 8 + Math.max(Math.ceil(num / (4 * (sf - 2 * lowDr))) * cr, 0);
  return (LORA.preambleSymbols + 4.25 + payloadSym) * tSym;
}

// Which slot a device is supposed to transmit in (undefined = not fixed).
export function expectedSlot(kind: "beacon" | "pin" | "main_cmd" | "resq" | "band", pinIndex?: number): number | undefined {
  switch (kind) {
    case "beacon":   return 0;
    case "pin":      return pinIndex === undefined ? undefined : 3 + pinIndex;
    case "main_cmd": return 7;
    case "resq":     return 8;
    default:         return undefined;   // band index isn't carried in the packet
  }
}
