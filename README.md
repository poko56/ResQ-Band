<div align="center">

# 🛟 ResQ-Band
**The IoT Wearable Emergency Localization and Survival Triage System** *ระบบกำไลข้อมืออัจฉริยะชี้เป้าและคัดกรองผู้รอดชีวิตใต้ซากอาคารถล่มสำหรับงาน US&R*

</div>

---

## 📌 Introduction (บทนำ)

จากปัญหาอุบัติเหตุอาคารและสิ่งก่อสร้างถล่ม **"เวลาทองคำ (Golden Hour)"** คือสิ่งสำคัญที่สุดในการช่วยชีวิต ทว่าโครงสร้างคอนกรีตและเหล็กเส้นมักดักจับและสะท้อนคลื่นวิทยุทั่วไป ทำให้ระบบระบุตำแหน่งมาตรฐานไม่สามารถทำงานได้ 

**ResQ-Band** คือโปรเจกต์ Open-Source ฮาร์ดแวร์และซอฟต์แวร์ที่ผสานเทคโนโลยี **Ultra-Wideband (UWB)** สำหรับการระบุตำแหน่งความแม่นยำสูงในภาวะปกติ และ **LoRa (433 MHz)** สำหรับการส่งสัญญาณทะลุทะลวงซากปรักหักพังในภาวะฉุกเฉิน ทำงานร่วมกับระบบประเมินสัญญาณชีพ เพื่อช่วยให้ทีมกู้ภัย (Urban Search & Rescue) สามารถชี้เป้าหมายและช่วยเหลือผู้รอดชีวิตได้อย่างแม่นยำที่สุด

---

## ✨ Features (จุดเด่นของระบบ)

* 📍 **Precision Pre-Collapse Tracking:** ใช้คลื่น UWB ประเมินพิกัดแบบ Real-time ระดับเซนติเมตรขณะอาคารยังไม่ถล่ม (Smart Zone Attendance)
* 💥 **The Fall Trigger:** อัลกอริทึมตรวจจับสภาวะตกอิสระ (Free-fall) หรือแรงกระแทกมหาศาล (High-G) เพื่อล็อกพิกัดสุดท้าย (Last Known Position) ก่อนซากอาคารพังทับ
* 📡 **Deep-Penetration SOS:** ส่งสัญญาณขอความช่วยเหลือทะลวงซากคอนกรีตด้วยคลื่นความถี่ต่ำ LoRa 433 MHz
* 🕸️ **Underground Mesh Relay:** ระบบทวนสัญญาณใยแมงมุม (ESP-NOW) ให้กำไลใต้ซากตึกช่วยส่งต่อสัญญาณกันเองเพื่อเพิ่มระยะส่ง
* 🫀 **Survival Triage Logic:** คัดกรองผู้บาดเจ็บอัตโนมัติด้วยเซนเซอร์วัดชีพจรและการเคาะรหัสขอความช่วยเหลือ (Tap-to-SOS)
* 🎯 **Last-Meter UWB Pinpointing:** เมื่อทีมกู้ภัยเข้าใกล้พื้นที่เป้าหมาย เครื่องค้นหาพกพาจะสลับจาก LoRa RSSI ไปใช้ **UWB Ranging (Two-Way Ranging)** เพื่อระบุระยะทาง-ทิศทางไปยังกำไลใต้ซากอาคารด้วยความแม่นยำระดับ **10–30 ซม.** ช่วยให้ขุดเจาะได้ตรงจุดโดยไม่รบกวนโครงสร้างที่อาจถล่มซ้ำ
* 🔄 **Fleet OTA Self-Update:** ระบบอัพเดท firmware อัตโนมัติแบบ Over-The-Air สำหรับอุปกรณ์ทุกตัวในฝูงบิน (Fleet) ผ่าน Firebase Storage → Gateway (WiFi) → Wristband/Handheld (ESP-NOW Chunk Protocol) โดยไม่ต้องถอดอุปกรณ์มาต่อสาย USB

