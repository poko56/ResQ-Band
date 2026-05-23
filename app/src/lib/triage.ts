import type { TriageLevel } from "./types";

export interface Vitals {
  heartRate: number;
  spo2: number;
  lastGForce: number;
  silentMs: number;
}

export function classifyTriage(v: Vitals): TriageLevel {
  if (v.heartRate === 0 || v.silentMs > 30 * 60_000) return "black";

  const criticalHR = v.heartRate < 40 || v.heartRate > 140;
  const criticalSpO2 = v.spo2 > 0 && v.spo2 < 85;
  const majorImpact = Math.abs(v.lastGForce) > 80;

  if (criticalHR || criticalSpO2 || majorImpact) return "red";

  const elevatedHR = v.heartRate > 110 || v.heartRate < 55;
  const lowSpO2 = v.spo2 < 94;
  if (elevatedHR || lowSpO2) return "yellow";

  return "green";
}

export const TRIAGE_COLORS: Record<TriageLevel, string> = {
  green: "#22c55e",
  yellow: "#eab308",
  red: "#ef4444",
  black: "#1f2937",
};

export const TRIAGE_LABELS_TH: Record<TriageLevel, string> = {
  green: "ปลอดภัย",
  yellow: "เฝ้าระวัง",
  red: "วิกฤต",
  black: "ไม่ตอบสนอง",
};
