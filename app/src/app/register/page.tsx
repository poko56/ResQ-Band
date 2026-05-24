"use client";

import { useRef, useState } from "react";
import { TopBar } from "@/components/ui/TopBar";
import { HubStatusBanner } from "@/components/Hub/HubStatusBanner";
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

  const [id, setId]               = useState("");
  const [name, setName]           = useState("");
  const [age, setAge]             = useState("");
  const [gender, setGender]       = useState<"" | "M" | "F" | "other">("");
  const [role, setRole]           = useState<"" | BandRole>("");
  const [idCardNumber, setIdCard] = useState("");
  const [bloodType, setBloodType] = useState("");
  const [allergies,   setAllergies]   = useState<string[]>([]);
  const [conditions,  setConditions]  = useState<string[]>([]);
  const [medications, setMedications] = useState<string[]>([]);
  const [ecName, setEcName]         = useState("");
  const [ecPhone, setEcPhone]       = useState("");
  const [ecRelation, setEcRelation] = useState("");
  const [photo, setPhoto]           = useState<string | undefined>();
  const [photoBusy, setPhotoBusy]   = useState(false);
  const [photoErr,  setPhotoErr]    = useState<string | undefined>();

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
    try { setPhoto(await compressImageFile(file)); }
    catch (err) { setPhotoErr(String((err as Error).message ?? err)); }
    finally { setPhotoBusy(false); }
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
          ? { name: ecName.trim(), phone: ecPhone.trim(), relation: ecRelation.trim() || undefined }
          : undefined,
    });
    resetForm();
  }

  const list = Object.values(wristbands).sort((a, b) => b.registeredAt - a.registeredAt);

  return (
    <div className="flex h-screen flex-col bg-app-bg">
      <TopBar />
      <HubStatusBanner />

      <div className="grid flex-1 grid-cols-[1fr_380px] overflow-hidden divide-x divide-app-divider">
        {/* Form */}
        <form onSubmit={submit} className="overflow-y-auto bg-app-bg">
          <div className="panel-header">Enroll survivor</div>

          <div className="mx-auto max-w-3xl p-4 space-y-3">
            <Section title="Identity">
              <Grid>
                <Field label="Device ID (8-char hex)" mono value={id} onChange={setId} placeholder="A1B2C3D4 — leave blank to randomize" />
                <Field label="Full name *" value={name} onChange={setName} placeholder="สมชาย ใจดี" required />
                <Field label="Age" value={age} onChange={setAge} placeholder="35" />
                <Select label="Gender" value={gender} onChange={(v) => setGender(v as typeof gender)}
                        options={[{ v: "", l: "—" }, { v: "M", l: "Male" }, { v: "F", l: "Female" }, { v: "other", l: "Other" }]} />
                <Select label="Role" value={role} onChange={(v) => setRole(v as typeof role)}
                        options={[
                          { v: "", l: "—" },
                          { v: "worker",   l: "คนงาน" },
                          { v: "engineer", l: "วิศวกร" },
                          { v: "guest",    l: "แขก" },
                          { v: "vip",      l: "VIP" },
                        ]} />
                <Field label="National ID" mono value={idCardNumber} onChange={setIdCard} placeholder="1-1234-56789-01-2" />
              </Grid>
            </Section>

            <Section title="Photo">
              <div className="flex items-start gap-3">
                <div className="grid h-24 w-24 place-items-center overflow-hidden border border-app-border bg-app-input">
                  {photo
                    /* eslint-disable-next-line @next/next/no-img-element */
                    ? <img src={photo} alt="preview" className="h-full w-full object-cover" />
                    : <span className="text-2xs text-app-muted">no photo</span>}
                </div>
                <div className="flex-1 space-y-1">
                  <input
                    ref={fileRef}
                    type="file"
                    accept="image/*"
                    capture="user"
                    onChange={onPhotoPick}
                    className="block w-full text-2xs text-app-dim file:mr-2 file:py-1 file:px-2 file:bg-accent-pressed file:text-white file:border-0 file:rounded-sm hover:file:bg-accent file:text-2xs file:uppercase file:tracking-wider file:font-semibold"
                  />
                  {photoBusy && <p className="text-2xs text-status-warn">compressing…</p>}
                  {photo && !photoBusy && (
                    <div className="flex items-center gap-2 text-2xs text-app-dim">
                      <span className="font-mono">≈ {approxKb(photo)} KB</span>
                      <button type="button" onClick={() => { setPhoto(undefined); if (fileRef.current) fileRef.current.value = ""; }} className="btn btn-sm">
                        Remove
                      </button>
                    </div>
                  )}
                  {photoErr && <p className="text-2xs text-status-err">{photoErr}</p>}
                </div>
              </div>
            </Section>

            <Section title="Medical">
              <div className="space-y-2">
                <Select label="Blood type" value={bloodType} onChange={setBloodType}
                        options={[{ v: "", l: "—" }, ...BLOOD_TYPES.map((b) => ({ v: b, l: b }))]} />
                <TagInput label="Allergies"      values={allergies}   onChange={setAllergies}   placeholder="type, press Enter" suggestions={COMMON_ALLERGIES}   chipColor="bg-status-warn text-app-bg" />
                <TagInput label="Conditions"     values={conditions}  onChange={setConditions}  placeholder="type, press Enter" suggestions={COMMON_CONDITIONS}  chipColor="bg-triage-red" />
                <TagInput label="Medications"    values={medications} onChange={setMedications} placeholder="type, press Enter" suggestions={COMMON_MEDICATIONS} chipColor="bg-accent-pressed" />
              </div>
            </Section>

            <Section title="Emergency contact">
              <Grid>
                <Field label="Name"     value={ecName}     onChange={setEcName}     placeholder="ชื่อ-สกุล" />
                <Field label="Phone"    value={ecPhone}    onChange={setEcPhone}    placeholder="08X-XXX-XXXX" />
                <Field label="Relation" value={ecRelation} onChange={setEcRelation} placeholder="บิดา / มารดา / คู่สมรส" />
              </Grid>
            </Section>

            <div className="flex gap-2 pt-2 border-t border-app-divider">
              <button type="submit" className="btn btn-accent flex-1 h-8 text-xs uppercase tracking-wider font-semibold">
                Enroll wristband
              </button>
              <button type="button" onClick={resetForm} className="btn h-8 text-xs uppercase tracking-wider">
                Reset
              </button>
            </div>
          </div>
        </form>

        {/* Roster */}
        <aside className="overflow-y-auto bg-app-panel">
          <div className="panel-header justify-between">
            <span>Enrolled</span>
            <span className="font-mono text-app-dim normal-case">{list.length}</span>
          </div>
          {list.length === 0 ? (
            <div className="grid place-items-center p-6 text-2xs text-app-muted">no wristbands yet</div>
          ) : (
            <ul className="divide-y divide-app-divider">
              {list.map((wb) => (
                <li key={wb.id} className="row-hover p-2">
                  <div className="flex gap-2">
                    {wb.photoDataUrl
                      /* eslint-disable-next-line @next/next/no-img-element */
                      ? <img src={wb.photoDataUrl} alt={wb.name} className="h-10 w-10 shrink-0 object-cover ring-1 ring-app-border" />
                      : <div className="grid h-10 w-10 shrink-0 place-items-center bg-app-input text-xs font-bold text-app-text ring-1 ring-app-border">
                          {wb.name.slice(0, 2).toUpperCase()}
                        </div>
                    }
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center gap-2">
                        <span className="truncate text-xs font-semibold text-app-text">{wb.name}</span>
                        <span className="font-mono text-2xs text-app-muted">{wb.id}</span>
                      </div>
                      <div className="text-2xs text-app-dim font-mono">
                        {wb.age && <span>age {wb.age} · </span>}
                        {wb.role}
                        {wb.medical?.bloodType && <span> · blood <span className="text-triage-red">{wb.medical.bloodType}</span></span>}
                      </div>
                      {(wb.medical?.allergies?.length || wb.medical?.conditions?.length) && (
                        <div className="mt-0.5 flex flex-wrap gap-0.5 text-2xs">
                          {wb.medical?.allergies?.map((a) => (
                            <span key={`a-${a}`} className="px-1 bg-status-warn/20 text-status-warn">allergy · {a}</span>
                          ))}
                          {wb.medical?.conditions?.map((c) => (
                            <span key={`c-${c}`} className="px-1 bg-triage-red/20 text-triage-red">{c}</span>
                          ))}
                        </div>
                      )}
                    </div>
                    <button onClick={() => removeWristband(wb.id)} className="btn btn-sm btn-ghost self-start">
                      ✕
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
function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <section className="panel">
      <div className="panel-header">{title}</div>
      <div className="p-3">{children}</div>
    </section>
  );
}

function Grid({ children }: { children: React.ReactNode }) {
  return <div className="grid grid-cols-2 gap-3">{children}</div>;
}

function Field(props: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  placeholder?: string;
  required?: boolean;
  mono?: boolean;
}) {
  return (
    <label className="block">
      <span className="field-label">{props.label}</span>
      <input
        value={props.value}
        onChange={(e) => props.onChange(e.target.value)}
        placeholder={props.placeholder}
        required={props.required}
        className={`field ${props.mono ? "font-mono" : ""}`}
      />
    </label>
  );
}

function Select(props: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  options: { v: string; l: string }[];
}) {
  return (
    <label className="block">
      <span className="field-label">{props.label}</span>
      <select value={props.value} onChange={(e) => props.onChange(e.target.value)} className="field">
        {props.options.map((o) => <option key={o.v} value={o.v}>{o.l}</option>)}
      </select>
    </label>
  );
}