---

## 🏗️ System Architecture (สถาปัตยกรรมระบบ)

```text
[ ภาวะปกติ: UWB Precision ]
กำไลข้อมือ (UWB Tag) <---> เสารับสัญญาณ (UWB Anchors) ---> Firebase (พิกัด Real-time)

[ ภาวะตึกถล่ม: LoRa Penetration & Mesh ]
กำไลข้อมือ (LoRa) ---> กำไลข้างเคียง (ESP-NOW Relay) ---> เสารับสัญญาณภายนอก (LoRa Gateway)
      |
      v
เครื่องค้นหาพกพาของทีมกู้ภัย (LoRa Yagi Antenna) ---> วิเคราะห์และชี้เป้าหมายภาคพื้นดิน

[ ภาวะค้นหาระยะใกล้: UWB Last-Meter Pinpoint ]
เครื่องค้นหาพกพา (UWB Initiator) <===Two-Way Ranging===> กำไลข้อมือ (UWB Responder)
      |
      v
แสดงระยะทาง (cm) + ทิศทาง (PDoA Compass) + Haptic Feedback บนหน้าจอ OLED
```

---

## 🛠️ Hardware Components (อุปกรณ์ที่ใช้พัฒนา)

โปรเจกต์นี้แบ่งฮาร์ดแวร์ออกเป็น 3 ส่วนหลัก (จำนวนขึ้นอยู่กับการสเกลระบบหน้างาน):

### 1. Smart Wristband Node (กำไลข้อมือ)
* **Microcontroller:** `ESP32-C3` (บอร์ดขนาดเล็ก ประหยัดพลังงาน)
* **Penetration Comm:** `Ra-02 LoRa 433 MHz` + `เสาสปริง 433 MHz`
* **Precision Comm:** `โมดูล UWB (เช่น DW3000)` สำหรับระบุพิกัดความแม่นยำสูง
* **Sensors:** `MPU6050` (3-Axis Accelerometer) และ `GY-MAX30102` (Heart Rate & SpO2)
* **Misc:** แบตเตอรี่ LiPo 3.7V, โมดูลชาร์จ TP4056, เรกูเลเตอร์ AMS1117, และ Active Buzzer 3.3V
* **Enclosure:** เคสพิมพ์ 3 มิติ วัสดุ TPU ยืดหยุ่นและกันกระแทก

### 2. Gateway Pillar (เสารับสัญญาณรอบอาคาร)
* **Microcontroller:** `ESP32 Devkit`
* **Modules:** `Ra-02 LoRa 433 MHz` และ `UWB Anchor Module`
* **Antenna:** `เสายางทรงแท่ง 433 MHz (Omni-directional, แนะนำ 7 dBi)` พร้อมสายแปลง IPEX to SMA
* **Power:** ระบบไฟสำรองอิสระ (Battery 18650 Pack)

### 3. Handheld Sweeper (เครื่องรับสัญญาณพกพาสำหรับกู้ภัย)
* **Microcontroller:** `ESP32 Devkit` + `Ra-02 LoRa 433 MHz`
* **UWB Pinpoint Module:** `DW3000 (พร้อม PDoA Antenna Pair)` สำหรับวัดระยะและทิศทางระยะใกล้แบบความแม่นยำสูง
* **Display:** `OLED Display 1.3" (I2C, SH1106)` แสดง RSSI/SNR ของ LoRa, ระยะ UWB (cm), เข็มทิศชี้ทิศทาง และสถานะ Triage
* **Antenna:** `เสาอากาศ Yagi 433 MHz` (แบบกำหนดทิศทาง) สำหรับแกะรอยและชี้เป้าหมายระยะไกล
* **Feedback:** `Vibration Motor` (Haptic) และ `Buzzer Piezo` แจ้งเตือนระยะใกล้-ไกลแบบ Geiger Counter
* **Controls:** ปุ่มสลับโหมด `LoRa Sweep ↔ UWB Pinpoint` และปุ่ม Lock-on Target ID
* **Power:** `Battery 18650 x 2` พร้อม BMS, ทนใช้งานต่อเนื่อง ≥ 8 ชม. ในภาคสนาม

