import type { Wristband } from "./types";

// Downscale a stored survivor photo to a handheld-sized thumbnail. The
// dispatcher keeps 480 px / q0.72 (~50 KB) which is far more than the ResQ-Node
// needs, and its LittleFS only holds ~960 KB for the whole roster.
export async function makeThumb(dataUrl: string): Promise<string | undefined> {
  try {
    const img = await new Promise<HTMLImageElement>((resolve, reject) => {
      const el = new Image();
      el.onload = () => resolve(el);
      el.onerror = () => reject(new Error("decode failed"));
      el.src = dataUrl;
    });
    const SIZE = 160;
    const ratio = Math.min(1, SIZE / Math.max(img.width, img.height));
    const canvas = document.createElement("canvas");
    canvas.width = Math.round(img.width * ratio);
    canvas.height = Math.round(img.height * ratio);
    const ctx = canvas.getContext("2d");
    if (!ctx) return undefined;
    ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
    return canvas.toDataURL("image/jpeg", 0.6);
  } catch {
    return undefined; // a bad photo must not block the whole roster
  }
}

// Build the roster object the ResQ-Node expects: keyed by band device id (hex,
// uppercase), each entry carrying name / medical / photo (thumbnailed). Shared
// by the "Export file" and the "Sync to Locator" flows so both produce the
// exact same shape the handheld's /api/roster consumer reads.
export async function buildRosterPayload(
  wristbands: Record<string, Wristband>,
): Promise<Record<string, unknown>> {
  const out: Record<string, unknown> = {};
  for (const wb of Object.values(wristbands)) {
    const photo = wb.photoDataUrl ? await makeThumb(wb.photoDataUrl) : undefined;
    out[wb.id.toUpperCase()] = {
      name: wb.name,
      age: wb.age,
      gender: wb.gender,
      role: wb.role,
      medical: wb.medical,
      emergencyContact: wb.emergencyContact,
      photo,
    };
  }
  return out;
}
