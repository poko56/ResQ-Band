"use client";

import dynamic from "next/dynamic";
import { useEffect } from "react";
import { useResQ } from "@/lib/store";
import { TopBar } from "@/components/ui/TopBar";

const HubBridgeClient = dynamic(() => import("@/components/Hub/HubBridgeClient"), {
  ssr: false,
});

const HubStatusBanner = dynamic(() => import("@/components/Hub/HubStatusBanner").then(mod => mod.HubStatusBanner), {
  ssr: false,
});

export function ClientLayout({ children }: { children: React.ReactNode }) {
  // Load the saved roster/anchors only after hydration (see skipHydration).
  useEffect(() => {
    void useResQ.persist.rehydrate();
  }, []);

  return (
    <>
      <HubBridgeClient />
      <TopBar />
      <HubStatusBanner />
      {children}
    </>
  );
}