---

## 🎯 UWB Last-Meter Search (การค้นหาตำแหน่งระยะใกล้ด้วย Ultra-Wideband)

เมื่อเดินทางถึง "พื้นที่ต้องสงสัย" จากการกวาด LoRa Yagi ปัญหาถัดมาคือ **RSSI จะ saturate** และ multipath reflection จากเศษโลหะ/คอนกรีตจะทำให้ทิศทางผิดเพี้ยน Handheld Sweeper จึงสลับโหมดเข้าสู่ UWB Pinpoint เพื่อชี้เป้าระยะใกล้

### 🔬 หลักการทำงานทางเทคนิค

UWB ทำงานในย่านความถี่ **3.1–10.6 GHz** ด้วย bandwidth กว้างกว่า 500 MHz ส่งสัญญาณเป็นพัลส์สั้นมาก (< 2 ns) ทำให้:

| คุณสมบัติ | ข้อได้เปรียบในงาน US&R |
|---|---|
| **High Time Resolution** | วัดเวลาเดินทางของคลื่นได้แม่นยำระดับ sub-nanosecond แปลงเป็นระยะทาง ±10 ซม. |
| **Resistance to Multipath** | พัลส์สั้นทำให้แยกแยะคลื่นตรง (LoS) ออกจากคลื่นสะท้อนได้ ลดความผิดพลาดในพื้นที่ที่มีโลหะ |
| **Low Spectral Density** | กำลังส่งต่ำมาก (< -41.3 dBm/MHz) ไม่รบกวนวิทยุสื่อสารของทีมกู้ภัย |
| **Penetration ระยะใกล้** | ทะลุคอนกรีต/ผนังเบาได้ในระยะ 0.5–2 ม. (ขึ้นกับความหนาและความชื้น) เพียงพอสำหรับการขุดชั้นสุดท้าย |

### 📐 อัลกอริทึมการวัด

โหมด UWB ใน Handheld ใช้สองเทคนิคควบคู่กัน:

1. **Two-Way Ranging (TWR / DS-TWR)**
   - Handheld (Initiator) ส่ง Poll → กำไล (Responder) ตอบ Response → Handheld ส่ง Final
   - คำนวณ `Time-of-Flight (ToF)` แบบสองทาง ทำให้ไม่ต้อง sync clock ระหว่างอุปกรณ์
   - ระยะ = `(ToF × c) / 2` โดย c คือความเร็วแสง
   - ความแม่นยำ ±10 ซม. ในระยะ 0–30 ม.

2. **Phase Difference of Arrival (PDoA) — Angle of Arrival**
   - ใช้เสาอากาศ UWB คู่ (spacing ≈ λ/2) วัดความต่างเฟสของคลื่นที่มาถึง
   - คำนวณมุม `θ = arcsin((Δφ × λ) / (2π × d))`
   - ให้ผลเป็น **เข็มทิศชี้ทิศทาง** (Arrow on OLED) ไปยังเป้าหมาย
   - ครอบคลุมมุม ±45° ความละเอียด ~5°

### 🧭 Workflow การใช้งานภาคสนาม

