import type { Sighting, TriageLevel } from "./types";

// Triage itself is decided on the band (include/ResQConfig.h thresholds) and
// arrives in each heartbeat.

export const TRIAGE_COLORS: Record<TriageLevel, string> = {
  green:  "#5fb878",
  yellow: "#d4a235",
  red:    "#d04545",
  black:  "#2a2a2a",
  nodata: "#6b7280",
};

export const TRIAGE_LABELS_TH: Record<TriageLevel, string> = {
  green: "ปลอดภัย",
  yellow: "เฝ้าระวัง",
  red: "วิกฤต",
  black: "ไม่พบชีพจร",
  nodata: "ไม่มีข้อมูลชีพ",
};

// What a band's badge should say. A band that has gone quiet keeps its last
// triage in the store, but showing "no pulse" for a band we simply can't hear
// is misleading, so silence and a missing sensor get their own labels.
export function triageBadge(s: Pick<Sighting, "triage" | "status" | "sensorMissing">): { label: string; color: string } {
  if (s.status === "silent") return { label: "ขาดการติดต่อ", color: "#4b5563" };
  if (s.sensorMissing) return { label: "ไม่มีเซนเซอร์", color: TRIAGE_COLORS.nodata };
  return { label: TRIAGE_LABELS_TH[s.triage], color: TRIAGE_COLORS[s.triage] };
}
