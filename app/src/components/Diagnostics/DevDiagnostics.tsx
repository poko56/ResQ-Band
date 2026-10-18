"use client";

import React, { useState, useMemo } from "react";
import { useResQ } from "@/lib/store";
import type { BandLinkDiagnostic, DisconnectEvent } from "@/lib/types";

// Signal color palette for lines & badges
const NODE_COLORS: Record<string, string> = {
  direct: "#38bdf8", // Sky blue for Direct to Hub
  pin0:   "#10b981", // Emerald for Pillar 1
  pin1:   "#f59e0b", // Amber for Pillar 2
  pin2:   "#a855f7", // Purple for Pillar 3
  pin3:   "#3b82f6", // Royal blue for Pillar 4
};

function getRssiColor(rssi: number): { text: string; bg: string; border: string } {
  if (rssi >= -75) {
    return { text: "text-emerald-400", bg: "bg-emerald-950/40", border: "border-emerald-700/50" };
  } else if (rssi >= -90) {
    return { text: "text-lime-400", bg: "bg-lime-950/40", border: "border-lime-700/50" };
  } else if (rssi >= -105) {
    return { text: "text-amber-400", bg: "bg-amber-950/40", border: "border-amber-700/50" };
  } else {
    return { text: "text-rose-400", bg: "bg-rose-950/40", border: "border-rose-700/50" };
  }
}

function getLossColor(lossPct: number): string {
  if (lossPct <= 2) return "text-emerald-400";
  if (lossPct <= 10) return "text-amber-400";
  return "text-rose-400";
}