```text
┌─────────────────────────────────────────────────────────────────┐
│  STEP 1: WIDE SEARCH (LoRa Yagi + RSSI Heatmap)                 │
│  ────────────────────────────────────────────────                │
│  เดินกวาดรอบซากอาคาร → หาทิศที่ RSSI สูงสุด → จดพิกัด GPS         │
│  ระยะ: 50–500 ม.   ความแม่นยำ: ±5–20 ม.                          │
└─────────────────────────────────────────────────────────────────┘
                            ↓ (RSSI > -70 dBm = ใกล้แล้ว)
┌─────────────────────────────────────────────────────────────────┐
│  STEP 2: PINPOINT MODE (กดปุ่มสลับเป็น UWB)                      │
│  ────────────────────────────────────────────────                │
│  Handheld เริ่ม TWR กับ Target ID ที่ Lock-on                    │
│  ระยะ: 0–30 ม.   ความแม่นยำ: ±10 ซม.                             │
│  OLED แสดง: [ระยะ 4.27 m] [→ ทิศทาง 23°] [Pulse ♥ 78 BPM]       │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  STEP 3: DIG-POINT CONFIRM (Haptic Geiger Mode)                 │
│  ────────────────────────────────────────────────                │
│  Vibration ถี่ขึ้นเมื่อใกล้ขึ้น (เหมือนเครื่องตรวจจับโลหะ)         │
│  เมื่อระยะ < 1 ม. → ปักธง / Mark Dig Point ให้ทีมขุดเจาะ          │
└─────────────────────────────────────────────────────────────────┘
```

### ⚠️ ข้อจำกัดและการรับมือ

| ข้อจำกัด | วิธีรับมือในการออกแบบ |
|---|---|
| UWB ทะลุคอนกรีตเสริมเหล็กหนา > 30 ซม. ได้ลดลงมาก | ใช้ LoRa เป็นตัวยืนยันการ "มีสัญญาณชีพ" ก่อน แล้วใช้ UWB เพื่อชี้จุดขุดสุดท้าย |
| ต้องมีพลังงานในกำไลเหลือพอสำหรับ UWB Ranging | กำไลจะอยู่ใน Deep-sleep แล้ว wake-up ตามคำสั่ง LoRa Wake Packet ก่อนเปิด UWB |
| PDoA แม่นเฉพาะใน Line-of-Sight | ใช้การเดินเก็บ Multiple Bearings (Triangulation) เพื่อยืนยันตำแหน่ง |
| คลื่น 6.5 GHz ถูกน้ำ/ดินชื้นดูดซับ | แสดง Confidence Score บนหน้าจอ ถ้าต่ำให้กู้ภัยใช้วิจารณญาณร่วม |

### 🔌 Pinout & Integration หลัก (ESP32 ↔ DW3000)

```text
DW3000          ESP32 Devkit
─────────       ─────────────
VCC      ───→   3.3V (ผ่าน LDO แยกจาก LoRa เพื่อลด noise)
GND      ───→   GND
SPI MISO ───→   GPIO 19
SPI MOSI ───→   GPIO 23
SPI SCK  ───→   GPIO 18
SPI CS   ───→   GPIO 4   (แยก bus จาก Ra-02 ที่ใช้ GPIO 5)
IRQ      ───→   GPIO 34
RST      ───→   GPIO 27
```

> 💡 **Tip:** ใช้ SPI bus เดียวกับ Ra-02 ได้ แต่ต้องระวัง CS แยกขา และจัดลำดับ transaction ให้ชัดเจน เพื่อไม่ให้ UWB ranging ผิดจังหวะตอน LoRa กำลังรับ-ส่ง

---

## 🔄 Fleet OTA Self-Update System (ระบบอัพเดท Firmware อัตโนมัติ)

อุปกรณ์ทุกตัวในระบบรองรับการอัพเดท firmware แบบ Over-The-Air โดยไม่ต้องถอดมาต่อสาย USB โดยใช้ **Gateway Pillar เป็นศูนย์กลางอัพเดท** ที่คอยรับ firmware จาก Cloud แล้วกระจายต่อไปยังอุปกรณ์ปลายทาง

### 🗺️ OTA Update Flow (ลำดับการอัพเดทในระบบ)

