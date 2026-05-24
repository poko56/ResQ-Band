"use client";

import { useState } from "react";
import { TopBar } from "@/components/ui/TopBar";
import { useResQ } from "@/lib/store";

export default function SetupPage() {
  const wristbands = useResQ((s) => s.wristbands);
  const registerWristband = useResQ((s) => s.registerWristband);
  const removeWristband = useResQ((s) => s.removeWristband);

  const [id, setId] = useState("");
  const [name, setName] = useState("");
  const [age, setAge] = useState("");
  const [role, setRole] = useState<"" | "worker" | "engineer" | "guest" | "vip">("");

  const submit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim()) return;
    registerWristband({
      id: id.trim() || undefined,
      name: name.trim(),
      age: age.trim() ? Number(age) : undefined,
      role: role || undefined,
    });
    setId("");
    setName("");
    setAge("");
    setRole("");
  };

  const list = Object.values(wristbands).sort((a, b) => b.registeredAt - a.registeredAt);

  return (
    <div className="flex h-screen flex-col">
      <TopBar />

      <div className="grid flex-1 grid-cols-[400px_1fr] overflow-hidden">
        <section className="border-r border-panel-border bg-panel-soft p-5">
          <h2 className="mb-1 text-lg font-bold">ลงทะเบียนกำไลใหม่</h2>
          <p className="mb-4 text-xs text-slate-400">
            กำหนดชื่อและเจ้าของให้กำไลแต่ละตัวก่อนออกภารกิจ — ID ต้องตรงกับ Device ID ของฮาร์ดแวร์
          </p>

          <form onSubmit={submit} className="space-y-3">
            <Field
              label="Device ID (8 ตัวอักษร hex)"
              hint="ปล่อยว่างถ้ายังไม่ทราบ ระบบจะสุ่มให้"
              value={id}
              onChange={setId}
              placeholder="A1B2C3D4"
              mono
            />
            <Field label="ชื่อผู้สวม *" value={name} onChange={setName} placeholder="เช่น 'สมชาย ใจดี'" required />
            <Field label="อายุ" value={age} onChange={setAge} placeholder="35" />
            <label className="block">
              <span className="mb-1 block text-xs font-semibold uppercase tracking-wide text-slate-400">บทบาท</span>
              <select
                value={role}
                onChange={(e) => setRole(e.target.value as typeof role)}
                className="w-full rounded border border-panel-border bg-panel px-3 py-2 text-sm outline-none focus:border-triage-red"
              >
                <option value="">— เลือก —</option>
                <option value="worker">คนงาน</option>
                <option value="engineer">วิศวกร</option>
                <option value="guest">แขก</option>
                <option value="vip">VIP</option>
              </select>
            </label>
            <p className="text-[11px] text-slate-500">
              ⚠️ ข้อมูลทางการแพทย์ (กรุ๊ปเลือด, แพ้, โรค) จะกรอกได้ในหน้าลงทะเบียนเต็มรูปแบบ (/register) ที่จะเพิ่มในรุ่นถัดไป
            </p>

            <button
              type="submit"
              className="w-full rounded bg-triage-red px-3 py-2 text-sm font-bold uppercase tracking-wide hover:bg-red-600"
            >
              ลงทะเบียน
            </button>
          </form>
        </section>

        <section className="overflow-y-auto p-5">
          <h2 className="mb-3 text-lg font-bold">
            กำไลที่ลงทะเบียนแล้ว ({list.length})
          </h2>

          {list.length === 0 ? (
            <div className="rounded border border-dashed border-panel-border p-6 text-center text-sm text-slate-500">
              ยังไม่มีกำไลในระบบ ลงทะเบียนตัวแรกได้จากฟอร์มทางซ้าย
            </div>
          ) : (
            <div className="space-y-2">
              {list.map((wb) => (
                <div
                  key={wb.id}
                  className="flex items-center gap-3 rounded border border-panel-border bg-panel-soft p-3"
                >
                  <div className="grid h-10 w-10 place-items-center rounded bg-slate-700 font-mono text-xs">
                    {wb.name.slice(0, 2).toUpperCase()}
                  </div>
                  <div className="min-w-0 flex-1">
                    <div className="font-semibold">{wb.name}</div>
                    <div className="text-xs text-slate-400">
                      <span className="font-mono">{wb.id}</span>
                      {wb.age   && <span> · อายุ {wb.age}</span>}
                      {wb.role  && <span> · {wb.role}</span>}
                    </div>
                  </div>
                  <button
                    onClick={() => removeWristband(wb.id)}
                    className="rounded bg-slate-700 px-2 py-1 text-xs text-slate-300 hover:bg-red-700 hover:text-white"
                  >
                    ลบ
                  </button>
                </div>
              ))}
            </div>
          )}
        </section>
      </div>
    </div>
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
      <span className="mb-1 block text-xs font-semibold uppercase tracking-wide text-slate-400">
        {props.label}
      </span>
      <input
        value={props.value}
        onChange={(e) => props.onChange(e.target.value)}
        placeholder={props.placeholder}
        required={props.required}
        className={`w-full rounded border border-panel-border bg-panel px-3 py-2 text-sm outline-none focus:border-triage-red ${
          props.mono ? "font-mono" : ""
        }`}
      />
      {props.hint && <span className="mt-1 block text-[11px] text-slate-500">{props.hint}</span>}
    </label>
  );
}
