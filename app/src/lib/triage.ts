import type { TriageLevel } from "./types";

// Triage itself is decided on the band (include/ResQConfig.h thresholds) and
// arrives in each heartbeat; the store only overrides HR 0 -> black.

export const TRIAGE_COLORS: Record<TriageLevel, string> = {
  green:  "#5fb878",
  yellow: "#d4a235",
  red:    "#d04545",
  black:  "#2a2a2a",
};

export const TRIAGE_LABELS_TH: Record<TriageLevel, string> = {
  green: "ปลอดภัย",
  yellow: "เฝ้าระวัง",
  red: "วิกฤต",
  black: "ไม่พบชีพจร",
};
