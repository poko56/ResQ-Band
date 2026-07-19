"use client";

import dynamic from "next/dynamic";
import { TopBar } from "@/components/ui/TopBar";

const HubBridgeClient = dynamic(() => import("@/components/Hub/HubBridgeClient"), {
  ssr: false,
});

const HubStatusBanner = dynamic(() => import("@/components/Hub/HubStatusBanner").then(mod => mod.HubStatusBanner), {
  ssr: false,
});

export function ClientLayout({ children }: { children: React.ReactNode }) {
  return (
    <>
      <HubBridgeClient />
      <TopBar />
      <HubStatusBanner />
      {children}
    </>
  );
}
