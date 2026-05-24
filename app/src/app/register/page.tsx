"use client";

// ============================================================================
// /register - full survivor onboarding before they enter the risk zone.
//
// All fields except (id, name) are optional, but the more we capture the more
// useful the dispatcher card will be for the field team:
//   - blood type / allergies / conditions / medications help triage decisions
//   - emergency contact lets dispatch notify family in parallel with rescue
//   - photo lets rescuers identify the right person at the scene
// ============================================================================

import { useRef, useState } from "react";
import Image from "next/image";
import { TopBar } from "@/components/ui/TopBar";
import { TagInput } from "@/components/ui/TagInput";
import { useResQ } from "@/lib/store";
import { compressImageFile, approxKb } from "@/lib/photo";
import type { BandRole } from "@/lib/types";

const BLOOD_TYPES = ["A+", "A-", "B+", "B-", "O+", "O-", "AB+", "AB-"];

const COMMON_ALLERGIES   = ["เพนนิซิลลิน", "อาหารทะเล", "ถั่ว", "นม", "ไข่", "ฝุ่น"];
const COMMON_CONDITIONS  = ["เบาหวาน", "ความดันสูง", "หอบหืด", "โรคหัวใจ", "ลมชัก", "ไต"];
const COMMON_MEDICATIONS = ["อินซูลิน", "EpiPen", "ยาความดัน", "ยาละลายลิ่มเลือด"];

