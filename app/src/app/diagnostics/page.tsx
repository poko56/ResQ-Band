import { Metadata } from "next";
import { DevDiagnostics } from "@/components/Diagnostics/DevDiagnostics";

export const metadata: Metadata = {
  title: "Dev Tools - Link Diagnostics | ResQ-Band",
  description: "Real-time link quality, RSSI/SNR signal tracking, and packet drop diagnostics between Pillars and Wristbands",
};

export default function DiagnosticsPage() {
  return <DevDiagnostics />;
}
