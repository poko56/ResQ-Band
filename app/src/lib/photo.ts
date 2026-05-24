// ============================================================================
// Photo helpers - client-side compression for survivor ID photos.
//
// Wristband.photoDataUrl is base64 JPEG kept in browser storage. We need it
// small enough that 100+ survivors won't blow up localStorage, and small
// enough that the search dispatcher can render them quickly. Target ~50 KB
// after compression by capping dimensions and using JPEG quality 0.72.
// ============================================================================

const MAX_DIMENSION = 480;
const JPEG_QUALITY  = 0.72;

export async function compressImageFile(file: File): Promise<string> {
  if (!file.type.startsWith("image/")) {
    throw new Error("ไฟล์ไม่ใช่รูปภาพ");
  }

  const dataUrl = await new Promise<string>((resolve, reject) => {
    const reader = new FileReader();
    reader.onload  = () => resolve(reader.result as string);
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(file);
  });

  const img = await new Promise<HTMLImageElement>((resolve, reject) => {
    const el = new Image();
    el.onload  = () => resolve(el);
    el.onerror = () => reject(new Error("โหลดรูปไม่สำเร็จ"));
    el.src = dataUrl;
  });

  const ratio = Math.min(1, MAX_DIMENSION / Math.max(img.width, img.height));
  const targetW = Math.round(img.width  * ratio);
  const targetH = Math.round(img.height * ratio);

  const canvas = document.createElement("canvas");
  canvas.width  = targetW;
  canvas.height = targetH;
  const ctx = canvas.getContext("2d");
  if (!ctx) throw new Error("Canvas 2D context ไม่พร้อม");
  ctx.drawImage(img, 0, 0, targetW, targetH);
  return canvas.toDataURL("image/jpeg", JPEG_QUALITY);
}

export function approxKb(dataUrl: string): number {
  // base64 → bytes: each 4 chars encode 3 bytes
  const b64 = dataUrl.split(",")[1] ?? "";
  return Math.round((b64.length * 3) / 4 / 1024);
}
