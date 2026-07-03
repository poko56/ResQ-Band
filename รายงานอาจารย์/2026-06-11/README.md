# รายงานความคืบหน้า — 11 มิถุนายน 2569

โครงงาน **ระบบติดตามผู้ประสบภัยใต้ซากอาคารถล่ม**

## สิ่งที่ทำเสร็จในรอบนี้

- จัดวางโครงสร้างโปรเจกต์ทั้งฝั่ง firmware และ web
- เซ็ตอัพ PlatformIO สำหรับ ESP32 ทั้ง 4 บอร์ด
- Band-Node (กำไลข้อมือ): อ่านค่า **MPU6050** (accel/gyro) และ **MAX30102** (heart rate) ส่งออก Serial
- MainNode (ศูนย์ควบคุม): init LoRa SX1278 + ฟัง packet ขาเข้า ดูค่า RSSI / SNR
- Pillar (เสาสัญญาณ): ทดสอบ LoRa receive แล้วนับ packet
- Handheld (เครื่องพกพา): ทดสอบ OLED SH1106 + ปุ่ม + LoRa init
- ขึ้นโครงเว็บ **Next.js 15** หน้า dashboard เบื้องต้น (mock data)

## ปัญหาที่เจอ

- MAX30102 ค่า HR ไม่นิ่ง ต้องใช้ moving average 4 ค่า
- ESP32-C3 มี SPI ตัวเดียว ตอนต่อ UWB เพิ่มในอนาคตต้องใช้ CS แยกขาให้ดี
- ESP32-S3 ใช้ native USB CDC ตอน flash ครั้งแรกต้องกดปุ่ม BOOT

## แผนงานสัปดาห์ถัดไป

1. ออกแบบ packet protocol แบบ struct (magic + type + CRC16)
2. ลองให้ Band ส่ง heartbeat จริง → MainNode รับแล้ว parse
3. เพิ่มหน้า register กำไลในเว็บ
4. เริ่มทำ TDMA scheduling เบื้องต้น

## โครงสร้างโฟลเดอร์

```
2026-06-11/
├── README.md                ← ไฟล์นี้
├── firmware/
│   ├── platformio.ini
│   ├── include/
│   │   └── config.h         ← pin map ของแต่ละบอร์ด
│   └── src/
│       ├── band_node/main.cpp   ← MPU6050 + MAX30102
│       ├── main_node/main.cpp   ← LoRa RX + USB Serial
│       ├── pillar/main.cpp      ← LoRa RX (เสาสัญญาณ)
│       └── handheld/main.cpp    ← OLED + ปุ่ม + LoRa
└── web/
    ├── package.json
    ├── next.config.mjs
    ├── tsconfig.json
    └── src/app/
        ├── layout.tsx
        ├── page.tsx         ← Dashboard
        └── globals.css
```

## วิธีรัน

### Firmware (PlatformIO)

```bash
cd firmware
# build เลือกบอร์ดที่จะ flash
pio run -e band_node        # หรือ main_node, pillar, handheld
pio run -e band_node -t upload
pio device monitor -e band_node
```

### Web (Next.js)

```bash
cd web
npm install
npm run dev
# เปิด http://localhost:3000
```

## ฮาร์ดแวร์ที่ใช้

| บอร์ด | MCU | เซนเซอร์ / Module |
|---|---|---|
| Band-Node (กำไลข้อมือ) | ESP32-C3 DevKit M-1 | MPU6050, MAX30102, SX1278, Buzzer |
| MainNode (ศูนย์ควบคุม) | ESP32-S3 DevKitC-1 | SX1278, USB-CDC |
| Pillar (เสาสัญญาณ) | ESP32 DevKit | SX1278 |
| Handheld (เครื่องพกพา) | ESP32 DevKit | SX1278, OLED SH1106, ปุ่มกด |

## หมายเหตุ

โค้ดในรอบนี้เป็น **เวอร์ชันเริ่มต้น** เน้นทดสอบ peripheral ทีละตัว ยังไม่มี protocol จริง ยังไม่มี TDMA ยังไม่ส่ง LoRa จริงระหว่างบอร์ด — เป็น scaffold สำหรับต่อยอดในสัปดาห์ถัดไป

---
*จัดทำโดย: นักศึกษา · 11 มิ.ย. 2569*
