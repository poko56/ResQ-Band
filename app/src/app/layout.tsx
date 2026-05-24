import type { Metadata, Viewport } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "ResQ-Band Command Center",
  description: "ระบบบริหารจัดการกำไลข้อมือชี้เป้าและคัดกรองผู้รอดชีวิตใต้ซากอาคารถล่ม",
  manifest: "/manifest.json",
};

export const viewport: Viewport = {
  themeColor: "#1a1a1a",
  width: "device-width",
  initialScale: 1,
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="th">
      <body className="h-screen overflow-hidden bg-app-bg text-app-text">{children}</body>
    </html>
  );
}
