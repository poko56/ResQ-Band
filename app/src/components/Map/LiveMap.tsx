"use client";

import { Fragment, useEffect, useMemo } from "react";
import { MapContainer, TileLayer, Marker, Popup, Circle, useMap, useMapEvents } from "react-leaflet";
import L from "leaflet";
import { useResQ } from "@/lib/store";
import { DEFAULT_CENTER } from "@/lib/mock";
import { TRIAGE_COLORS, TRIAGE_LABELS_TH } from "@/lib/triage";

function makeAnchorIcon(name: string) {
  return L.divIcon({
    className: "",
    html: `<div class="resq-anchor">${name.slice(0, 2).toUpperCase()}</div>`,
    iconSize: [30, 30],
    iconAnchor: [15, 15],
  });
}

function makeSurvivorIcon(label: string, color: string, sos: boolean) {
  return L.divIcon({
    className: "",
    html: `<div class="resq-pin ${sos ? "pulse-sos" : ""}" style="background:${color}">${label.slice(0, 2).toUpperCase()}</div>`,
    iconSize: [24, 24],
    iconAnchor: [12, 12],
  });
}

function PlacementHandler() {
  const placementMode = useResQ((s) => s.placementMode);
  const placeAnchor = useResQ((s) => s.placeAnchor);

  useMapEvents({
    click(e) {
      if (placementMode === "anchor") {
        placeAnchor({ lat: e.latlng.lat, lng: e.latlng.lng });
      }
    },
  });

  return null;
}

function CenterOnFirstAnchor() {
  const map = useMap();
  const anchors = useResQ((s) => s.anchors);

  useEffect(() => {
    const list = Object.values(anchors);
    if (list.length === 0) return;
    if (list.length === 1) {
      map.setView([list[0]!.position.lat, list[0]!.position.lng], 18);
      return;
    }
    const bounds = L.latLngBounds(list.map((a) => [a.position.lat, a.position.lng]));
    map.fitBounds(bounds, { padding: [40, 40], maxZoom: 19 });
  }, [anchors, map]);

  return null;
}

export default function LiveMap() {
  const anchors = useResQ((s) => s.anchors);
  const wristbands = useResQ((s) => s.wristbands);
  const sightings = useResQ((s) => s.sightings);
  const placementMode = useResQ((s) => s.placementMode);

  const anchorMarkers = useMemo(() => Object.values(anchors), [anchors]);
  const survivorMarkers = useMemo(() => {
    return Object.values(sightings)
      .filter((s) => s.position && s.status !== "rescued")
      .map((s) => ({ sighting: s, wristband: wristbands[s.wristbandId] }));
  }, [sightings, wristbands]);

  return (
    <div className="relative h-full w-full">
      <MapContainer
        center={[DEFAULT_CENTER.lat, DEFAULT_CENTER.lng]}
        zoom={17}
        className="h-full w-full"
        style={{ cursor: placementMode === "anchor" ? "crosshair" : "" }}
      >
        <TileLayer
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
          maxZoom={19}
        />

        <PlacementHandler />
        <CenterOnFirstAnchor />

        {anchorMarkers.map((a) => (
          <Fragment key={a.id}>
            <Marker position={[a.position.lat, a.position.lng]} icon={makeAnchorIcon(a.name)}>
              <Popup>
                <div className="text-xs">
                  <div className="font-bold">{a.name}</div>
                  <div className="font-mono text-slate-500">{a.id}</div>
                  <div>
                    {a.position.lat.toFixed(6)}, {a.position.lng.toFixed(6)}
                  </div>
                </div>
              </Popup>
            </Marker>
            <Circle
              center={[a.position.lat, a.position.lng]}
              radius={150}
              pathOptions={{ color: "#f97316", weight: 1, opacity: 0.4, fillOpacity: 0.05 }}
            />
          </Fragment>
        ))}

        {survivorMarkers.map(({ sighting, wristband }) => {
          const name = wristband?.name ?? sighting.wristbandId;
          const isSOS = sighting.status === "sos";
          return (
            <Marker
              key={sighting.wristbandId}
              position={[sighting.position!.lat, sighting.position!.lng]}
              icon={makeSurvivorIcon(name, TRIAGE_COLORS[sighting.triage], isSOS)}
            >
              <Popup>
                <div className="text-xs">
                  <div className="font-bold">
                    {name}{" "}
                    <span className="ml-2 rounded px-1 text-white" style={{ background: TRIAGE_COLORS[sighting.triage] }}>
                      {TRIAGE_LABELS_TH[sighting.triage]}
                    </span>
                  </div>
                  <div>♥ {sighting.heartRate} BPM · O₂ {sighting.spo2}%</div>
                  <div>🔋 {sighting.batteryPct}% · hops: {sighting.hopCount}</div>
                  <div className="text-slate-500">เห็นล่าสุด: {new Date(sighting.lastSeen).toLocaleTimeString("th-TH")}</div>
                </div>
              </Popup>
            </Marker>
          );
        })}
      </MapContainer>

      {placementMode === "anchor" && (
        <div className="pointer-events-none absolute left-1/2 top-4 -translate-x-1/2 rounded bg-orange-500 px-3 py-1 text-xs font-bold text-white shadow-lg">
          คลิกบนแผนที่เพื่อวางตำแหน่งเสา Anchor
        </div>
      )}
    </div>
  );
}