```text
┌──────────────────────────────────────────────────────────────────────┐
│  CLOUD / COMMAND CENTER                                              │
│  Developer push binary → Firebase Storage / GitHub Releases         │
│  Firebase RTDB: /ota/latest = { version: "1.2.3", url: "...", md5 } │
└───────────────────────────┬──────────────────────────────────────────┘
                            │ WiFi HTTPS Pull (ตรวจสอบทุก 6 ชม.)
                            ▼
┌──────────────────────────────────────────────────────────────────────┐
│  GATEWAY PILLAR (WiFi + ESP-NOW)                                     │
│  1. ดึง firmware .bin จาก Firebase Storage                           │
│  2. ตรวจสอบ MD5 / SHA256                                             │
│  3. เขียนลง OTA_1 partition → Reboot → ทำงานบน firmware ใหม่        │
│  4. ประกาศ OTA_AVAILABLE broadcast ผ่าน ESP-NOW                      │
└──────┬────────────────────────────────────────────────┬──────────────┘
       │ ESP-NOW Chunk Protocol                         │ WiFi AP (ถ้ามี)
       ▼                                                ▼
┌──────────────────┐                        ┌───────────────────────┐
│  WRISTBAND NODES │                        │  HANDHELD SWEEPER     │
│  รับ chunks ทีละ │                        │  OTA ผ่าน WiFi (ก่อน)│
│  240 bytes จาก   │                        │  หรือรับ ESP-NOW      │
│  Gateway         │                        │  จาก Gateway (สำรอง) │
│  → OTA_1 write   │                        │  → OTA_1 write        │
│  → MD5 verify    │                        │  → MD5 verify         │
│  → Reboot        │                        │  → Reboot             │
└──────────────────┘                        └───────────────────────┘
```

### 📦 Partition Scheme (การแบ่ง Flash สำหรับ OTA)

แต่ละอุปกรณ์ต้องมี 2 partition สำหรับ firmware สลับกัน (A/B Update):

**ESP32-C3 Wristband (4 MB Flash)**
```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1E0000,
app1,     app,  ota_1,   0x1F0000, 0x1E0000,
spiffs,   data, spiffs,  0x3D0000, 0x30000,
```

> ⚠️ firmware ต้องมีขนาด < 1.9 MB — จัดการด้วยการปิด debug log ใน production build และใช้ `CONFIG_COMPILER_OPTIMIZATION_SIZE`

**ESP32 Devkit Gateway/Handheld (4 MB Flash)**
```csv
# Name,   Type, SubType, Offset,   Size,
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x180000,
app1,     app,  ota_1,   0x190000, 0x180000,
spiffs,   data, spiffs,  0x310000, 0xF0000,
```

### 🔁 ESP-NOW Chunk Protocol (สำหรับอัพเดท Wristband)

เนื่องจาก Wristband ไม่มี WiFi ในโหมดฉุกเฉิน Gateway จึงส่ง firmware ผ่าน ESP-NOW ด้วย custom chunking protocol:

```text
Packet Structure (250 bytes max per ESP-NOW frame):
┌────────┬──────────┬──────────┬──────────┬──────────────────────────┐
│ CMD    │ CHUNK_ID │ TOTAL    │ CRC16    │ PAYLOAD (up to 240 bytes)│
│ 1 byte │ 2 bytes  │ 2 bytes  │ 2 bytes  │ 240 bytes                │
└────────┴──────────┴──────────┴──────────┴──────────────────────────┘

CMD values:
  0x01 = OTA_BEGIN    (แจ้ง total size, version, MD5)
  0x02 = OTA_DATA     (chunk payload)
  0x03 = OTA_END      (MD5 final verify + apply)
  0x04 = OTA_ACK      (Wristband ตอบกลับแต่ละ chunk)
  0x05 = OTA_ABORT    (ยกเลิก — ถ้า CRC error > 3 ครั้ง)
```

**ระยะเวลาโดยประมาณ** (firmware 400 KB, ESP-NOW @ 1 Mbps effective ~250 kbps):
- จำนวน chunks: ~1,703 packets
- เวลา: ~7–10 นาที (รวม ACK roundtrip และ retry)
- กำไลต้องอยู่ใน ESP-NOW range (<100 ม. จาก Gateway)