export default function RegisterPage() {
  const wristbands       = useResQ((s) => s.wristbands);
  const registerWristband = useResQ((s) => s.registerWristband);
  const removeWristband   = useResQ((s) => s.removeWristband);

  // Form state
  const [id,        setId]        = useState("");
  const [name,      setName]      = useState("");
  const [age,       setAge]       = useState("");
  const [gender,    setGender]    = useState<"" | "M" | "F" | "other">("");
  const [role,      setRole]      = useState<"" | BandRole>("");
  const [idCardNumber, setIdCard] = useState("");
  const [bloodType, setBloodType] = useState("");
  const [allergies,   setAllergies]   = useState<string[]>([]);
  const [conditions,  setConditions]  = useState<string[]>([]);
  const [medications, setMedications] = useState<string[]>([]);
  const [ecName, setEcName] = useState("");
  const [ecPhone, setEcPhone] = useState("");
  const [ecRelation, setEcRelation] = useState("");
  const [photo, setPhoto] = useState<string | undefined>(undefined);
  const [photoBusy, setPhotoBusy] = useState(false);
  const [photoErr,  setPhotoErr]  = useState<string | undefined>(undefined);

  const fileRef = useRef<HTMLInputElement | null>(null);

  function resetForm() {
    setId(""); setName(""); setAge(""); setGender(""); setRole("");
    setIdCard(""); setBloodType("");
    setAllergies([]); setConditions([]); setMedications([]);
    setEcName(""); setEcPhone(""); setEcRelation("");
    setPhoto(undefined); setPhotoErr(undefined);
    if (fileRef.current) fileRef.current.value = "";
  }

  async function onPhotoPick(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    setPhotoBusy(true); setPhotoErr(undefined);
    try {
      const dataUrl = await compressImageFile(file);
      setPhoto(dataUrl);
    } catch (err) {
      setPhotoErr(String((err as Error).message ?? err));
    } finally {
      setPhotoBusy(false);
    }
  }

  function submit(e: React.FormEvent) {
    e.preventDefault();
    if (!name.trim()) return;
    registerWristband({
      id: id.trim() || undefined,
      name: name.trim(),
      age: age.trim() ? Number(age) : undefined,
      gender: gender || undefined,
      role: role || undefined,
      idCardNumber: idCardNumber.trim() || undefined,
      photoDataUrl: photo,
      medical:
        (bloodType || allergies.length || conditions.length || medications.length)
          ? {
              bloodType:   bloodType || undefined,
              allergies:   allergies.length   ? allergies   : undefined,
              conditions:  conditions.length  ? conditions  : undefined,
              medications: medications.length ? medications : undefined,
            }
          : undefined,
      emergencyContact:
        (ecName.trim() || ecPhone.trim())
          ? {
              name: ecName.trim(),
              phone: ecPhone.trim(),
              relation: ecRelation.trim() || undefined,
            }
          : undefined,
    });
    resetForm();
  }

  const list = Object.values(wristbands).sort((a, b) => b.registeredAt - a.registeredAt);

  return (
    <div className="flex h-screen flex-col">
      <TopBar />

      <div className="grid flex-1 grid-cols-[1fr_400px] overflow-hidden">
        {/* ---- FORM ------------------------------------------------------ */}
        <form onSubmit={submit} className="overflow-y-auto p-6">
          <div className="mx-auto max-w-3xl space-y-6">
            <header>
              <h1 className="text-2xl font-bold">ลงทะเบียนผู้สวมกำไล</h1>
              <p className="text-sm text-slate-400">
                กรอกข้อมูลก่อนเข้าพื้นที่เสี่ยง — ยิ่งกรอกครบ ทีมกู้ภัยยิ่งช่วยได้ตรงจุด
              </p>
            </header>

            {/* Identity ------------------------------------------------- */}
            <Section title="ข้อมูลพื้นฐาน" desc="จำเป็น: ชื่อ — อื่นๆ ใส่ถ้ามี">
              <div className="grid grid-cols-2 gap-3">
                <Field label="Device ID (8 ตัวอักษร hex)" mono value={id} onChange={setId}
                       placeholder="A1B2C3D4 — ปล่อยว่างให้สุ่ม" />
                <Field label="ชื่อ-สกุล *" value={name} onChange={setName} placeholder="สมชาย ใจดี" required />
                <Field label="อายุ" value={age} onChange={setAge} placeholder="35" />
                <SelectField label="เพศ" value={gender} onChange={(v) => setGender(v as typeof gender)}
                             options={[
                               { v: "",      l: "— เลือก —" },
                               { v: "M",     l: "ชาย" },
                               { v: "F",     l: "หญิง" },
                               { v: "other", l: "อื่นๆ" },
                             ]} />
                <SelectField label="บทบาท" value={role} onChange={(v) => setRole(v as typeof role)}
                             options={[
                               { v: "",         l: "— เลือก —" },
                               { v: "worker",   l: "คนงาน" },
                               { v: "engineer", l: "วิศวกร" },
                               { v: "guest",    l: "แขก / ผู้เยี่ยมชม" },
                               { v: "vip",      l: "VIP" },
                             ]} />
                <Field label="เลขบัตรประชาชน" mono value={idCardNumber} onChange={setIdCard}
                       placeholder="1-1234-56789-01-2" />
              </div>
            </Section>

            {/* Photo --------------------------------------------------- */}
            <Section title="ภาพถ่ายผู้สวม" desc="ใช้ให้ทีมกู้ภัยระบุตัวบุคคลในที่เกิดเหตุได้รวดเร็ว">
              <div className="flex items-start gap-4">
                <div className="grid h-28 w-28 place-items-center overflow-hidden rounded border border-panel-border bg-panel">
                  {photo ? (
                    /* eslint-disable-next-line @next/next/no-img-element */
                    <img src={photo} alt="preview" className="h-full w-full object-cover" />
                  ) : (
                    <span className="text-xs text-slate-500">ยังไม่มีรูป</span>
                  )}
                </div>
                <div className="flex-1 space-y-2">
                  <input
                    ref={fileRef}
                    type="file"
                    accept="image/*"
                    capture="user"
                    onChange={onPhotoPick}
                    className="block w-full cursor-pointer rounded border border-panel-border bg-panel px-3 py-2 text-sm text-slate-300 file:mr-3 file:rounded file:border-0 file:bg-emerald-700 file:px-3 file:py-1 file:text-white"
                  />
                  {photoBusy && <p className="text-xs text-amber-400">กำลังบีบอัดรูป...</p>}
                  {photo && !photoBusy && (
                    <div className="flex items-center gap-2 text-xs text-slate-400">
                      <span>ขนาดประมาณ {approxKb(photo)} KB</span>
                      <button
                        type="button"
                        onClick={() => { setPhoto(undefined); if (fileRef.current) fileRef.current.value = ""; }}
                        className="rounded bg-slate-700 px-2 py-0.5 text-slate-200 hover:bg-red-700"
                      >
                        ลบรูป
                      </button>
                    </div>
                  )}
                  {photoErr && <p className="text-xs text-red-400">{photoErr}</p>}
                </div>
              </div>
            </Section>

            {/* Medical -------------------------------------------------- */}
            <Section title="ข้อมูลทางการแพทย์" desc="สำคัญต่อการตัดสินใจชีวิตในที่เกิดเหตุ">
              <div className="space-y-3">
                <SelectField label="กรุ๊ปเลือด" value={bloodType} onChange={setBloodType}
                             options={[{ v: "", l: "— เลือก —" }, ...BLOOD_TYPES.map((b) => ({ v: b, l: b }))]} />

                <TagInput label="แพ้" values={allergies} onChange={setAllergies}
                          placeholder="พิมพ์แล้วกด Enter หรือ ,"
                          chipColor="bg-amber-700" suggestions={COMMON_ALLERGIES} />

                <TagInput label="โรคประจำตัว" values={conditions} onChange={setConditions}
                          placeholder="พิมพ์แล้วกด Enter หรือ ,"
                          chipColor="bg-rose-700" suggestions={COMMON_CONDITIONS} />

                <TagInput label="ยาที่ใช้ประจำ" values={medications} onChange={setMedications}
                          placeholder="พิมพ์แล้วกด Enter หรือ ,"
                          chipColor="bg-indigo-700" suggestions={COMMON_MEDICATIONS} />
              </div>
            </Section>

            {/* Emergency contact --------------------------------------- */}
            <Section title="ผู้ติดต่อฉุกเฉิน" desc="ทีม dispatcher อาจติดต่อขนานไปกับการกู้ภัย">
              <div className="grid grid-cols-2 gap-3">
                <Field label="ชื่อผู้ติดต่อ" value={ecName} onChange={setEcName} placeholder="ชื่อ-สกุล" />
                <Field label="เบอร์โทร" value={ecPhone} onChange={setEcPhone} placeholder="08X-XXX-XXXX" />
                <Field label="ความสัมพันธ์" value={ecRelation} onChange={setEcRelation} placeholder="บิดา / มารดา / คู่สมรส" />
              </div>
            </Section>

            <div className="flex gap-3">
              <button
                type="submit"
                className="flex-1 rounded bg-triage-red px-4 py-2.5 text-sm font-bold uppercase tracking-wide text-white hover:bg-red-600"
              >
                ลงทะเบียน
              </button>
              <button
                type="button"
                onClick={resetForm}
                className="rounded bg-slate-700 px-4 py-2.5 text-sm text-slate-200 hover:bg-slate-600"
              >
                ล้างฟอร์ม
              </button>
            </div>
          </div>
        </form>

        {/* ---- ROSTER --------------------------------------------------- */}
        <aside className="overflow-y-auto border-l border-panel-border bg-panel-soft p-4">
          <h2 className="mb-3 text-sm font-bold uppercase tracking-wide text-slate-400">
            ลงทะเบียนแล้ว ({list.length})
          </h2>
          {list.length === 0 ? (
            <div className="rounded border border-dashed border-panel-border p-6 text-center text-xs text-slate-500">
              ยังไม่มีกำไลในระบบ
            </div>
          ) : (
            <ul className="space-y-2">
              {list.map((wb) => (
                <li key={wb.id} className="rounded border border-panel-border bg-panel p-2.5">
                  <div className="flex gap-3">
                    {wb.photoDataUrl ? (
                      /* eslint-disable-next-line @next/next/no-img-element */
                      <img src={wb.photoDataUrl} alt={wb.name} className="h-12 w-12 shrink-0 rounded object-cover" />
                    ) : (
                      <div className="grid h-12 w-12 shrink-0 place-items-center rounded bg-slate-700 text-sm font-bold">
                        {wb.name.slice(0, 2).toUpperCase()}
                      </div>
                    )}
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center gap-2">
                        <span className="truncate text-sm font-semibold">{wb.name}</span>
                        <span className="font-mono text-[10px] text-slate-500">{wb.id}</span>
                      </div>
                      <div className="text-xs text-slate-400">
                        {wb.age && <span>อายุ {wb.age}</span>}
                        {wb.role && <span> · {wb.role}</span>}
                        {wb.medical?.bloodType && <span> · เลือด <span className="text-rose-400">{wb.medical.bloodType}</span></span>}
                      </div>
                      {(wb.medical?.allergies?.length || wb.medical?.conditions?.length) && (
                        <div className="mt-1 flex flex-wrap gap-1 text-[10px]">
                          {wb.medical?.allergies?.map((a) => (
                            <span key={`a-${a}`} className="rounded bg-amber-700/40 px-1 text-amber-200">แพ้: {a}</span>
                          ))}
                          {wb.medical?.conditions?.map((c) => (
                            <span key={`c-${c}`} className="rounded bg-rose-700/40 px-1 text-rose-200">{c}</span>
                          ))}
                        </div>
                      )}
                    </div>
                    <button
                      onClick={() => removeWristband(wb.id)}
                      className="self-start rounded bg-slate-700 px-2 py-1 text-[10px] text-slate-300 hover:bg-red-700 hover:text-white"
                    >
                      ลบ
                    </button>
                  </div>
                </li>
              ))}
            </ul>
          )}
        </aside>
      </div>
    </div>
  );
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
function Section({ title, desc, children }: { title: string; desc?: string; children: React.ReactNode }) {
  return (
    <section className="rounded border border-panel-border bg-panel-soft p-4">
      <h3 className="text-sm font-bold uppercase tracking-wide text-slate-300">{title}</h3>
      {desc && <p className="mb-3 text-xs text-slate-500">{desc}</p>}
      <div className="mt-2">{children}</div>
    </section>
  );
}

function Field(props: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  hint?: string;
  required?: boolean;
  mono?: boolean;
}) {
  return (
    <label className="block">
      <span className="mb-1 block text-xs font-semibold uppercase tracking-wide text-slate-400">{props.label}</span>
      <input
        value={props.value}
        onChange={(e) => props.onChange(e.target.value)}
        placeholder={props.placeholder}
        required={props.required}
        className={`w-full rounded border border-panel-border bg-panel px-3 py-2 text-sm outline-none focus:border-emerald-500 ${props.mono ? "font-mono" : ""}`}
      />
      {props.hint && <span className="mt-1 block text-[11px] text-slate-500">{props.hint}</span>}
    </label>
  );
}

function SelectField(props: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  options: { v: string; l: string }[];
}) {
  return (
    <label className="block">
      <span className="mb-1 block text-xs font-semibold uppercase tracking-wide text-slate-400">{props.label}</span>
      <select
        value={props.value}
        onChange={(e) => props.onChange(e.target.value)}
        className="w-full rounded border border-panel-border bg-panel px-3 py-2 text-sm outline-none focus:border-emerald-500"
      >
        {props.options.map((o) => (
          <option key={o.v} value={o.v}>{o.l}</option>
        ))}
      </select>
    </label>
  );
}