export function DevDiagnostics() {
  const diagnostics = useResQ((s) => s.diagnostics);
  const resetDiagnostics = useResQ((s) => s.resetDiagnostics);
  const wristbands = useResQ((s) => s.wristbands);
  const anchors = useResQ((s) => s.anchors);
  const hub = useResQ((s) => s.hub);

  const [selectedBand, setSelectedBand] = useState<string>("ALL");
  const [metricView, setMetricView] = useState<"rssi" | "snr">("rssi");
  const [eventFilter, setEventFilter] = useState<"all" | "drop" | "lost">("all");
  const [hoveredPoint, setHoveredPoint] = useState<{
    target: string;
    rssi: number;
    snr: number;
    ts: number;
    seq?: number;
    lost?: number;
  } | null>(null);

  const bandList = useMemo(() => {
    const list = Object.values(wristbands);
    if (list.length > 0) return list;
    // Fallback to diagnosed bands if roster is empty
    return Object.keys(diagnostics.bands).map((id) => ({ id, name: id }));
  }, [wristbands, diagnostics.bands]);

  // Aggregate stats across all bands
  const aggregate = useMemo(() => {
    const bands = Object.values(diagnostics.bands);
    let totalExpected = 0;
    let totalReceived = 0;
    let totalLost = 0;

    for (const b of bands) {
      totalExpected += b.totalExpected;
      totalReceived += b.totalReceived;
      totalLost += b.totalLost;
    }

    const pdr = totalExpected > 0 ? (totalReceived / totalExpected) * 100 : 100;
    const lossPct = totalExpected > 0 ? (totalLost / totalExpected) * 100 : 0;

    return {
      totalExpected,
      totalReceived,
      totalLost,
      pdr,
      lossPct,
      activeBandsCount: bands.length,
      eventsCount: diagnostics.disconnectEvents.length,
    };
  }, [diagnostics]);

  // Filtered disconnect events
  const filteredEvents = useMemo(() => {
    return diagnostics.disconnectEvents.filter((ev) => {
      if (eventFilter === "all") return true;
      return ev.type === eventFilter;
    });
  }, [diagnostics.disconnectEvents, eventFilter]);

  // Export JSON Report
  function handleExport() {
    const reportData = {
      exportedAt: new Date().toISOString(),
      hub: {
        state: hub.state,
        mainId: hub.mainId,
        fw: hub.fw,
        uptimeS: diagnostics.hubStats.uptimeS,
        totalRx: diagnostics.hubStats.totalRx,
        totalDropped: diagnostics.hubStats.totalDropped,
      },
      aggregate,
      bands: diagnostics.bands,
      pillars: diagnostics.pins,
      disconnectEvents: diagnostics.disconnectEvents,
    };

    const blob = new Blob([JSON.stringify(reportData, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `resq-link-diagnostics-${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  // Graph Calculations (last 60s timeline)
  const chartData = useMemo(() => {
    const now = Date.now();
    const windowMs = 60000; // 60-second window
    const minTs = now - windowMs;

    // Collect series to draw
    const series: {
      id: string;
      label: string;
      color: string;
      points: { x: number; y: number; raw: any }[];
      drops: { x: number; y: number; count: number; raw: any }[];
    }[] = [];

    const bandsToRender =
      selectedBand === "ALL"
        ? Object.values(diagnostics.bands)
        : [diagnostics.bands[selectedBand]].filter(Boolean);

    bandsToRender.forEach((b) => {
      if (!b) return;
      // Direct points
      const directPts = (b.history || [])
        .filter((pt) => pt.ts >= minTs)
        .map((pt) => {
          const x = ((pt.ts - minTs) / windowMs) * 100; // 0..100%
          const val = metricView === "rssi" ? pt.rssi : pt.snr;
          return { x, y: val, raw: pt };
        });

      const directDrops = (b.history || [])
        .filter((pt) => pt.ts >= minTs && (pt.lostCount ?? 0) > 0)
        .map((pt) => {
          const x = ((pt.ts - minTs) / windowMs) * 100;
          const val = metricView === "rssi" ? pt.rssi : pt.snr;
          return { x, y: val, count: pt.lostCount || 0, raw: pt };
        });

      if (directPts.length > 0) {
        series.push({
          id: `${b.id}-direct`,
          label: `${b.id} (Direct Hub)`,
          color: NODE_COLORS.direct,
          points: directPts,
          drops: directDrops,
        });
      }

      // Pin points for this band
      [0, 1, 2, 3].forEach((pinIdx) => {
        const pinStat = b.pinStats[pinIdx];
        if (pinStat && pinStat.history && pinStat.history.length > 0) {
          const pts = pinStat.history
            .filter((pt) => pt.ts >= minTs)
            .map((pt) => {
              const x = ((pt.ts - minTs) / windowMs) * 100;
              const val = metricView === "rssi" ? pt.rssi : pt.snr;
              return { x, y: val, raw: { ...pt, source: `pin${pinIdx}`, pinIndex: pinIdx } };
            });

          if (pts.length > 0) {
            series.push({
              id: `${b.id}-pin${pinIdx}`,
              label: `${b.id} (เสา ${pinIdx + 1})`,
              color: NODE_COLORS[`pin${pinIdx}`] || "#94a3b8",
              points: pts,
              drops: [],
            });
          }
        }
      });
    });

    return { minTs, windowMs, series };
  }, [diagnostics.bands, selectedBand, metricView]);

  // Coordinate mapping for SVG
  // RSSI: -125 (bottom) to -40 (top)
  // SNR: -15 (bottom) to +15 (top)
  const yRange = metricView === "rssi" ? { min: -125, max: -40, unit: "dBm" } : { min: -15, max: 15, unit: "dB" };
  const valToSvgY = (val: number, h: number) => {
    const clamped = Math.max(yRange.min, Math.min(yRange.max, val));
    const ratio = (clamped - yRange.min) / (yRange.max - yRange.min);
    return h - ratio * h;
  };

  return (
    <div className="flex flex-1 flex-col overflow-hidden bg-app-bg text-app-text select-none">
      {/* Top Header & Actions */}
      <div className="flex items-center justify-between h-9 px-3 bg-app-surface border-b border-app-divider">
        <div className="flex items-center gap-2">
          <span className="flex h-2 w-2 rounded-full bg-emerald-500 animate-pulse" />
          <span className="text-xs font-semibold uppercase tracking-wider text-app-text">
            Connection & Link Diagnostics (การเชื่อมต่อเสา & กำไล)
          </span>
          <span className="font-mono text-2xs text-app-muted border border-app-border px-1.5 py-0.5 rounded">
            TDMA Sequence Tracking
          </span>
        </div>

        <div className="flex items-center gap-2">
          <button
            onClick={handleExport}
            className="btn btn-sm btn-ghost border border-app-border text-2xs hover:text-white"
            title="ส่งออกรายงานคุณภาพสัญญาณและประวัติการหลุดเป็นไฟล์ JSON"
          >
            Export JSON
          </button>
          <button
            onClick={() => {
              if (confirm("รีเซ็ตสถิติคุณภาพสัญญาณและประวัติการหลุดทั้งหมด?")) {
                resetDiagnostics();
              }
            }}
            className="btn btn-sm btn-ghost border border-app-border text-2xs hover:text-status-err"
            title="ล้างตัวนับแพ็กเก็ตที่หลุดและเริ่มเก็บสถิติใหม่"
          >
            Reset Stats
          </button>
        </div>
      </div>

      {/* Main Scrollable Dashboard */}
      <div className="flex-1 overflow-y-auto p-3 space-y-3">
        {/* KPI Cards Grid */}
        <div className="grid grid-cols-2 md:grid-cols-6 gap-2">
          {/* Card 1: PDR */}
          <div className="panel p-2.5 flex flex-col justify-between">
            <span className="text-2xs uppercase tracking-wider text-app-muted">Delivery Rate (PDR)</span>
            <div className="flex items-baseline gap-1 mt-1">
              <span className={`text-xl font-bold font-mono ${getLossColor(aggregate.lossPct)}`}>
                {aggregate.pdr.toFixed(1)}%
              </span>
            </div>
            <span className="text-2xs text-app-muted truncate mt-0.5">
              อัตราส่งสำเร็จ (เป้าหมาย &gt; 95%)
            </span>
          </div>

          {/* Card 2: Dropped / Lost Packets */}
          <div className="panel p-2.5 flex flex-col justify-between">
            <span className="text-2xs uppercase tracking-wider text-app-muted">Lost Packets (หลุด)</span>
            <div className="flex items-baseline gap-1.5 mt-1">
              <span className={`text-xl font-bold font-mono ${aggregate.totalLost > 0 ? "text-amber-400" : "text-emerald-400"}`}>
                {aggregate.totalLost}
              </span>
              <span className="text-2xs text-app-dim font-mono">/ {aggregate.totalExpected}</span>
            </div>
            <span className="text-2xs text-app-muted truncate mt-0.5">
              สูญหายระหว่างทาง (Sequence skip)
            </span>
          </div>

          {/* Card 3: Active Wristbands */}
          <div className="panel p-2.5 flex flex-col justify-between">
            <span className="text-2xs uppercase tracking-wider text-app-muted">Wristbands Active</span>
            <div className="flex items-baseline gap-1 mt-1">
              <span className="text-xl font-bold font-mono text-status-info">
                {Object.keys(diagnostics.bands).length}
              </span>
              <span className="text-2xs text-app-dim font-mono">กำไล</span>
            </div>
            <span className="text-2xs text-app-muted truncate mt-0.5">
              กำไลที่ส่งสัญญาณเข้าระบบ
            </span>
          </div>

          {/* Card 4: Active Anchors (Pillars 1..4) */}
          <div className="panel p-2.5 flex flex-col justify-between">
            <span className="text-2xs uppercase tracking-wider text-app-muted">Pillars (เสา 1-4)</span>
            <div className="flex items-baseline gap-1 mt-1">
              <span className={`text-xl font-bold font-mono ${Object.keys(diagnostics.pins).length === 4 ? "text-emerald-400" : "text-amber-400"}`}>
                {Object.keys(diagnostics.pins).length} / 4
              </span>
              <span className="text-2xs text-app-dim font-mono">เสา</span>
            </div>
            <span className="text-2xs text-app-muted truncate mt-0.5">
              เสารายงานไปยัง MainNode
            </span>
          </div>

          {/* Card 5: Disconnect Events */}
          <div className="panel p-2.5 flex flex-col justify-between">
            <span className="text-2xs uppercase tracking-wider text-app-muted">Disconnects (ขาดติดต่อ)</span>
            <div className="flex items-baseline gap-1 mt-1">
              <span className={`text-xl font-bold font-mono ${aggregate.eventsCount > 0 ? "text-rose-400" : "text-emerald-400"}`}>
                {aggregate.eventsCount}
              </span>
              <span className="text-2xs text-app-dim font-mono">ครั้ง</span>
            </div>
            <span className="text-2xs text-app-muted truncate mt-0.5">
              สัญญาณขาดหาย &gt; 25-30s หรือข้ามเฟรม
            </span>
          </div>

          {/* Card 6: Hub Health */}
          <div className="panel p-2.5 flex flex-col justify-between">
            <span className="text-2xs uppercase tracking-wider text-app-muted">MainNode Rx / Cycle</span>
            <div className="flex items-baseline gap-1 mt-1">
              <span className="text-xl font-bold font-mono text-app-text">
                {diagnostics.hubStats.totalRx}
              </span>
              <span className="text-2xs text-app-dim font-mono">rx</span>
            </div>
            <span className="text-2xs text-app-muted truncate mt-0.5">
              Cycle #{diagnostics.hubStats.currentCycle} · {diagnostics.hubStats.uptimeS}s uptime
            </span>
          </div>
        </div>

        {/* Real-time Graph Section */}
        <div className="panel p-3">
          <div className="flex flex-wrap items-center justify-between gap-2 pb-2.5 border-b border-app-divider">
            <div className="flex items-center gap-3">
              <span className="text-xs font-semibold uppercase tracking-wider text-app-text">
                Live Signal & Dropouts (กราฟสัญญาณและการหลุดแบบ Realtime - 60 วินาทีล่าสุด)
              </span>

              {/* Metric Toggle */}
              <div className="flex items-center bg-app-surface border border-app-border rounded-sm p-0.5">
                <button
                  onClick={() => setMetricView("rssi")}
                  className={`px-2 py-0.5 text-2xs font-semibold rounded-sm transition-colors ${metricView === "rssi" ? "bg-accent text-white" : "text-app-dim hover:text-app-text"}`}
                >
                  RSSI (dBm)
                </button>
                <button
                  onClick={() => setMetricView("snr")}
                  className={`px-2 py-0.5 text-2xs font-semibold rounded-sm transition-colors ${metricView === "snr" ? "bg-accent text-white" : "text-app-dim hover:text-app-text"}`}
                >
                  SNR (dB)
                </button>
              </div>
            </div>

            {/* Target Filter Tabs */}
            <div className="flex items-center gap-1 overflow-x-auto max-w-full">
              <span className="text-2xs text-app-muted mr-1">เลือกกำไล:</span>
              <button
                onClick={() => setSelectedBand("ALL")}
                className={`btn btn-sm ${selectedBand === "ALL" ? "btn-accent" : "btn-ghost border border-app-border"}`}
              >
                ทั้งหมด (All)
              </button>
              {bandList.map((wb) => (
                <button
                  key={wb.id}
                  onClick={() => setSelectedBand(wb.id)}
                  className={`btn btn-sm ${selectedBand === wb.id ? "btn-accent" : "btn-ghost border border-app-border"}`}
                >
                  {wb.name || wb.id}
                </button>
              ))}
            </div>
          </div>

          {/* SVG Graph View */}
          <div className="relative mt-3 h-64 w-full bg-[#141414] border border-app-border rounded-sm overflow-hidden">
            {/* Sensitivity Margin Zones (Background Gradients) */}
            {metricView === "rssi" && (
              <>
                <div
                  className="absolute left-0 right-0 top-0 border-b border-dashed border-emerald-900/40 bg-emerald-950/10 pointer-events-none"
                  style={{ height: `${valToSvgY(-75, 256)}px` }}
                >
                  <span className="absolute top-1 right-2 text-3xs font-mono text-emerald-600/70">
                    EXCELLENT (&gt; -75 dBm)
                  </span>
                </div>
                <div
                  className="absolute left-0 right-0 border-b border-dashed border-amber-900/40 bg-amber-950/5 pointer-events-none"
                  style={{
                    top: `${valToSvgY(-75, 256)}px`,
                    height: `${valToSvgY(-105, 256) - valToSvgY(-75, 256)}px`,
                  }}
                >
                  <span className="absolute top-1 right-2 text-3xs font-mono text-amber-600/70">
                    ACCEPTABLE (-75 ~ -105 dBm)
                  </span>
                </div>
                <div
                  className="absolute left-0 right-0 bottom-0 bg-rose-950/15 pointer-events-none"
                  style={{ height: `${256 - valToSvgY(-105, 256)}px` }}
                >
                  <span className="absolute bottom-1 right-2 text-3xs font-mono text-rose-500/70">
                    CRITICAL MARGIN (&lt; -105 dBm / DROPOUT RISK)
                  </span>
                </div>
              </>
            )}

            {/* Y-axis Labels */}
            <div className="absolute left-2 top-2 bottom-2 flex flex-col justify-between pointer-events-none font-mono text-3xs text-app-muted z-10">
              <span>{yRange.max} {yRange.unit}</span>
              <span>{Math.round((yRange.max + yRange.min) / 2)} {yRange.unit}</span>
              <span>{yRange.min} {yRange.unit}</span>
            </div>

            {/* X-axis Labels */}
            <div className="absolute left-10 right-2 bottom-1 flex justify-between pointer-events-none font-mono text-3xs text-app-muted z-10">
              <span>-60s</span>
              <span>-45s</span>
              <span>-30s</span>
              <span>-15s</span>
              <span>ตอนนี้ (0s)</span>
            </div>

            {/* Main Interactive SVG */}
            <svg
              className="absolute inset-0 h-full w-full"
              viewBox="0 0 1000 256"
              preserveAspectRatio="none"
              onMouseLeave={() => setHoveredPoint(null)}
            >
              {/* Horizontal Grid Lines */}
              {[0, 0.25, 0.5, 0.75, 1].map((ratio) => {
                const y = ratio * 256;
                return (
                  <line
                    key={ratio}
                    x1="0"
                    y1={y}
                    x2="1000"
                    y2={y}
                    stroke="#262626"
                    strokeWidth="1"
                    strokeDasharray="4 4"
                  />
                );
              })}

              {/* Render Series Paths */}
              {chartData.series.map((s) => {
                if (s.points.length < 2) return null;
                const pathStr = s.points
                  .map((p, idx) => {
                    const svgX = (p.x / 100) * 1000;
                    const svgY = valToSvgY(p.y, 256);
                    return `${idx === 0 ? "M" : "L"} ${svgX.toFixed(1)} ${svgY.toFixed(1)}`;
                  })
                  .join(" ");

                return (
                  <g key={s.id}>
                    <path
                      d={pathStr}
                      fill="none"
                      stroke={s.color}
                      strokeWidth="2.2"
                      strokeLinecap="round"
                      strokeLinejoin="round"
                    />

                    {/* Individual points on line */}
                    {s.points.map((p, idx) => {
                      const svgX = (p.x / 100) * 1000;
                      const svgY = valToSvgY(p.y, 256);
                      return (
                        <circle
                          key={idx}
                          cx={svgX}
                          cy={svgY}
                          r="3"
                          fill={s.color}
                          stroke="#141414"
                          strokeWidth="1"
                          className="cursor-pointer hover:r-5 transition-all"
                          onMouseEnter={() =>
                            setHoveredPoint({
                              target: s.label,
                              rssi: p.raw.rssi,
                              snr: p.raw.snr,
                              ts: p.raw.ts,
                              seq: p.raw.seq,
                              lost: p.raw.lostCount,
                            })
                          }
                        />
                      );
                    })}

                    {/* Red Flash Dropped-Packet Markers */}
                    {s.drops.map((d, dIdx) => {
                      const svgX = (d.x / 100) * 1000;
                      return (
                        <g key={`drop-${dIdx}`}>
                          <line
                            x1={svgX}
                            y1="0"
                            x2={svgX}
                            y2="256"
                            stroke="#ef4444"
                            strokeWidth="1.5"
                            strokeDasharray="2 2"
                            opacity="0.8"
                          />
                          <polygon
                            points={`${svgX - 5},0 ${svgX + 5},0 ${svgX},8`}
                            fill="#ef4444"
                          />
                          <circle
                            cx={svgX}
                            cy={valToSvgY(d.y, 256)}
                            r="6"
                            fill="#ef4444"
                            opacity="0.9"
                            className="animate-ping"
                          />
                          <circle
                            cx={svgX}
                            cy={valToSvgY(d.y, 256)}
                            r="4"
                            fill="#ef4444"
                            stroke="#fff"
                            strokeWidth="1"
                          />
                        </g>
                      );
                    })}
                  </g>
                );
              })}
            </svg>

            {/* Hover Tooltip Overlay */}
            {hoveredPoint && (
              <div
                className="absolute top-3 left-16 z-20 bg-app-surface/95 border border-app-border rounded shadow-lg p-2 font-mono text-2xs space-y-0.5 backdrop-blur-sm pointer-events-none"
              >
                <div className="font-semibold text-white">{hoveredPoint.target}</div>
                <div className="text-app-dim">
                  RSSI: <span className="text-emerald-400">{hoveredPoint.rssi} dBm</span> · SNR: <span className="text-cyan-400">{hoveredPoint.snr} dB</span>
                </div>
                {hoveredPoint.seq !== undefined && (
                  <div className="text-app-muted">Seq: #{hoveredPoint.seq}</div>
                )}
                {hoveredPoint.lost !== undefined && hoveredPoint.lost > 0 && (
                  <div className="text-rose-400 font-bold">
                    ⚠️ หลุดในรอบนี้: {hoveredPoint.lost} แพ็กเก็ต
                  </div>
                )}
                <div className="text-3xs text-app-muted">
                  {new Date(hoveredPoint.ts).toLocaleTimeString("th-TH")}
                </div>
              </div>
            )}

            {/* Empty state overlay if no data yet */}
            {chartData.series.length === 0 && (
              <div className="absolute inset-0 flex flex-col items-center justify-center text-app-muted text-xs bg-app-bg/60">
                <span>กำลังรอข้อมูลสัญญาณจากกำไลหรือเสา...</span>
                <span className="text-2xs text-app-dim mt-1">
                  (เริ่มจำลองในโหมด Demo หรือเปิดเครื่องส่ง LoRa)
                </span>
              </div>
            )}
          </div>

          {/* Graph Legend & Drop Indicator Explanation */}
          <div className="flex flex-wrap items-center justify-between gap-2 mt-2 pt-2 text-2xs border-t border-app-divider">
            <div className="flex items-center gap-4">
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-3 rounded-xs" style={{ backgroundColor: NODE_COLORS.direct }} />
                <span>Direct to Hub</span>
              </span>
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-3 rounded-xs" style={{ backgroundColor: NODE_COLORS.pin0 }} />
                <span>เสา 1 (Pin-1)</span>
              </span>
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-3 rounded-xs" style={{ backgroundColor: NODE_COLORS.pin1 }} />
                <span>เสา 2 (Pin-2)</span>
              </span>
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-3 rounded-xs" style={{ backgroundColor: NODE_COLORS.pin2 }} />
                <span>เสา 3 (Pin-3)</span>
              </span>
              <span className="flex items-center gap-1.5">
                <span className="h-2 w-3 rounded-xs" style={{ backgroundColor: NODE_COLORS.pin3 }} />
                <span>เสา 4 (Pin-4)</span>
              </span>
            </div>

            <div className="flex items-center gap-2 text-rose-400">
              <span className="flex h-2 w-2 rounded-full bg-rose-500 animate-pulse" />
              <span>เส้นประสีแดง = ตรวจพบแพ็กเก็ตหลุด/ข้ามลำดับ (Packet Drop)</span>
            </div>
          </div>
        </div>

        {/* Matrix: Pillar <-> Band Link Quality Grid */}
        <div className="panel p-3">
          <div className="panel-header -mx-3 -mt-3 mb-3 justify-between">
            <span>Link Quality Matrix: สัญญาณกำไลผ่านแต่ละเสา (Pillars & Hub)</span>
            <span className="font-mono text-2xs text-app-dim">
              ค่า dBm ยิ่งเข้าใกล้ 0 ยิ่งแรง (เช่น -60 ดีกว่า -100)
            </span>
          </div>

          <div className="overflow-x-auto">
            <table className="w-full text-left text-xs border-collapse">
              <thead>
                <tr className="border-b border-app-divider text-2xs uppercase tracking-wider text-app-muted">
                  <th className="py-2 px-2.5">กำไล (Wristband)</th>
                  <th className="py-2 px-2.5">Direct to Hub</th>
                  <th className="py-2 px-2.5">เสา 1 (Pin-1)</th>
                  <th className="py-2 px-2.5">เสา 2 (Pin-2)</th>
                  <th className="py-2 px-2.5">เสา 3 (Pin-3)</th>
                  <th className="py-2 px-2.5">เสา 4 (Pin-4)</th>
                  <th className="py-2 px-2.5 text-right">Packet Loss</th>
                  <th className="py-2 px-2.5 text-right">สถานะลิงก์</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-app-divider font-mono">
                {Object.keys(wristbands).length === 0 && Object.keys(diagnostics.bands).length === 0 ? (
                  <tr>
                    <td colSpan={8} className="py-6 text-center text-xs text-app-muted">
                      ยังไม่มีข้อมูลกำไลในระบบ (กดลงทะเบียนกำไล หรือเปิดโหมด Demo เพื่อเริ่มทดสอบ)
                    </td>
                  </tr>
                ) : (
                  (Object.keys(wristbands).length > 0 ? Object.keys(wristbands) : Object.keys(diagnostics.bands)).map((id) => {
                    const wb = wristbands[id];
                    const diag = diagnostics.bands[id];
                    const directRssi = diag?.currentRssi;
                    const directStyle = directRssi !== undefined ? getRssiColor(directRssi) : null;
                    const lossRate = diag?.lossRatePct ?? 0;

                    return (
                      <tr key={id} className="hover:bg-app-raised transition-colors">
                        {/* Band Name */}
                        <td className="py-2 px-2.5 font-sans">
                          <div className="font-semibold text-app-text">{wb?.name || id}</div>
                          <div className="font-mono text-3xs text-app-muted">ID: {id}</div>
                        </td>

                        {/* Direct Hub */}
                        <td className="py-2 px-2.5">
                          {directRssi !== undefined && directStyle ? (
                            <span className={`inline-flex items-center gap-1 px-1.5 py-0.5 rounded border text-2xs ${directStyle.bg} ${directStyle.border} ${directStyle.text}`}>
                              <span>{directRssi} dBm</span>
                              <span className="text-3xs text-app-muted font-sans">({diag?.currentSnr} dB)</span>
                            </span>
                          ) : (
                            <span className="text-app-muted text-2xs">--</span>
                          )}
                        </td>

                        {/* Pillar 1..4 */}
                        {[0, 1, 2, 3].map((pinIdx) => {
                          const pStat = diag?.pinStats[pinIdx];
                          const pStyle = pStat?.lastRssi !== undefined ? getRssiColor(pStat.lastRssi) : null;
                          const ageSec = pStat ? Math.round((Date.now() - pStat.lastSeen) / 1000) : null;

                          return (
                            <td key={pinIdx} className="py-2 px-2.5">
                              {pStat && pStyle ? (
                                <div className="space-y-0.5">
                                  <span className={`inline-flex items-center gap-1 px-1.5 py-0.5 rounded border text-2xs ${pStyle.bg} ${pStyle.border} ${pStyle.text}`}>
                                    <span>{pStat.lastRssi} dBm</span>
                                  </span>
                                  <div className="text-3xs text-app-muted">
                                    {pStat.heardCount} ครั้ง · {ageSec}s ที่แล้ว
                                  </div>
                                </div>
                              ) : (
                                <span className="text-app-muted text-2xs">--</span>
                              )}
                            </td>
                          );
                        })}

                        {/* Packet Loss */}
                        <td className="py-2 px-2.5 text-right font-mono">
                          {diag ? (
                            <div>
                              <span className={`font-bold ${getLossColor(lossRate)}`}>
                                {lossRate.toFixed(1)}%
                              </span>
                              <div className="text-3xs text-app-muted">
                                ขาด {diag.totalLost} จาก {diag.totalExpected}
                              </div>
                            </div>
                          ) : (
                            <span className="text-app-muted">0%</span>
                          )}
                        </td>

                        {/* Status Badge */}
                        <td className="py-2 px-2.5 text-right font-sans">
                          {lossRate <= 2 ? (
                            <span className="inline-block px-1.5 py-0.5 text-3xs font-semibold rounded bg-emerald-950 text-emerald-400 border border-emerald-800">
                              เสถียรมาก (Stable)
                            </span>
                          ) : lossRate <= 15 ? (
                            <span className="inline-block px-1.5 py-0.5 text-3xs font-semibold rounded bg-amber-950 text-amber-400 border border-amber-800">
                              ปานกลาง (Fair)
                            </span>
                          ) : (
                            <span className="inline-block px-1.5 py-0.5 text-3xs font-semibold rounded bg-rose-950 text-rose-400 border border-rose-800">
                              หลุดบ่อย (Degraded)
                            </span>
                          )}
                        </td>
                      </tr>
                    );
                  })
                )}
              </tbody>
            </table>
          </div>
        </div>

        {/* Pillars Backhaul to MainNode Status */}
        <div className="panel p-3">
          <div className="panel-header -mx-3 -mt-3 mb-3 justify-between">
            <span>Pillars Link to MainNode Hub (สัญญาณเสาแต่ละตัวไปยังโหนดศูนย์กลาง)</span>
            <span className="font-mono text-2xs text-app-dim">
              SX1278 LoRa Uplink
            </span>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-2 md:grid-cols-4 gap-2.5">
            {[0, 1, 2, 3].map((pinIdx) => {
              const anchor = Object.values(anchors).find((a) => a.pinIndex === pinIdx);
              const pDiag = diagnostics.pins[pinIdx];
              const isOnline = anchor?.online || (pDiag && (Date.now() - pDiag.lastSeen) < 25000);
              const rssiStyle = pDiag ? getRssiColor(pDiag.rssiToHub) : null;

              return (
                <div
                  key={pinIdx}
                  className={`p-2.5 rounded border ${isOnline ? "bg-app-surface border-app-border" : "bg-app-bg border-app-divider opacity-60"}`}
                >
                  <div className="flex items-center justify-between">
                    <span className="text-xs font-semibold text-app-text">
                      เสา {pinIdx + 1} ({anchor?.name || `Pin-${pinIdx + 1}`})
                    </span>
                    <span className={`h-2 w-2 rounded-full ${isOnline ? "bg-emerald-500" : "bg-rose-500"}`} />
                  </div>

                  <div className="mt-2 space-y-1 text-2xs font-mono">
                    <div className="flex justify-between">
                      <span className="text-app-muted font-sans">LoRa to Hub:</span>
                      {pDiag && rssiStyle ? (
                        <span className={`font-semibold ${rssiStyle.text}`}>
                          {pDiag.rssiToHub} dBm ({pDiag.snrToHub} dB)
                        </span>
                      ) : (
                        <span className="text-app-muted">ไม่มีข้อมูล</span>
                      )}
                    </div>
                    <div className="flex justify-between">
                      <span className="text-app-muted font-sans">Reports Count:</span>
                      <span className="text-app-text">{pDiag?.reportsCount ?? 0} ครั้ง</span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-app-muted font-sans">ได้ยินกำไล:</span>
                      <span className="text-status-info">
                        {pDiag?.bandsHeardHistory.slice(-1)[0]?.count ?? 0} เครื่อง
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-app-muted font-sans">สถานะ:</span>
                      <span className={isOnline ? "text-emerald-400" : "text-rose-400 font-sans"}>
                        {isOnline ? "ONLINE" : "OFFLINE / DISCONNECTED"}
                      </span>
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        </div>

        {/* Disconnect & Packet Loss Event Log Table */}
        <div className="panel p-3">
          <div className="panel-header -mx-3 -mt-3 mb-3 justify-between">
            <div className="flex items-center gap-2">
              <span>ประวัติการหลุดและแพ็กเก็ตสูญหาย (Disconnect & Loss Event Log)</span>
              <span className="font-mono text-2xs text-app-muted">
                ({filteredEvents.length} เหตุการณ์)
              </span>
            </div>

            {/* Filter buttons */}
            <div className="flex items-center gap-1 font-sans">
              <button
                onClick={() => setEventFilter("all")}
                className={`btn btn-sm ${eventFilter === "all" ? "btn-accent" : "btn-ghost border border-app-border"}`}
              >
                ทั้งหมด
              </button>
              <button
                onClick={() => setEventFilter("drop")}
                className={`btn btn-sm ${eventFilter === "drop" ? "btn-accent" : "btn-ghost border border-app-border"}`}
              >
                แพ็กเก็ตหลุด (Drops)
              </button>
              <button
                onClick={() => setEventFilter("lost")}
                className={`btn btn-sm ${eventFilter === "lost" ? "btn-accent" : "btn-ghost border border-app-border"}`}
              >
                ขาดการติดต่อ (Lost)
              </button>
            </div>
          </div>

          <div className="max-h-60 overflow-y-auto">
            {filteredEvents.length === 0 ? (
              <div className="py-8 text-center text-xs text-app-muted">
                <span className="text-emerald-400 font-semibold">✓ ไม่พบเหตุการณ์สัญญาณหลุด</span>
                <div className="text-2xs text-app-dim mt-0.5">
                  การรับส่งสัญญาณระหว่างเสาและกำไลทำงานได้ราบรื่น 100%
                </div>
              </div>
            ) : (
              <table className="w-full text-left text-xs border-collapse">
                <thead>
                  <tr className="border-b border-app-divider text-2xs uppercase tracking-wider text-app-muted">
                    <th className="py-1.5 px-2">เวลา (Time)</th>
                    <th className="py-1.5 px-2">ประเภท (Type)</th>
                    <th className="py-1.5 px-2">เป้าหมาย (Target)</th>
                    <th className="py-1.5 px-2">รายละเอียดเหตุการณ์ (Event Details)</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-app-divider font-mono">
                  {filteredEvents.slice(0, 50).map((ev) => {
                    const isDrop = ev.type === "drop";
                    const isLost = ev.type === "lost";

                    return (
                      <tr key={ev.id} className="hover:bg-app-raised transition-colors">
                        <td className="py-1.5 px-2 text-2xs text-app-dim whitespace-nowrap">
                          {new Date(ev.ts).toLocaleTimeString("th-TH", {
                            hour: "2-digit",
                            minute: "2-digit",
                            second: "2-digit",
                          })}
                        </td>
                        <td className="py-1.5 px-2">
                          <span
                            className={`inline-block px-1.5 py-0.5 text-3xs font-semibold rounded uppercase ${
                              isLost
                                ? "bg-rose-950 text-rose-400 border border-rose-800"
                                : isDrop
                                ? "bg-amber-950 text-amber-400 border border-amber-800"
                                : "bg-app-surface text-app-dim border border-app-border"
                            }`}
                          >
                            {isLost ? "DISCONNECTED" : isDrop ? "PACKET DROP" : ev.type}
                          </span>
                        </td>
                        <td className="py-1.5 px-2 font-semibold text-app-text">
                          {ev.target}
                        </td>
                        <td className="py-1.5 px-2 text-app-dim font-sans text-xs">
                          {ev.message}
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
