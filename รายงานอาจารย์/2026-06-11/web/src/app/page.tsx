"use client";

import { useState } from "react";

type Wristband = {
  id: string;
  name: string;
  hr: number;
  spo2: number;
  status: "OK" | "SOS";
};

export default function Page() {
  // mock data - ตอนนี้ยังไม่ได้เชื่อมต่อจริง
  const [list, setList] = useState<Wristband[]>([
    { id: "A001", name: "สมชาย ใจดี", hr: 75, spo2: 98, status: "OK" },
    { id: "A002", name: "สมหญิง รักดี", hr: 0, spo2: 0, status: "SOS" },
    { id: "A003", name: "วิชัย สุขใจ", hr: 82, spo2: 97, status: "OK" },
  ]);
  const [name, setName] = useState("");

  function addBand() {
    const t = name.trim();
    if (!t) return;
    const id = "A" + String(list.length + 100).padStart(3, "0");
    setList([...list, { id, name: t, hr: 0, spo2: 0, status: "OK" }]);
    setName("");
  }

  function removeBand(id: string) {
    setList(list.filter((w) => w.id !== id));
  }

  const okCount = list.filter((w) => w.status === "OK").length;
  const sosCount = list.filter((w) => w.status === "SOS").length;

  return (
    <div className="container">
      <h1>ระบบติดตามผู้ประสบภัย</h1>
      <p className="subtitle">หน้าจอแสดงผลกำไลข้อมือผู้สวมในพื้นที่เสี่ยง — เวอร์ชันเริ่มต้น</p>

      <div className="stats">
        <div className="stat-card">
          <div className="stat-num">{list.length}</div>
          <div className="stat-label">กำไลทั้งหมด</div>
        </div>
        <div className="stat-card" style={{ borderLeftColor: "#1a9d4a" }}>
          <div className="stat-num" style={{ color: "#1a9d4a" }}>{okCount}</div>
          <div className="stat-label">ปลอดภัย</div>
        </div>
        <div className="stat-card">
          <div className="stat-num" style={{ color: "#c0392b" }}>{sosCount}</div>
          <div className="stat-label">SOS</div>
        </div>
      </div>

      <div className="row">
        <input
          type="text"
          placeholder="ใส่ชื่อผู้สวมกำไล แล้วกด เพิ่ม"
          value={name}
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => { if (e.key === "Enter") addBand(); }}
        />
        <button className="btn" onClick={addBand}>เพิ่ม</button>
      </div>

      <table>
        <thead>
          <tr>
            <th>ID</th>
            <th>ชื่อ</th>
            <th>HR</th>
            <th>SpO₂</th>
            <th>สถานะ</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          {list.map((w) => (
            <tr key={w.id}>
              <td><code>{w.id}</code></td>
              <td>{w.name}</td>
              <td>{w.hr || "-"}</td>
              <td>{w.spo2 ? `${w.spo2}%` : "-"}</td>
              <td className={w.status === "SOS" ? "status-sos" : "status-ok"}>
                {w.status}
              </td>
              <td>
                <button className="btn" onClick={() => removeBand(w.id)} style={{ padding: "4px 10px", fontSize: 12 }}>
                  ลบ
                </button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