### 🔒 ความปลอดภัยและ Rollback

| มาตรการ | รายละเอียด |
|---|---|
| **MD5/SHA256 Verify** | ตรวจ checksum หลัง download ครบ ก่อน apply ทุกครั้ง |
| **Version Guard** | ปฏิเสธ firmware ที่ version เก่ากว่า version ปัจจุบัน (ป้องกัน downgrade attack) |
| **Boot Count Watchdog** | ถ้า boot ใหม่แล้ว crash > 3 ครั้ง → `esp_ota_mark_app_invalid_rollback_and_reboot()` คืน firmware เดิมอัตโนมัติ |
| **HTTPS + TLS** | Gateway ดึง firmware ผ่าน HTTPS เท่านั้น ใช้ Firebase SSL certificate pinning |
| **OTA Freeze Mode** | ระบบล็อคการอัพเดทเมื่ออยู่ในโหมดฉุกเฉิน (Event Active Flag ใน RTDB) เพื่อไม่ให้ reboot กลางภารกิจ |

### ⚙️ Firmware Version Management (ใน `platformio.ini`)

```ini
[env:wristband]
board = esp32-c3-devkitm-1
build_flags =
  -D FW_VERSION=\"1.2.3\"
  -D DEVICE_TYPE=WRISTBAND
  -D OTA_CHECK_INTERVAL_MS=21600000   ; ตรวจสอบทุก 6 ชม.
  -D OTA_CHUNK_SIZE=240
  -D OTA_MAX_RETRY=3
board_build.partitions = partitions_wristband.csv
```

```ini
[env:gateway]
board = esp32dev
build_flags =
  -D FW_VERSION=\"1.2.3\"
  -D DEVICE_TYPE=GATEWAY
  -D OTA_CHECK_INTERVAL_MS=21600000
  -D OTA_ESPNOW_BCAST_DELAY_MS=500   ; หน่วง 500ms ก่อน broadcast ให้ Wristband
board_build.partitions = partitions_gateway.csv
```

### 🚀 การ Deploy Firmware ใหม่ (สำหรับนักพัฒนา)

```bash
# 1. Build binary ด้วย PlatformIO
pio run -e wristband --target buildfs
pio run -e wristband

# 2. อัพโหลด binary ไป Firebase Storage
firebase deploy --only storage

# 3. อัพเดท version record ใน RTDB
firebase database:update /ota/wristband '{
  "version": "1.2.3",
  "url": "gs://resq-band.appspot.com/firmware/wristband_1.2.3.bin",
  "md5": "a1b2c3d4e5f6...",
  "release_notes": "เพิ่ม UWB Pinpoint mode",
  "force_update": false
}'

# 4. Gateway จะ poll และกระจาย firmware ภายใน 6 ชั่วโมง
#    หรือ trigger manual: RTDB /ota/trigger = true
```

---

## 💻 Software & Services

* **Embedded C/C++:** พัฒนาผ่าน VS Code (PlatformIO) หรือ Arduino IDE
* **Database & Dashboard:** Firebase Realtime Database สำหรับรับส่งข้อมูล, OTA version management และ Web Application สำหรับแสดงผลหน้าจอ Command Center
* **OTA Storage:** Firebase Storage สำหรับเก็บ firmware binary ที่พร้อม deploy
* **3D CAD:** SolidWorks หรือ Fusion 360 สำหรับออกแบบเคสอุปกรณ์

---

## 🤝 Contributing
โปรเจกต์นี้ยินดีรับการปรับปรุงและพัฒนาต่อยอดจากนักพัฒนาทุกท่าน หากพบปัญหาหรือมีข้อเสนอแนะ สามารถเปิด Issue หรือส่ง Pull Request ได้เลยครับ

## 📄 License
Distributed under the MIT License. See `LICENSE` for more information.
